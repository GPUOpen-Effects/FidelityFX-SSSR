//_____________________________________________________________/\_______________________________________________________________
//==============================================================================================================================
//
//                                         [DS] Downsampler 0.1
//
//==============================================================================================================================
// LICENSE
// =======
// Copyright (c) 2017-2020 Advanced Micro Devices, Inc. All rights reserved.
// -------
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// -------
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// -------
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//------------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------------
// INTEGRATION SUMMARY FOR CPU
// ===========================
// // you need to provide as constants:
// // number of mip levels to be computed (maximum is 12)
// // number of total thread groups: ((widthInPixels+63)>>6) * ((heightInPixels+63)>>6)
// ...
// // Dispatch the shader such that each thread group works on a 64x64 sub-tile of the source image
// vkCmdDispatch(cmdBuf,(widthInPixels+63)>>6,(heightInPixels+63)>>6,1);

//------------------------------------------------------------------------------------------------------------------------------
// INTEGRATION SUMMARY FOR GPU
// ===========================

// [SAMPLER] - if you want to use a sampler for loading the source image
// follow additionally the instructions marked with [SAMPLER]
// this is recommended, as it's the more optimal path

// // Setup layout. Example below for VK_FORMAT_R16G16B16A16_SFLOAT.
// // Note: If you use UNORM/SRGB format, you need to convert to linear space
// // as this is not done automatically via this downsampler approach
// // conversion to linear (load function): x*x
// // conversion from linear (store function): sqrt()

// // source image
// GLSL: layout(set=0,binding=0,rgba16f)uniform image2D imgSrc;
// [SAMPLER]: layout(set=0,binding=0)uniform texture2D imgSrc;
// HLSL: [[vk::binding(0)]] Texture2D<float4> imgSrc :register(u0);

// // destination -> 12 is the maximum number of mips supported by DS
// GLSL: layout(set=0,binding=1,rgba16f) uniform coherent image2D imgDst[12];
// HLSL: [[vk::binding(1)]] globallycoherent RWTexture2D<float4> imgDst[12] :register(u1);

// // global atomic counter - MUST be initialized to 0
// // GLSL:
// layout(std430, set=0, binding=2) buffer perThreadgroup
// {
//    uint counter;
// } globalAtomic;
// // HLSL:
// struct perThreadgroup
// {
//    uint counter;
// };
// [[vk::binding(2)]] RWStructuredBuffer<perThreadgroup> globalAtomic;

// // [SAMPLER] add sampler
// GLSL: layout(set=0, binding=3) uniform sampler srcSampler;
// HLSL: [[vk::binding(3)]] SamplerState srcSampler :register(s0);

// // constants - either push constant or constant buffer
// // or calculate within shader
// // [SAMPLER] when using sampler add inverse source image size
// // GLSL:
// layout(push_constant) uniform pushConstants {
//	  uint mips; // needed to opt out earlier if mips are < 12
//	  uint numWorkGroups; // number of total thread groups, so numWorkGroupsX * numWorkGroupsY * numWorkGroupsZ
// } myPerMip;
// // HLSL:
// [[vk::push_constant]]
// cbuffer myPerMip {
//	uint mips;
//	uint numWorkGroups;
// };

// ...
// // Setup pre-portability-header defines (sets up GLSL/HLSL path, etc)
// #define A_GPU 1
// #define A_GLSL 1 // or // #define A_HLSL 1

// // if you want to use PACKED version
// // recommended if bpc <= 16bit
// #define A_HALF

// ...
// // Include the portability header (or copy it in without an include).
// #include "ffx_a.h"
// ...

// // Define LDS variables
// shared AF4 spd_intermediate[16][16]; // HLSL: groupshared
// shared AU1 spd_counter; // HLSL: groupshared
// // PACKED version
// shared AH4 spd_intermediate[16][16]; // HLSL: groupshared
// // Note: You can also use
// shared AF1 spd_intermediateR[16][16];
// shared AF1 spd_intermediateG[16][16];
// shared AF1 spd_intermediateB[16][16];
// shared AF1 spd_intermediateA[16][16];
// // or for Packed version:
// shared AH2 spd_intermediateRG[16][16];
// shared AH2 spd_intermediateBA[16][16];
// // This is potentially faster
// // Adapt your load and store functions accordingly

// // if subgroup operations are not supported / can't use SM6.0
// #define SPD_NO_WAVE_OPERATIONS

// // Define the fetch function(s) and the reduction function
// // if non-power-of-2 textures, add border controls to the load and store functions
// // to make sure the borders of the mip level look as you it
// // this is also the place where you need to do color space transformation if needed
// // E.g. if your texture format is SRGB/UNORM and you use the UAV load and store functions
// // no automatic to/from linear conversions are happening
// // there is to/from linear conversions when using a sampler and render target approach
// // conversion to linear (load function): x*x
// // conversion from linear (store function): sqrt()

// // Load from source image
// GLSL: AF4 DSLoadSourceImage(ASU2 p){return imageLoad(imgSrc, p);}
// HLSL: AF4 DSLoadSourceImage(ASU2 tex){return imgSrc[tex];}
// [SAMPLER] you can also use a sampler, but then you need to modify also
// AF4 ReduceLoadSourceImage4(AU2 base) { ... } to
// AF4 ReduceLoadSourceImage4(AU2 base)
// {
//   return DSLoadSourceImage(ASU2(base));
// }
// GLSL:
// AF4 DSLoadSourceImage(ASU2 p){
//	  AF2 textureCoord = p * invInputSize + invInputSize;
//	  return texture(sampler2D(imgSrc, srcSampler), textureCoord);
//	}
// HLSL:
// AF4 DSLoadSourceImage(ASU2 p){
//	  AF2 textureCoord = p * invInputSize + invInputSize;
//	  return imgSrc.SampleLevel(srcSampler, textureCoord, 0);
//	}

// [SAMPLER] make sure the sampler matches your defined reduction function :)
// [SAMPLER] if you can use a sampler, use one - it's the more optimal path

// // DSLoad() takes a 32-bit signed integer 2D coordinate and loads color.
// // Loads the 5th mip level, each value is computed by a different thread group
// // last thread group will access all its elements and compute the subsequent mips
// GLSL: AF4 DSLoad(ASU2 p){return imageLoad(imgDst[5],p);}
// HLSL: AF4 DSLoad(ASU2 tex){return imgDst[5][tex];}

// Define the store function
// GLSL: void DSStore(ASU2 p, AF4 value, AU1 mip){imageStore(imgDst[mip], p, value);}
// HLSL: void DSStore(ASU2 pix, AF4 value, AU1 index){imgDst[index][pix] = value;}

// // Define the atomic counter increase function
// // GLSL:
// void DSIncreaseAtomicCounter(){spd_counter = atomicAdd(globalAtomic.counter, 1);}
// AU1 DSGetAtomicCounter() {return spd_counter;}
// // HLSL:
// void DSIncreaseAtomicCounter(){InterlockedAdd(globalAtomic[0].counter, 1, spd_counter);}
// AU1 DSGetAtomicCounter(){return spd_counter;}

// // Define the lds load and store functions
// // GLSL:
// AF4 DSLoadIntermediate(AU1 x, AU1 y){return spd_intermediate[x][y];}
// void DSStoreIntermediate(AU1 x, AU1 y, AF4 value){spd_intermediate[x][y] = value;}
// // HLSL:
// AF4 DSLoadIntermediate(AU1 x, AU1 y){return spd_intermediate[x][y];}
// void DSStoreIntermediate(AU1 x, AU1 y, AF4 value){spd_intermediate[x][y] = value;}

// // Define your reduction function: takes as input the four 2x2 values and returns 1 output value
// Example below: computes the average value
// AF4 DSReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return (v0+v1+v2+v3)*0.25;}

// // PACKED VERSION
// Load from source image
// GLSL: AH4 DSLoadSourceImageH(ASU2 p){return AH4(imageLoad(imgSrc, p));}
// HLSL: AH4 DSLoadSourceImageH(ASU2 tex){return AH4(imgSrc[tex]);}
// [SAMPLER] you can also use a sampler, but then you need to modify also
// AH4 ReduceLoadSourceImage4H(AU2 base) { ... } to
// AH4 ReduceLoadSourceImage4H(AU2 base)
// {
//   return DSLoadSourceImageH(ASU2(base));
// }
// GLSL:
// AH4 DSLoadSourceImageH(ASU2 p){
//	  AF2 textureCoord = p * invInputSize + invInputSize;
//	  return AH4(texture(sampler2D(imgSrc, srcSampler), textureCoord));
//	}
// HLSL:
// AH4 DSLoadSourceImageH(ASU2 p){
//	  AF2 textureCoord = p * invInputSize + invInputSize;
//	  return AH4(imgSrc.SampleLevel(srcSampler, textureCoord, 0));
//	}

// [SAMPLER] make sure the sampler matches your defined reduction function :)
// [SAMPLER] if you can use a sampler, use one - it's the more optimal path

// // DSLoadH() takes a 32-bit signed integer 2D coordinate and loads color.
// // Loads the 5th mip level, each value is computed by a different thread group
// // last thread group will access all its elements and compute the subsequent mips
// GLSL: AH4 DSLoadH(ASU2 p){return AH4(imageLoad(imgDst[5],p));}
// HLSL: AH4 DSLoadH(ASU2 tex){return AH4(imgDst[5][tex]);}

// Define the store function
// GLSL: void DSStoreH(ASU2 p, AH4 value, AU1 mip){imageStore(imgDst[mip], p, AF4(value));}
// HLSL: void DSStoreH(ASU2 pix, AH4 value, AU1 index){imgDst[index][pix] = AF4(value);}

// // Define the atomic counter increase function
// // GLSL:
// void DSIncreaseAtomicCounter(){spd_counter = atomicAdd(globalAtomic.counter, 1);}
// AU1 DSGetAtomicCounter() {return spd_counter;}
// // HLSL:
// void DSIncreaseAtomicCounter(){InterlockedAdd(globalAtomic[0].counter, 1, spd_counter);}
// AU1 DSGetAtomicCounter(){return spd_counter;}

// // Define the lds load and store functions
// // GLSL:
// AH4 DSLoadIntermediateH(AU1 x, AU1 y){return spd_intermediate[x][y];}
// void DSStoreIntermediateH(AU1 x, AU1 y, AH4 value){spd_intermediate[x][y] = value;}
// // HLSL:
// AH4 DSLoadIntermediate(AU1 x, AU1 y){return spd_intermediate[x][y];}
// void DSStoreIntermediate(AU1 x, AU1 y, AH4 value){spd_intermediate[x][y] = value;}

// // Define your reduction function: takes as input the four 2x2 values and returns 1 output value
// Example below: computes the average value
// AH4 DSReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3){return (v0+v1+v2+v3)*AH1(0.25);}

// // If you only use PACKED version
// #define SPD_PACKED_ONLY

// // Include this SPD (single pass downsampler) header file (or copy it in without an include).
// #include "ffx_spd.h"
// ...

// // Example in shader integration
// // GLSL:
// layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
// void main(){
//  // Call the downsampling function
//  Downsample(AU2(gl_WorkGroupID.xy), AU1(gl_LocalInvocationIndex), 
//  AU1(myPerMip.mips), AU1(myPerMip.numWorkGroups));
//
// // PACKED:
// DownsampleH(AU2(gl_WorkGroupID.xy), AU1(gl_LocalInvocationIndex), 
//		AU1(myPerMip.mips), AU1(myPerMip.numWorkGroups));
// ...
// // HLSL:
// [numthreads(256,1,1)]
// void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex) {
//    Downsample(AU2(WorkGroupId.xy), AU1(LocalThreadIndex),  
//		AU1(mips), AU1(numWorkGroups));
//
// // PACKED:
// DownsampleH(AU2(WorkGroupId.xy), AU1(LocalThreadIndex),  
//		AU1(mips), AU1(numWorkGroups));
// ...

//
//------------------------------------------------------------------------------------------------------------------------------



//==============================================================================================================================
//                                                     NON-PACKED VERSION
//==============================================================================================================================

#ifdef SPD_PACKED_ONLY
  // Avoid compiler error
  AF4 DSLoadSourceImage(ASU2 p){return AF4(0.0,0.0,0.0,0.0);}
  AF4 DSLoad(ASU2 p){return AF4(0.0,0.0,0.0,0.0);}
  void DSStore(ASU2 p, AF4 value, AU1 mip){}
  AF4 DSLoadIntermediate(AU1 x, AU1 y){return AF4(0.0,0.0,0.0,0.0);}
  void DSStoreIntermediate(AU1 x, AU1 y, AF4 value){}
  AF4 DSReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return AF4(0.0,0.0,0.0,0.0);}
#endif

//_____________________________________________________________/\_______________________________________________________________
#if defined(A_GLSL) && !defined(SPD_NO_WAVE_OPERATIONS)
#extension GL_KHR_shader_subgroup_quad:require
#endif

void DSWorkgroupShuffleBarrier() {
#ifdef A_GLSL
    barrier();
#endif 
#ifdef A_HLSL
    GroupMemoryBarrierWithGroupSync();
#endif
}

// Only last active workgroup should proceed
bool DSExitWorkgroup(AU1 numWorkGroups, AU1 localInvocationIndex) 
{
    // global atomic counter
    if (localInvocationIndex == 0)
    {
        DSIncreaseAtomicCounter();
    }
    DSWorkgroupShuffleBarrier();
    return (DSGetAtomicCounter() != (numWorkGroups - 1));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// User defined: AF4 DSReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3);

AF4 ReduceQuad(AF4 v)
{
    #if defined(A_GLSL) && !defined(SPD_NO_WAVE_OPERATIONS)
    AF4 v0 = v;
    AF4 v1 = subgroupQuadSwapHorizontal(v);
    AF4 v2 = subgroupQuadSwapVertical(v);
    AF4 v3 = subgroupQuadSwapDiagonal(v);
    return DSReduce4(v0, v1, v2, v3);
    #elif defined(A_HLSL)
    // requires SM6.0
    AU1 quad = WaveGetLaneIndex() &  (~0x3);
    AF4 v0 = v;
    AF4 v1 = WaveReadLaneAt(v, quad | 1);
    AF4 v2 = WaveReadLaneAt(v, quad | 2);
    AF4 v3 = WaveReadLaneAt(v, quad | 3);
    return DSReduce4(v0, v1, v2, v3);
    /*
    // if SM6.0 is not available, you can use the AMD shader intrinsics
    // works for DX11
    AF4 v0 = v;
    AF4 v1;
    v1.x = AmdExtD3DShaderIntrinsics_SwizzleF(v.x, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    v1.y = AmdExtD3DShaderIntrinsics_SwizzleF(v.y, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    v1.z = AmdExtD3DShaderIntrinsics_SwizzleF(v.z, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    v1.w = AmdExtD3DShaderIntrinsics_SwizzleF(v.w, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    AF4 v2;
    v2.x = AmdExtD3DShaderIntrinsics_SwizzleF(v.x, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    v2.y = AmdExtD3DShaderIntrinsics_SwizzleF(v.y, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    v2.z = AmdExtD3DShaderIntrinsics_SwizzleF(v.z, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    v2.w = AmdExtD3DShaderIntrinsics_SwizzleF(v.w, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    AF4 v3;
    v3.x = AmdExtD3DShaderIntrinsics_SwizzleF(v.x, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    v3.y = AmdExtD3DShaderIntrinsics_SwizzleF(v.y, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    v3.z = AmdExtD3DShaderIntrinsics_SwizzleF(v.z, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    v3.w = AmdExtD3DShaderIntrinsics_SwizzleF(v.w, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    return DSReduce4(v0, v1, v2, v3);
    */
    #endif
    return AF4_x(0.0);
}

AF4 ReduceIntermediate(AU2 i0, AU2 i1, AU2 i2, AU2 i3)
{
    AF4 v0 = DSLoadIntermediate(i0.x, i0.y);
    AF4 v1 = DSLoadIntermediate(i1.x, i1.y);
    AF4 v2 = DSLoadIntermediate(i2.x, i2.y);
    AF4 v3 = DSLoadIntermediate(i3.x, i3.y);
    return DSReduce4(v0, v1, v2, v3);
}

AF4 ReduceLoad4(AU2 i0, AU2 i1, AU2 i2, AU2 i3)
{
    AF4 v0 = DSLoad(ASU2(i0));
    AF4 v1 = DSLoad(ASU2(i1));
    AF4 v2 = DSLoad(ASU2(i2));
    AF4 v3 = DSLoad(ASU2(i3));
    return DSReduce4(v0, v1, v2, v3);
}

AF4 ReduceLoad4(AU2 base)
{
    return ReduceLoad4(
        AU2(base + AU2(0, 0)),
        AU2(base + AU2(0, 1)), 
        AU2(base + AU2(1, 0)), 
        AU2(base + AU2(1, 1)));
}

AF4 ReduceLoadSourceImage4(AU2 i0, AU2 i1, AU2 i2, AU2 i3)
{
    AF4 v0 = DSLoadSourceImage(ASU2(i0));
    AF4 v1 = DSLoadSourceImage(ASU2(i1));
    AF4 v2 = DSLoadSourceImage(ASU2(i2));
    AF4 v3 = DSLoadSourceImage(ASU2(i3));
    return DSReduce4(v0, v1, v2, v3);
}

AF4 ReduceLoadSourceImage4(AU2 base)
{
    // [SAMPLER] use:
    return DSLoadSourceImage(ASU2(base));
    // if no sampler use:
    /*return ReduceLoadSourceImage4(
        AU2(base + AU2(0, 0)),
        AU2(base + AU2(0, 1)), 
        AU2(base + AU2(1, 0)), 
        AU2(base + AU2(1, 1)));*/
}

void DownsampleMips_0_1_Intrinsics(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
    AF4 v[4];

    ASU2 tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2);
    ASU2 pix = ASU2(workGroupID.xy * 32) + ASU2(x, y);
    v[0] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[0], 0);

    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y);
    v[1] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[1], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x, y + 16);
    v[2] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[2], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y + 16);
    v[3] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[3], 0);

    if (mip <= 1)
        return;

    v[0] = ReduceQuad(v[0]);
    v[1] = ReduceQuad(v[1]);
    v[2] = ReduceQuad(v[2]);
    v[3] = ReduceQuad(v[3]);

    if ((localInvocationIndex % 4) == 0)
    {
        DSStore(ASU2(workGroupID.xy * 16) + 
            ASU2(x/2, y/2), v[0], 1);
        DSStoreIntermediate(
            x/2, y/2, v[0]);

        DSStore(ASU2(workGroupID.xy * 16) + 
            ASU2(x/2 + 8, y/2), v[1], 1);
        DSStoreIntermediate(
            x/2 + 8, y/2, v[1]);

        DSStore(ASU2(workGroupID.xy * 16) + 
            ASU2(x/2, y/2 + 8), v[2], 1);
        DSStoreIntermediate(
            x/2, y/2 + 8, v[2]);

        DSStore(ASU2(workGroupID.xy * 16) + 
            ASU2(x/2 + 8, y/2 + 8), v[3], 1);
        DSStoreIntermediate(
            x/2 + 8, y/2 + 8, v[3]);
    }
}

void DownsampleMips_0_1_LDS(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip) 
{
    AF4 v[4];

    ASU2 tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2);
    ASU2 pix = ASU2(workGroupID.xy * 32) + ASU2(x, y);
    v[0] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[0], 0);

    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y);
    v[1] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[1], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x, y + 16);
    v[2] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[2], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y + 16);
    v[3] = ReduceLoadSourceImage4(tex);
    DSStore(pix, v[3], 0);

    if (mip <= 1)
        return;

    for (int i = 0; i < 4; i++)
    {
        DSStoreIntermediate(x, y, v[i]);
        DSWorkgroupShuffleBarrier();
        if (localInvocationIndex < 64)
        {
            v[i] = ReduceIntermediate(
                AU2(x * 2 + 0, y * 2 + 0),
                AU2(x * 2 + 1, y * 2 + 0),
                AU2(x * 2 + 0, y * 2 + 1),
                AU2(x * 2 + 1, y * 2 + 1)
            );
            DSStore(ASU2(workGroupID.xy * 16) + ASU2(x + (i % 2) * 8, y + (i / 2) * 8), v[i], 1);
        }
        DSWorkgroupShuffleBarrier();
    }

    if (localInvocationIndex < 64)
    {
        DSStoreIntermediate(x + 0, y + 0, v[0]);
        DSStoreIntermediate(x + 8, y + 0, v[1]);
        DSStoreIntermediate(x + 0, y + 8, v[2]);
        DSStoreIntermediate(x + 8, y + 8, v[3]);
    }
}

void DownsampleMips_0_1(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip) 
{
#ifdef SPD_NO_WAVE_OPERATIONS
    DownsampleMips_0_1_LDS(x, y, workGroupID, localInvocationIndex, mip);
#else
    DownsampleMips_0_1_Intrinsics(x, y, workGroupID, localInvocationIndex, mip);
#endif
}


void DownsampleMip_2(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 64)
    {
        AF4 v = ReduceIntermediate(
            AU2(x * 2 + 0 + 0, y * 2 + 0),
            AU2(x * 2 + 0 + 1, y * 2 + 0),
            AU2(x * 2 + 0 + 0, y * 2 + 1),
            AU2(x * 2 + 0 + 1, y * 2 + 1)
        );
        DSStore(ASU2(workGroupID.xy * 8) + ASU2(x, y), v, mip);
        // store to LDS, try to reduce bank conflicts
        // x 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0 x
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // x 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0
        // ...
        // x 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0
        DSStoreIntermediate(x * 2 + y % 2, y * 2, v);
    }
#else
    AF4 v = DSLoadIntermediate(x, y);
    v = ReduceQuad(v);
    // quad index 0 stores result
    if (localInvocationIndex % 4 == 0)
    {   
        DSStore(ASU2(workGroupID.xy * 8) + ASU2(x/2, y/2), v, mip);
        DSStoreIntermediate(x + (y/2) % 2, y, v);
    }
#endif
}

void DownsampleMip_3(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 16)
    {
        // x 0 x 0
        // 0 0 0 0
        // 0 x 0 x
        // 0 0 0 0
        AF4 v = ReduceIntermediate(
            AU2(x * 4 + 0 + 0, y * 4 + 0),
            AU2(x * 4 + 2 + 0, y * 4 + 0),
            AU2(x * 4 + 0 + 1, y * 4 + 2),
            AU2(x * 4 + 2 + 1, y * 4 + 2)
        );
        DSStore(ASU2(workGroupID.xy * 4) + ASU2(x, y), v, mip);
        // store to LDS
        // x 0 0 0 x 0 0 0 x 0 0 0 x 0 0 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 x 0 0 0 x 0 0 0 x 0 0 0 x 0 0
        // ...
        // 0 0 x 0 0 0 x 0 0 0 x 0 0 0 x 0
        // ...
        // 0 0 0 x 0 0 0 x 0 0 0 x 0 0 0 x
        // ...
        DSStoreIntermediate(x * 4 + y, y * 4, v);
    }
#else
    if (localInvocationIndex < 64)
    {
        AF4 v = DSLoadIntermediate(x * 2 + y % 2,y * 2);
        v = ReduceQuad(v);
        // quad index 0 stores result
        if (localInvocationIndex % 4 == 0)
        {   
            DSStore(ASU2(workGroupID.xy * 4) + ASU2(x/2, y/2), v, mip);
            DSStoreIntermediate(x * 2 + y/2, y * 2, v);
        }
    }
#endif
}

void DownsampleMip_4(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 4)
    {
        // x 0 0 0 x 0 0 0
        // ...
        // 0 x 0 0 0 x 0 0
        AF4 v = ReduceIntermediate(
            AU2(x * 8 + 0 + 0 + y * 2, y * 8 + 0),
            AU2(x * 8 + 4 + 0 + y * 2, y * 8 + 0),
            AU2(x * 8 + 0 + 1 + y * 2, y * 8 + 4),
            AU2(x * 8 + 4 + 1 + y * 2, y * 8 + 4)
        );
        DSStore(ASU2(workGroupID.xy * 2) + ASU2(x, y), v, mip);
        // store to LDS
        // x x x x 0 ...
        // 0 ...
        DSStoreIntermediate(x + y * 2, 0, v);
    }
#else
    if (localInvocationIndex < 16)
    {
        AF4 v = DSLoadIntermediate(x * 4 + y,y * 4);
        v = ReduceQuad(v);
        // quad index 0 stores result
        if (localInvocationIndex % 4 == 0)
        {   
            DSStore(ASU2(workGroupID.xy * 2) + ASU2(x/2, y/2), v, mip);
            DSStoreIntermediate(x / 2 + y, 0, v);
        }
    }
#endif
}

void DownsampleMip_5(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 1)
    {
        // x x x x 0 ...
        // 0 ...
        AF4 v = ReduceIntermediate(
            AU2(0, 0),
            AU2(1, 0),
            AU2(2, 0),
            AU2(3, 0)
        );
        DSStore(ASU2(workGroupID.xy), v, mip);
    }
#else
    if (localInvocationIndex < 4)
    {
        AF4 v = DSLoadIntermediate(localInvocationIndex,0);
        v = ReduceQuad(v);
        // quad index 0 stores result
        if (localInvocationIndex % 4 == 0)
        {   
            DSStore(ASU2(workGroupID.xy), v, mip);
        }
    }
#endif
}

void DownsampleMips_6_7(AU1 x, AU1 y, AU1 mips)
{
    ASU2 tex = ASU2(x * 4 + 0, y * 4 + 0);
    ASU2 pix = ASU2(x * 2 + 0, y * 2 + 0);
    AF4 v0 = ReduceLoad4(tex);
    DSStore(pix, v0, 6);

    tex = ASU2(x * 4 + 2, y * 4 + 0);
    pix = ASU2(x * 2 + 1, y * 2 + 0);
    AF4 v1 = ReduceLoad4(tex);
    DSStore(pix, v1, 6);

    tex = ASU2(x * 4 + 0, y * 4 + 2);
    pix = ASU2(x * 2 + 0, y * 2 + 1);
    AF4 v2 = ReduceLoad4(tex);
    DSStore(pix, v2, 6);

    tex = ASU2(x * 4 + 2, y * 4 + 2);
    pix = ASU2(x * 2 + 1, y * 2 + 1);
    AF4 v3 = ReduceLoad4(tex);
    DSStore(pix, v3, 6);

    if (mips <= 7) return;
    // no barrier needed, working on values only from the same thread

    AF4 v = DSReduce4(v0, v1, v2, v3);
    DSStore(ASU2(x, y), v, 7);
    DSStoreIntermediate(x, y, v);
}

void DownsampleNextFour(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 baseMip, AU1 mips)
{
    if (mips <= baseMip) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_2(x, y, workGroupID, localInvocationIndex, baseMip);

    if (mips <= baseMip + 1) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_3(x, y, workGroupID, localInvocationIndex, baseMip + 1);

    if (mips <= baseMip + 2) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_4(x, y, workGroupID, localInvocationIndex, baseMip + 2);

    if (mips <= baseMip + 3) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_5(x, y, workGroupID, localInvocationIndex, baseMip + 3);
}

void Downsample(
    AU2 workGroupID,
    AU1 localInvocationIndex,
    AU1 mips,
    AU1 numWorkGroups
) {
    AU2 sub_xy = ARmpRed8x8(localInvocationIndex % 64);
    AU1 x = sub_xy.x + 8 * ((localInvocationIndex >> 6) % 2);
    AU1 y = sub_xy.y + 8 * ((localInvocationIndex >> 7));
    DownsampleMips_0_1(x, y, workGroupID, localInvocationIndex, mips);

    DownsampleNextFour(x, y, workGroupID, localInvocationIndex, 2, mips);

    if (mips <= 6) return;

    if (DSExitWorkgroup(numWorkGroups, localInvocationIndex)) return;

    // After mip 6 there is only a single workgroup left that downsamples the remaining up to 64x64 texels.
    DownsampleMips_6_7(x, y, mips);

    DownsampleNextFour(x, y, AU2(0,0), localInvocationIndex, 8, mips);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//==============================================================================================================================
//                                                       PACKED VERSION
//==============================================================================================================================

#ifdef A_HALF // A_HALF

#ifdef A_GLSL
#extension GL_EXT_shader_subgroup_extended_types_float16:require
#endif

AH4 ReduceQuadH(AH4 v)
{
    #if defined(A_GLSL) && !defined(SPD_NO_WAVE_OPERATIONS)
    AH4 v0 = v;
    AH4 v1 = subgroupQuadSwapHorizontal(v);
    AH4 v2 = subgroupQuadSwapVertical(v);
    AH4 v3 = subgroupQuadSwapDiagonal(v);
    return DSReduce4H(v0, v1, v2, v3);
    #elif defined(A_HLSL)
    // requires SM6.0
    AU1 quad = WaveGetLaneIndex() &  (~0x3);
    AH4 v0 = v;
    AH4 v1 = WaveReadLaneAt(v, quad | 1);
    AH4 v2 = WaveReadLaneAt(v, quad | 2);
    AH4 v3 = WaveReadLaneAt(v, quad | 3);
    return DSReduce4H(v0, v1, v2, v3);
    /*
    // if SM6.0 is not available, you can use the AMD shader intrinsics
    // works for DX11
    AH4 v0 = v;
    AH4 v1;
    v1.x = AmdExtD3DShaderIntrinsics_SwizzleF(v.x, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    v1.y = AmdExtD3DShaderIntrinsics_SwizzleF(v.y, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    v1.z = AmdExtD3DShaderIntrinsics_SwizzleF(v.z, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    v1.w = AmdExtD3DShaderIntrinsics_SwizzleF(v.w, AmdExtD3DShaderIntrinsicsSwizzle_SwapX1);
    AH4 v2;
    v2.x = AmdExtD3DShaderIntrinsics_SwizzleF(v.x, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    v2.y = AmdExtD3DShaderIntrinsics_SwizzleF(v.y, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    v2.z = AmdExtD3DShaderIntrinsics_SwizzleF(v.z, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    v2.w = AmdExtD3DShaderIntrinsics_SwizzleF(v.w, AmdExtD3DShaderIntrinsicsSwizzle_SwapX2);
    AH4 v3;
    v3.x = AmdExtD3DShaderIntrinsics_SwizzleF(v.x, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    v3.y = AmdExtD3DShaderIntrinsics_SwizzleF(v.y, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    v3.z = AmdExtD3DShaderIntrinsics_SwizzleF(v.z, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    v3.w = AmdExtD3DShaderIntrinsics_SwizzleF(v.w, AmdExtD3DShaderIntrinsicsSwizzle_ReverseX4);
    return DSReduce4H(v0, v1, v2, v3);
    */
    #endif
    return AH4(0.0, 0.0, 0.0, 0.0);

}

AH4 ReduceIntermediateH(AU2 i0, AU2 i1, AU2 i2, AU2 i3)
{
    AH4 v0 = DSLoadIntermediateH(i0.x, i0.y);
    AH4 v1 = DSLoadIntermediateH(i1.x, i1.y);
    AH4 v2 = DSLoadIntermediateH(i2.x, i2.y);
    AH4 v3 = DSLoadIntermediateH(i3.x, i3.y);
    return DSReduce4H(v0, v1, v2, v3);
}

AH4 ReduceLoad4H(AU2 i0, AU2 i1, AU2 i2, AU2 i3)
{
    AH4 v0 = DSLoadH(ASU2(i0));
    AH4 v1 = DSLoadH(ASU2(i1));
    AH4 v2 = DSLoadH(ASU2(i2));
    AH4 v3 = DSLoadH(ASU2(i3));
    return DSReduce4H(v0, v1, v2, v3);
}

AH4 ReduceLoad4H(AU2 base)
{
    return ReduceLoad4H(
        AU2(base + AU2(0, 0)),
        AU2(base + AU2(0, 1)), 
        AU2(base + AU2(1, 0)), 
        AU2(base + AU2(1, 1)));
}

AH4 ReduceLoadSourceImage4H(AU2 i0, AU2 i1, AU2 i2, AU2 i3)
{
    AH4 v0 = DSLoadSourceImageH(ASU2(i0));
    AH4 v1 = DSLoadSourceImageH(ASU2(i1));
    AH4 v2 = DSLoadSourceImageH(ASU2(i2));
    AH4 v3 = DSLoadSourceImageH(ASU2(i3));
    return DSReduce4H(v0, v1, v2, v3);
}

AH4 ReduceLoadSourceImage4H(AU2 base)
{
     // [SAMPLER] use:
    return DSLoadSourceImageH(ASU2(base));
    // if no sampler use:
    /*return ReduceLoadSourceImage4H(
        AU2(base + AU2(0, 0)),
        AU2(base + AU2(0, 1)), 
        AU2(base + AU2(1, 0)), 
        AU2(base + AU2(1, 1)));*/

}

void DownsampleMips_0_1_IntrinsicsH(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mips)
{
    AH4 v[4];

    ASU2 tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2);
    ASU2 pix = ASU2(workGroupID.xy * 32) + ASU2(x, y);
    v[0] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[0], 0);

    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y);
    v[1] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[1], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x, y + 16);
    v[2] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[2], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y + 16);
    v[3] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[3], 0);

    if (mips <= 1)
        return;

    v[0] = ReduceQuadH(v[0]);
    v[1] = ReduceQuadH(v[1]);
    v[2] = ReduceQuadH(v[2]);
    v[3] = ReduceQuadH(v[3]);

    if ((localInvocationIndex % 4) == 0)
    {
        DSStoreH(ASU2(workGroupID.xy * 16) + ASU2(x/2, y/2), v[0], 1);
        DSStoreIntermediateH(x/2, y/2, v[0]);

        DSStoreH(ASU2(workGroupID.xy * 16) + ASU2(x/2 + 8, y/2), v[1], 1);
        DSStoreIntermediateH(x/2 + 8, y/2, v[1]);

        DSStoreH(ASU2(workGroupID.xy * 16) + ASU2(x/2, y/2 + 8), v[2], 1);
        DSStoreIntermediateH(x/2, y/2 + 8, v[2]);

        DSStoreH(ASU2(workGroupID.xy * 16) + ASU2(x/2 + 8, y/2 + 8), v[3], 1);
        DSStoreIntermediateH(x/2 + 8, y/2 + 8, v[3]);
    }
}

void DownsampleMips_0_1_LDSH(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mips) 
{
    AH4 v[4];

    ASU2 tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2);
    ASU2 pix = ASU2(workGroupID.xy * 32) + ASU2(x, y);
    v[0] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[0], 0);

    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y);
    v[1] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[1], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x, y + 16);
    v[2] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[2], 0);
    
    tex = ASU2(workGroupID.xy * 64) + ASU2(x * 2 + 32, y * 2 + 32);
    pix = ASU2(workGroupID.xy * 32) + ASU2(x + 16, y + 16);
    v[3] = ReduceLoadSourceImage4H(tex);
    DSStoreH(pix, v[3], 0);

    if (mips <= 1)
        return;

    for (int i = 0; i < 4; i++)
    {
        DSStoreIntermediateH(x, y, v[i]);
        DSWorkgroupShuffleBarrier();
        if (localInvocationIndex < 64)
        {
            v[i] = ReduceIntermediateH(
                AU2(x * 2 + 0, y * 2 + 0),
                AU2(x * 2 + 1, y * 2 + 0),
                AU2(x * 2 + 0, y * 2 + 1),
                AU2(x * 2 + 1, y * 2 + 1)
            );
            DSStoreH(ASU2(workGroupID.xy * 16) + ASU2(x + (i % 2) * 8, y + (i / 2) * 8), v[i], 1);
        }
        DSWorkgroupShuffleBarrier();
    }

    if (localInvocationIndex < 64)
    {
        DSStoreIntermediateH(x + 0, y + 0, v[0]);
        DSStoreIntermediateH(x + 8, y + 0, v[1]);
        DSStoreIntermediateH(x + 0, y + 8, v[2]);
        DSStoreIntermediateH(x + 8, y + 8, v[3]);
    }
}

void DownsampleMips_0_1H(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mips) 
{
#ifdef SPD_NO_WAVE_OPERATIONS
    DownsampleMips_0_1_LDSH(x, y, workGroupID, localInvocationIndex, mips);
#else
    DownsampleMips_0_1_IntrinsicsH(x, y, workGroupID, localInvocationIndex, mips);
#endif
}


void DownsampleMip_2H(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 64)
    {
        AH4 v = ReduceIntermediateH(
            AU2(x * 2 + 0 + 0, y * 2 + 0),
            AU2(x * 2 + 0 + 1, y * 2 + 0),
            AU2(x * 2 + 0 + 0, y * 2 + 1),
            AU2(x * 2 + 0 + 1, y * 2 + 1)
        );
        DSStoreH(ASU2(workGroupID.xy * 8) + ASU2(x, y), v, mip);
        // store to LDS, try to reduce bank conflicts
        // x 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0 x
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // x 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0
        // ...
        // x 0 x 0 x 0 x 0 x 0 x 0 x 0 x 0
        DSStoreIntermediateH(x * 2 + y % 2, y * 2, v);
    }
#else
    AH4 v = DSLoadIntermediateH(x, y);
    v = ReduceQuadH(v);
    // quad index 0 stores result
    if (localInvocationIndex % 4 == 0)
    {   
        DSStoreH(ASU2(workGroupID.xy * 8) + ASU2(x/2, y/2), v, mip);
        DSStoreIntermediateH(x + (y/2) % 2, y, v);
    }
#endif
}

void DownsampleMip_3H(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 16)
    {
        // x 0 x 0
        // 0 0 0 0
        // 0 x 0 x
        // 0 0 0 0
        AH4 v = ReduceIntermediateH(
            AU2(x * 4 + 0 + 0, y * 4 + 0),
            AU2(x * 4 + 2 + 0, y * 4 + 0),
            AU2(x * 4 + 0 + 1, y * 4 + 2),
            AU2(x * 4 + 2 + 1, y * 4 + 2)
        );
        DSStoreH(ASU2(workGroupID.xy * 4) + ASU2(x, y), v, mip);
        // store to LDS
        // x 0 0 0 x 0 0 0 x 0 0 0 x 0 0 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
        // 0 x 0 0 0 x 0 0 0 x 0 0 0 x 0 0
        // ...
        // 0 0 x 0 0 0 x 0 0 0 x 0 0 0 x 0
        // ...
        // 0 0 0 x 0 0 0 x 0 0 0 x 0 0 0 x
        // ...
        DSStoreIntermediateH(x * 4 + y, y * 4, v);
    }
#else
    if (localInvocationIndex < 64)
    {
        AH4 v = DSLoadIntermediateH(x * 2 + y % 2,y * 2);
        v = ReduceQuadH(v);
        // quad index 0 stores result
        if (localInvocationIndex % 4 == 0)
        {   
            DSStoreH(ASU2(workGroupID.xy * 4) + ASU2(x/2, y/2), v, mip);
            DSStoreIntermediateH(x * 2 + y/2, y * 2, v);
        }
    }
#endif
}

void DownsampleMip_4H(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 4)
    {
        // x 0 0 0 x 0 0 0
        // ...
        // 0 x 0 0 0 x 0 0
        AH4 v = ReduceIntermediateH(
            AU2(x * 8 + 0 + 0 + y * 2, y * 8 + 0),
            AU2(x * 8 + 4 + 0 + y * 2, y * 8 + 0),
            AU2(x * 8 + 0 + 1 + y * 2, y * 8 + 4),
            AU2(x * 8 + 4 + 1 + y * 2, y * 8 + 4)
        );
        DSStoreH(ASU2(workGroupID.xy * 2) + ASU2(x, y), v, mip);
        // store to LDS
        // x x x x 0 ...
        // 0 ...
        DSStoreIntermediateH(x + y * 2, 0, v);
    }
#else
    if (localInvocationIndex < 16)
    {
        AH4 v = DSLoadIntermediateH(x * 4 + y,y * 4);
        v = ReduceQuadH(v);
        // quad index 0 stores result
        if (localInvocationIndex % 4 == 0)
        {   
            DSStoreH(ASU2(workGroupID.xy * 2) + ASU2(x/2, y/2), v, mip);
            DSStoreIntermediateH(x / 2 + y, 0, v);
        }
    }
#endif
}

void DownsampleMip_5H(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 mip)
{
#ifdef SPD_NO_WAVE_OPERATIONS
    if (localInvocationIndex < 1)
    {
        // x x x x 0 ...
        // 0 ...
        AH4 v = ReduceIntermediateH(
            AU2(0, 0),
            AU2(1, 0),
            AU2(2, 0),
            AU2(3, 0)
        );
        DSStoreH(ASU2(workGroupID.xy), v, mip);
    }
#else
    if (localInvocationIndex < 4)
    {
        AH4 v = DSLoadIntermediateH(localInvocationIndex,0);
        v = ReduceQuadH(v);
        // quad index 0 stores result
        if (localInvocationIndex % 4 == 0)
        {   
            DSStoreH(ASU2(workGroupID.xy), v, mip);
        }
    }
#endif
}

void DownsampleMips_6_7H(AU1 x, AU1 y, AU1 mips)
{
    ASU2 tex = ASU2(x * 4 + 0, y * 4 + 0);
    ASU2 pix = ASU2(x * 2 + 0, y * 2 + 0);
    AH4 v0 = ReduceLoad4H(tex);
    DSStoreH(pix, v0, 6);

    tex = ASU2(x * 4 + 2, y * 4 + 0);
    pix = ASU2(x * 2 + 1, y * 2 + 0);
    AH4 v1 = ReduceLoad4H(tex);
    DSStoreH(pix, v1, 6);

    tex = ASU2(x * 4 + 0, y * 4 + 2);
    pix = ASU2(x * 2 + 0, y * 2 + 1);
    AH4 v2 = ReduceLoad4H(tex);
    DSStoreH(pix, v2, 6);

    tex = ASU2(x * 4 + 2, y * 4 + 2);
    pix = ASU2(x * 2 + 1, y * 2 + 1);
    AH4 v3 = ReduceLoad4H(tex);
    DSStoreH(pix, v3, 6);

    if (mips < 8) return;
    // no barrier needed, working on values only from the same thread

    AH4 v = DSReduce4H(v0, v1, v2, v3);
    DSStoreH(ASU2(x, y), v, 7);
    DSStoreIntermediateH(x, y, v);
}

void DownsampleNextFourH(AU1 x, AU1 y, AU2 workGroupID, AU1 localInvocationIndex, AU1 baseMip, AU1 mips)
{
    if (mips <= baseMip) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_2H(x, y, workGroupID, localInvocationIndex, baseMip);

    if (mips <= baseMip + 1) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_3H(x, y, workGroupID, localInvocationIndex, baseMip + 1);

    if (mips <= baseMip + 2) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_4H(x, y, workGroupID, localInvocationIndex, baseMip + 2);

    if (mips <= baseMip + 3) return;
    DSWorkgroupShuffleBarrier();
    DownsampleMip_5H(x, y, workGroupID, localInvocationIndex, baseMip + 3);
}

void DownsampleH(
    AU2 workGroupID,
    AU1 localInvocationIndex,
    AU1 mips,
    AU1 numWorkGroups
) {
    AU2 sub_xy = ARmpRed8x8(localInvocationIndex % 64);
    AU1 x = sub_xy.x + 8 * ((localInvocationIndex >> 6) % 2);
    AU1 y = sub_xy.y + 8 * ((localInvocationIndex >> 7));

    DownsampleMips_0_1H(x, y, workGroupID, localInvocationIndex, mips);

    DownsampleNextFourH(x, y, workGroupID, localInvocationIndex, 2, mips);

    if (mips < 7) return;

    if (DSExitWorkgroup(numWorkGroups, localInvocationIndex)) return;

    // After mip 6 there is only a single workgroup left that downsamples the remaining up to 64x64 texels.
    DownsampleMips_6_7H(x, y, mips);

    DownsampleNextFourH(x, y, AU2(0,0), localInvocationIndex, 8, mips);
}

#endif