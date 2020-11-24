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

[[vk::binding(0, 1)]] Texture2D<float4> g_normal                                        : register(t0); 
[[vk::binding(1, 1)]] Texture2D<float> g_roughness                                      : register(t1); 
[[vk::binding(2, 1)]] Texture2D<float4> g_normal_history                                : register(t2); 
[[vk::binding(3, 1)]] Texture2D<float> g_roughness_history                              : register(t3); 
[[vk::binding(4, 1)]] Texture2D<float> g_depth_buffer                                   : register(t4); 
[[vk::binding(5, 1)]] Texture2D<float2> g_motion_vectors                                : register(t5); 
[[vk::binding(6, 1)]] Texture2D<float3> g_temporally_denoised_reflections_history       : register(t6);
[[vk::binding(7, 1)]] Texture2D<float> g_ray_lengths                                    : register(t7); 
[[vk::binding(8, 1)]] Texture2D<float3> g_spatially_denoised_reflections                : register(t8);
[[vk::binding(9, 1)]] StructuredBuffer<uint> g_tile_meta_data_mask                      : register(t9);

[[vk::binding(10, 1)]] RWTexture2D<float3> g_temporally_denoised_reflections            : register(u0);
[[vk::binding(11, 1)]] RWStructuredBuffer<uint> g_temporal_variance_mask                : register(u1);

float FFX_DNSR_Reflections_LoadRayLength(int2 pixel_coordinate) {
    return g_ray_lengths.Load(int3(pixel_coordinate, 0));
}

float2 FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate) {
    return g_motion_vectors.Load(int3(pixel_coordinate, 0)).xy * float2(0.5, -0.5);
}

float3 FFX_DNSR_Reflections_LoadNormal(int2 pixel_coordinate) {
    return 2 * g_normal.Load(int3(pixel_coordinate, 0)).xyz - 1;
}

float3 FFX_DNSR_Reflections_LoadNormalHistory(int2 pixel_coordinate) {
    return 2 * g_normal_history.Load(int3(pixel_coordinate, 0)).xyz - 1;
}

float FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) {
    return g_roughness.Load(int3(pixel_coordinate, 0));
}

float FFX_DNSR_Reflections_LoadRoughnessHistory(int2 pixel_coordinate) {
    return g_roughness_history.Load(int3(pixel_coordinate, 0));
}

float3 FFX_DNSR_Reflections_LoadRadianceHistory(int2 pixel_coordinate) {
    return g_temporally_denoised_reflections_history.Load(int3(pixel_coordinate, 0)).xyz;
}

float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) {
    return g_depth_buffer.Load(int3(pixel_coordinate, 0));
}

float3 FFX_DNSR_Reflections_LoadSpatiallyDenoisedReflections(int2 pixel_coordinate) {
    return g_spatially_denoised_reflections.Load(int3(pixel_coordinate, 0)).xyz;
}

uint FFX_DNSR_Reflections_LoadTileMetaDataMask(uint index) {
    return g_tile_meta_data_mask[index];
}

void FFX_DNSR_Reflections_StoreTemporallyDenoisedReflections(int2 pixel_coordinate, float3 value) {
    g_temporally_denoised_reflections[pixel_coordinate] = value;
}

void FFX_DNSR_Reflections_StoreTemporalVarianceMask(int index, uint mask) {
    g_temporal_variance_mask[index] = mask;
}

#include "ffx_denoiser_reflections_resolve_temporal.h"

[numthreads(8, 8, 1)]
void main(int2 dispatch_thread_id : SV_DispatchThreadID, int2 group_thread_id : SV_GroupThreadID) {
    uint2 image_size;
    g_temporally_denoised_reflections.GetDimensions(image_size.x, image_size.y);
    FFX_DNSR_Reflections_ResolveTemporal(dispatch_thread_id, group_thread_id, image_size, g_temporal_stability_factor, g_temporal_variance_threshold);
}