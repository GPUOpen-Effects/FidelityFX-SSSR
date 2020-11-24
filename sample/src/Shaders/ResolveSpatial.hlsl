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

#include "Common.hlsl"

[[vk::binding(0, 1)]] Texture2D<float> g_depth_buffer                           : register(t0);
[[vk::binding(1, 1)]] Texture2D<float4> g_normal                                : register(t1);
[[vk::binding(2, 1)]] Texture2D<float> g_roughness                              : register(t2);
[[vk::binding(3, 1)]] Texture2D<min16float3> g_intersection_result              : register(t3);
[[vk::binding(4, 1)]] StructuredBuffer<uint> g_tile_meta_data_mask              : register(t4);

[[vk::binding(5, 1)]] RWTexture2D<float3> g_spatially_denoised_reflections      : register(u0);

groupshared uint g_shared_0[16][16];
groupshared uint g_shared_1[16][16];
groupshared uint g_shared_2[16][16];
groupshared uint g_shared_3[16][16];
groupshared float g_shared_depth[16][16];

min16float3 FFX_DNSR_Reflections_LoadRadianceFromGroupSharedMemory(int2 idx) {
    uint2 tmp;
    tmp.x = g_shared_0[idx.y][idx.x];
    tmp.y = g_shared_1[idx.y][idx.x];
    return min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y)).xyz;
}

min16float3 FFX_DNSR_Reflections_LoadNormalFromGroupSharedMemory(int2 idx) {
    uint2 tmp;
    tmp.x = g_shared_2[idx.y][idx.x];
    tmp.y = g_shared_3[idx.y][idx.x];
    return min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y)).xyz;
}

float FFX_DNSR_Reflections_LoadDepthFromGroupSharedMemory(int2 idx) {
    return g_shared_depth[idx.y][idx.x];
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 idx, min16float3 radiance, min16float3 normal, float depth) {
    g_shared_0[idx.y][idx.x] = PackFloat16(radiance.xy);
    g_shared_1[idx.y][idx.x] = PackFloat16(min16float2(radiance.z, 0));
    g_shared_2[idx.y][idx.x] = PackFloat16(normal.xy);
    g_shared_3[idx.y][idx.x] = PackFloat16(min16float2(normal.z, 0));
    g_shared_depth[idx.y][idx.x] = depth;
}

float FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) {
    return g_roughness.Load(int3(pixel_coordinate, 0));
}

min16float3 FFX_DNSR_Reflections_LoadRadianceFP16(int2 pixel_coordinate) {
    return g_intersection_result.Load(int3(pixel_coordinate, 0)).xyz;
}

min16float3 FFX_DNSR_Reflections_LoadNormalFP16(int2 pixel_coordinate) {
    return (min16float3) (2 * g_normal.Load(int3(pixel_coordinate, 0)).xyz - 1);
}

float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) {
    return g_depth_buffer.Load(int3(pixel_coordinate, 0));
}

void FFX_DNSR_Reflections_StoreSpatiallyDenoisedReflections(int2 pixel_coordinate, min16float3 value) {
    g_spatially_denoised_reflections[pixel_coordinate] = value;
}

uint FFX_DNSR_Reflections_LoadTileMetaDataMask(uint index) {
    return g_tile_meta_data_mask[index];
}

#include "ffx_denoiser_reflections_resolve_spatial.h"

[numthreads(8, 8, 1)]
void main(uint group_index : SV_GroupIndex, uint2 group_id : SV_GroupID) {

    uint2 screen_dimensions;
    g_depth_buffer.GetDimensions(screen_dimensions.x, screen_dimensions.y);

    uint2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    uint2 dispatch_thread_id = group_id * 8 + group_thread_id;
    FFX_DNSR_Reflections_ResolveSpatial((int2)dispatch_thread_id, (int2)group_thread_id, g_samples_per_quad, screen_dimensions);
}