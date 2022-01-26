/**********************************************************************
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

[[vk::binding(0)]] Texture2D<float> g_depth_buffer : register(t0);
[[vk::binding(1)]] RWTexture2D<float> g_downsampled_depth_buffer[13] : register(u0); // 12 is the maximum amount of supported mips by the downsampling lib (4096x4096). We copy the depth buffer over for simplicity.
[[vk::binding(2)]] RWBuffer<uint> g_global_atomic : register(u13); // Single atomic counter that stores the number of remaining threadgroups to process.

#define A_GPU
#define A_HLSL
#include "ffx_a.h"

groupshared float g_group_shared_depth_values[16][16];
groupshared uint g_group_shared_counter;

#define DS_FALLBACK

// Define fetch and store functions
AF4 SpdLoadSourceImage(ASU2 index) { return g_depth_buffer[index].xxxx; }
AF4 SpdLoad(ASU2 index) { return g_downsampled_depth_buffer[6][index].xxxx; } // 5 -> 6 as we store a copy of the depth buffer at index 0
void SpdStore(ASU2 pix, AF4 outValue, AU1 index) { g_downsampled_depth_buffer[index + 1][pix] = outValue.x; } // + 1 as we store a copy of the depth buffer at index 0
void SpdIncreaseAtomicCounter() { InterlockedAdd(g_global_atomic[0], 1, g_group_shared_counter); }
AU1 SpdGetAtomicCounter() { return g_group_shared_counter; }
AF4 SpdLoadIntermediate(AU1 x, AU1 y) {
	float f = g_group_shared_depth_values[x][y];
	return f.xxxx; 
}
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value) { g_group_shared_depth_values[x][y] = value.x; }
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3) { return min(min(v0, v1), min(v2,v3)); }

#include "ffx_spd.h"

uint GetThreadgroupCount(uint2 image_size){
	// Each threadgroup works on 64x64 texels
	return ((image_size.x + 63) / 64) * ((image_size.y + 63) / 64);
}

// Returns mips count of a texture with specified size
float GetMipsCount(float2 texture_size){
    float max_dim = max(texture_size.x, texture_size.y);
    return 1.0 + floor(log2(max_dim));
}

[numthreads(32, 8, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex){
	float2 depth_image_size = 0;
	g_depth_buffer.GetDimensions(depth_image_size.x, depth_image_size.y);

    // Copy most detailed level into the hierarchy and transform it.
	uint2 u_depth_image_size = uint2(depth_image_size);
	for (int i = 0; i < 2; ++i)
	{
		for (int j = 0; j < 8; ++j)
		{
			uint2 idx = uint2(2 * dispatch_thread_id.x + i, 8 * dispatch_thread_id.y + j);
			if (idx.x < u_depth_image_size.x && idx.y < u_depth_image_size.y)
			{
				g_downsampled_depth_buffer[0][idx] = g_depth_buffer[idx];
			}
		}
	}

    float2 image_size = 0;
    g_downsampled_depth_buffer[0].GetDimensions(image_size.x, image_size.y);
    float mips_count = GetMipsCount(image_size);
    uint threadgroup_count = GetThreadgroupCount(image_size);

    SpdDownsample(
		AU2(group_id.xy),
		AU1(group_index),
		AU1(mips_count),
		AU1(threadgroup_count));
}