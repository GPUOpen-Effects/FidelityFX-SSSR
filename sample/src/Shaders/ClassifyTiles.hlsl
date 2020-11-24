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

[[vk::binding(0, 1)]] Texture2D<float4> g_roughness                         : register(t0);
[[vk::binding(1, 1)]] StructuredBuffer<uint> g_temporal_variance_mask       : register(t1);

[[vk::binding(2, 1)]] RWBuffer<uint> g_ray_list                             : register(u0);
[[vk::binding(3, 1)]] globallycoherent RWBuffer<uint> g_ray_counter         : register(u1);
[[vk::binding(4, 1)]] RWTexture2D<float3> g_intersection_results            : register(u2);
[[vk::binding(5, 1)]] RWStructuredBuffer<uint> g_tile_meta_data_mask        : register(u3);
[[vk::binding(6, 1)]] RWTexture2D<float> g_extracted_roughness              : register(u4);

uint FFX_DNSR_Reflections_LoadTemporalVarianceMask(uint index) {
    return g_temporal_variance_mask[index];
}

void FFX_DNSR_Reflections_IncrementRayCounter(uint value, out uint original_value) {
    InterlockedAdd(g_ray_counter[0], value, original_value);
}

void FFX_DNSR_Reflections_StoreRay(int index, uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal) {
    g_ray_list[index] = PackRayCoords(ray_coord, copy_horizontal, copy_vertical, copy_diagonal); // Store out pixel to trace
}

void FFX_DNSR_Reflections_StoreTileMetaDataMask(uint index, uint mask) {
    g_tile_meta_data_mask[index] = mask;
}

#include "ffx_denoiser_reflections_classify_tiles.h"

[numthreads(8, 8, 1)]
void main(uint2 group_id : SV_GroupID, uint group_index : SV_GroupIndex) {
    uint2 screen_size;
    g_roughness.GetDimensions(screen_size.x, screen_size.y);

    uint2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index); // Remap lanes to ensure four neighboring lanes are arranged in a quad pattern
    uint2 dispatch_thread_id = group_id * 8 + group_thread_id;

    float roughness = g_roughness.Load(int3(dispatch_thread_id, 0)).w;

    FFX_DNSR_Reflections_ClassifyTiles(dispatch_thread_id, group_thread_id, roughness, screen_size, g_samples_per_quad, g_temporal_variance_guided_tracing_enabled);

    // Clear intersection results as there wont be any ray that overwrites them
    g_intersection_results[dispatch_thread_id] = 0;

    // Extract only the channel containing the roughness to avoid loading all 4 channels in the follow up passes.
    g_extracted_roughness[dispatch_thread_id] = roughness;
}