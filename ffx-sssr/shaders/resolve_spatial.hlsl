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

#ifndef FFX_SSSR_SPATIAL_RESOLVE
#define FFX_SSSR_SPATIAL_RESOLVE

Texture2D<FFX_SSSR_DEPTH_TEXTURE_FORMAT>        g_depth_buffer  : register(t0);
Texture2D<FFX_SSSR_NORMALS_TEXTURE_FORMAT>      g_normal        : register(t1);
Texture2D<FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT>    g_roughness     : register(t2);

SamplerState g_linear_sampler                                   : register(s0);

RWTexture2D<float4> g_spatially_denoised_reflections            : register(u0);
RWTexture2D<float>  g_ray_lengths                               : register(u1);
RWTexture2D<float4> g_intersection_result                       : register(u2); // Reflection colors at the end of the intersect pass. 
RWTexture2D<float>  g_has_ray                                   : register(u3);
RWBuffer<uint>      g_tile_list                                 : register(u4);


// Only really need 16x16 but 17x17 avoids bank conflicts.
groupshared uint g_shared_0[17][17];
groupshared uint g_shared_1[17][17];
groupshared uint g_shared_2[17][17];
groupshared uint g_shared_3[17][17];
groupshared float g_shared_depth[17][17];

min16float4 LoadRadianceFromGroupSharedMemory(int2 idx)
{
    uint2 tmp;
    tmp.x = g_shared_0[idx.x][idx.y];
    tmp.y = g_shared_1[idx.x][idx.y];
    return min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y));
}

min16float3 LoadNormalFromGroupSharedMemory(int2 idx)
{
    uint2 tmp;
    tmp.x = g_shared_2[idx.x][idx.y];
    tmp.y = g_shared_3[idx.x][idx.y];
    return min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y)).xyz;
}

float LoadDepthFromGroupSharedMemory(int2 idx)
{
    return g_shared_depth[idx.x][idx.y];
}

void StoreInGroupSharedMemory(int2 idx, min16float4 radiance, min16float3 normal, float depth)
{
    g_shared_0[idx.x][idx.y] = PackFloat16(radiance.xy);
    g_shared_1[idx.x][idx.y] = PackFloat16(radiance.zw);
    g_shared_2[idx.x][idx.y] = PackFloat16(normal.xy);
    g_shared_3[idx.x][idx.y] = PackFloat16(min16float2(normal.z, 0));
    g_shared_depth[idx.x][idx.y] = depth;
}

min16float LoadRayLengthFP16(int2 idx)
{
   return g_ray_lengths.Load(idx);
}

min16float3 LoadRadianceFP16(int2 idx)
{
    return g_intersection_result.Load(int3(idx, 0)).xyz;
}

min16float3 LoadNormalFP16(int2 idx)
{
    return (min16float3) FfxSssrUnpackNormals(g_normal.Load(int3(idx, 0)));
}

float LoadDepth(int2 idx)
{
    return FfxSssrUnpackDepth(g_depth_buffer.Load(int3(idx, 0)));
}

bool LoadHasRay(int2 idx)
{
    return g_has_ray.Load(int3(idx, 0));
}

void LoadWithOffset(int2 did, int2 offset, out min16float ray_length, out min16float3 radiance, out min16float3 normal, out float depth, out bool has_ray)
{
    did += offset;
    ray_length = LoadRayLengthFP16(did);
    radiance = LoadRadianceFP16(did);
    normal = LoadNormalFP16(did);
    depth = LoadDepth(did);
    has_ray = LoadHasRay(did);
}

void StoreWithOffset(int2 gtid, int2 offset, min16float ray_length, min16float3 radiance, min16float3 normal, float depth)
{
    gtid += offset;
    StoreInGroupSharedMemory(gtid, min16float4(radiance, ray_length), normal, depth); // Pack ray length and radiance together
}

void InitializeGroupSharedMemory(int2 did, int2 gtid)
{
    const uint samples_per_quad = g_samples_per_quad;

    // First pass, load (1 + 3 + 8 + 3 + 1) = (16x16) region into shared memory.
    // That is a guard band of 3, the inner region of 8 plus one additional band to catch base pixels if we didn't shoot rays for the respective edges/corners of the loaded region.

    int2 offset_0 = 0;
    int2 offset_1 = int2(8, 0);
    int2 offset_2 = int2(0, 8);
    int2 offset_3 = int2(8, 8);

    min16float ray_length_0;
    min16float3 radiance_0;
    min16float3 normal_0;
    float depth_0;
    bool has_ray_0;

    min16float ray_length_1;
    min16float3 radiance_1;
    min16float3 normal_1;
    float depth_1;
    bool has_ray_1;

    min16float ray_length_2;
    min16float3 radiance_2;
    min16float3 normal_2;
    float depth_2;
    bool has_ray_2;

    min16float ray_length_3;
    min16float3 radiance_3;
    min16float3 normal_3;
    float depth_3;
    bool has_ray_3;

    /// XA
    /// BC

    did -= 4; // 1 + 3 => additional band + left band
    LoadWithOffset(did, offset_0, ray_length_0, radiance_0, normal_0, depth_0, has_ray_0); // X
    LoadWithOffset(did, offset_1, ray_length_1, radiance_1, normal_1, depth_1, has_ray_1); // A
    LoadWithOffset(did, offset_2, ray_length_2, radiance_2, normal_2, depth_2, has_ray_2); // B
    LoadWithOffset(did, offset_3, ray_length_3, radiance_3, normal_3, depth_3, has_ray_3); // C

    // If own values are invalid, because no ray created them, lookup the values from the neighboring threads
    const int lane_index = WaveGetLaneIndex();
    const int base_lane_index = GetBaseLane(lane_index, samples_per_quad); // As offsets are multiples of 8, we always get the same base lane index no matter the offset.
    const bool is_base_ray = base_lane_index == lane_index;

    const int lane_index_0 = (has_ray_0 || is_base_ray) ? lane_index : base_lane_index;
    const int lane_index_1 = (has_ray_1 || is_base_ray) ? lane_index : base_lane_index;
    const int lane_index_2 = (has_ray_2 || is_base_ray) ? lane_index : base_lane_index;
    const int lane_index_3 = (has_ray_3 || is_base_ray) ? lane_index : base_lane_index;

    radiance_0 = WaveReadLaneAt(radiance_0, lane_index_0);
    radiance_1 = WaveReadLaneAt(radiance_1, lane_index_1);
    radiance_2 = WaveReadLaneAt(radiance_2, lane_index_2);
    radiance_3 = WaveReadLaneAt(radiance_3, lane_index_3);

    ray_length_0 = WaveReadLaneAt(ray_length_0, lane_index_0);
    ray_length_1 = WaveReadLaneAt(ray_length_1, lane_index_1);
    ray_length_2 = WaveReadLaneAt(ray_length_2, lane_index_2);
    ray_length_3 = WaveReadLaneAt(ray_length_3, lane_index_3);

    StoreWithOffset(gtid, offset_0, ray_length_0, radiance_0, normal_0, depth_0); // X
    StoreWithOffset(gtid, offset_1, ray_length_1, radiance_1, normal_1, depth_1); // A
    StoreWithOffset(gtid, offset_2, ray_length_2, radiance_2, normal_2, depth_2); // B
    StoreWithOffset(gtid, offset_3, ray_length_3, radiance_3, normal_3, depth_3); // C
}

min16float3 ResolveScreenspaceReflections(int2 gtid, min16float3 center_radiance, min16float3 center_normal, float center_depth)
{
    float3 accumulated_radiance = center_radiance;
    float accumulated_weight = 1;

    const float normal_sigma = 64.0;
    const float depth_sigma = 0.02;

    // First 15 numbers of Halton(2,3) streteched to [-3,3]
    const int2 reuse_offsets[] = {
        0, 1,
        -2, 1,
        2, -3,
        -3, 0,
        1, 2,
        -1, -2,
        3, 0,
        -3, 3,
        0, -3,
        -1, -1,
        2, 1,
        -2, -2,
        1, 0,
        0, 2,
        3, -1
    };
    const uint sample_count = 15;

    for (int i = 0; i < sample_count; ++i)
    {
        int2 new_idx = gtid + reuse_offsets[i];
        min16float3 normal = LoadNormalFromGroupSharedMemory(new_idx);
        float depth = LoadDepthFromGroupSharedMemory(new_idx);
        min16float4 radiance = LoadRadianceFromGroupSharedMemory(new_idx);
        float weight = 1
            * GetEdgeStoppingNormalWeight((float3)center_normal, (float3)normal, normal_sigma)
            * Gaussian(center_depth, depth, depth_sigma)
            ;

        // Accumulate all contributions.
        accumulated_weight += weight;
        accumulated_radiance += weight * radiance.xyz;
    }

    accumulated_radiance /= max(accumulated_weight, 0.00001);
    return accumulated_radiance;
}

void Resolve(int2 did, int2 gtid)
{
    float center_roughness = LoadRoughness(did, g_roughness);
    InitializeGroupSharedMemory(did, gtid);
    GroupMemoryBarrierWithGroupSync();

    if (!IsGlossy(center_roughness) || IsMirrorReflection(center_roughness))
    {
        return;
    }

    gtid += 4; // Center threads in groupshared memory

    min16float4 center_radiance = LoadRadianceFromGroupSharedMemory(gtid);
    min16float3 center_normal = LoadNormalFromGroupSharedMemory(gtid);
    float center_depth = LoadDepthFromGroupSharedMemory(gtid);
    g_spatially_denoised_reflections[did.xy] = min16float4(ResolveScreenspaceReflections(gtid, center_radiance.xyz, center_normal, center_depth), 1);
    g_ray_lengths[did.xy] = center_radiance.w; // ray_length
}

[numthreads(64, 1, 1)]
void main(uint group_thread_id_linear : SV_GroupThreadID, uint group_id : SV_GroupID)
{
    uint packed_base_coords = g_tile_list[group_id];
    uint2 base_coords = Unpack(packed_base_coords);
    uint2 group_thread_id_2d = RemapLane8x8(group_thread_id_linear);
    uint2 coords = base_coords + group_thread_id_2d;
    Resolve((int2)coords, (int2)group_thread_id_2d);
}

#endif // FFX_SSSR_SPATIAL_RESOLVE