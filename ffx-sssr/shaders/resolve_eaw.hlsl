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

#ifndef FFX_SSSR_EAW_RESOLVE
#define FFX_SSSR_EAW_RESOLVE

Texture2D<FFX_SSSR_NORMALS_TEXTURE_FORMAT>      g_normal            : register(t0);
Texture2D<FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT>    g_roughness         : register(t1);
Texture2D<FFX_SSSR_DEPTH_TEXTURE_FORMAT>        g_depth_buffer      : register(t2);

SamplerState g_linear_sampler                                       : register(s0);

RWTexture2D<float4> g_temporally_denoised_reflections               : register(u0);
RWTexture2D<float4> g_denoised_reflections                          : register(u1); // Will hold the reflection colors at the end of the resolve pass. 
RWBuffer<uint>      g_tile_list                                     : register(u2);

groupshared uint g_shared_0[12][12];
groupshared uint g_shared_1[12][12];

void LoadFromGroupSharedMemory(int2 idx, out min16float3 radiance, out min16float roughness)
{
    uint2 tmp;
    tmp.x = g_shared_0[idx.x][idx.y];
    tmp.y = g_shared_1[idx.x][idx.y];

    min16float4 min16tmp = min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y));
    radiance = min16tmp.xyz;
    roughness = min16tmp.w;
}

void StoreInGroupSharedMemory(int2 idx, min16float3 radiance, min16float roughness)
{
    min16float4 tmp = min16float4(radiance, roughness);
    g_shared_0[idx.x][idx.y] = PackFloat16(tmp.xy);
    g_shared_1[idx.x][idx.y] = PackFloat16(tmp.zw);
}

min16float3 LoadRadiance(int2 idx)
{
    return g_temporally_denoised_reflections.Load(int3(idx, 0)).xyz;
}

min16float LoadRoughnessValue(int2 idx)
{
    return FfxSssrUnpackRoughness(g_roughness.Load(int3(idx, 0)));
}

min16float4 ResolveScreenspaceReflections(int2 gtid, min16float center_roughness)
{
    const min16float roughness_sigma_min = 0.001;
    const min16float roughness_sigma_max = 0.01;

    min16float3 sum = 0.0;
    min16float total_weight = 0.0;

    const int radius = 2;
    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            int2 texel_coords = gtid + int2(dx, dy);

            min16float3 radiance;
            min16float roughness;
            LoadFromGroupSharedMemory(texel_coords, radiance, roughness);

            min16float weight = GetEdgeStoppingRoughnessWeightFP16(center_roughness, roughness, roughness_sigma_min, roughness_sigma_max);
            sum += weight * radiance;
            total_weight += weight;
        }
    }

    sum /= max(total_weight, 0.0001);
    return min16float4(sum, 1);
}

void LoadWithOffset(int2 did, int2 offset, out min16float3 radiance, out min16float roughness)
{
    did += offset;
    radiance = LoadRadiance(did);
    roughness = LoadRoughnessValue(did);
}

void StoreWithOffset(int2 gtid, int2 offset, min16float3 radiance, min16float roughness)
{
    gtid += offset;
    StoreInGroupSharedMemory(gtid, radiance, roughness);
}

void InitializeGroupSharedMemory(int2 did, int2 gtid)
{
    int2 offset_0 = 0;
    if (gtid.x < 4)
    {
        offset_0 = int2(8, 0);
    }
    else if (gtid.y >= 4)
    {
        offset_0 = int2(4, 4);
    }
    else
    {
        offset_0 = -gtid; // map all threads to the same memory location to guarantee cache hits.
    }

    int2 offset_1 = 0;
    if (gtid.y < 4)
    {
        offset_1 = int2(0, 8);
    }
    else
    {
        offset_1 = -gtid; // map all threads to the same memory location to guarantee cache hits.
    }

    min16float3 radiance_0;
    min16float roughness_0;

    min16float3 radiance_1;
    min16float roughness_1;

    min16float3 radiance_2;
    min16float roughness_2;

    /// XXA
    /// XXA
    /// BBC

    did -= 2;
    LoadWithOffset(did, int2(0, 0), radiance_0, roughness_0); // X
    LoadWithOffset(did, offset_0, radiance_1, roughness_1); // A & C
    LoadWithOffset(did, offset_1, radiance_2, roughness_2); // B
    
    StoreWithOffset(gtid, int2(0, 0), radiance_0, roughness_0); // X
    if (gtid.x < 4 || gtid.y >= 4)
    {
        StoreWithOffset(gtid, offset_0, radiance_1, roughness_1); // A & C
    }
    if (gtid.y < 4)
    {
        StoreWithOffset(gtid, offset_1, radiance_2, roughness_2); // B
    }
}

void Resolve(int2 did, int2 gtid)
{
    InitializeGroupSharedMemory(did, gtid);
    GroupMemoryBarrierWithGroupSync();

    gtid += 2; // Center threads in groupshared memory

    min16float3 center_radiance;
    min16float center_roughness;
    LoadFromGroupSharedMemory(gtid, center_radiance, center_roughness);

    if (!IsGlossy(center_roughness) || IsMirrorReflection(center_roughness))
    {
        return;
    }

    g_denoised_reflections[did.xy] = ResolveScreenspaceReflections(gtid, center_roughness);
}

[numthreads(8, 8, 1)]
void main(uint2 group_thread_id : SV_GroupThreadID, uint group_id : SV_GroupID)
{
    uint packed_base_coords = g_tile_list[group_id];
    uint2 base_coords = Unpack(packed_base_coords);
    uint2 coords = base_coords + group_thread_id;
    Resolve((int2)coords, (int2)group_thread_id);
}

#endif // FFX_SSSR_EAW_RESOLVE