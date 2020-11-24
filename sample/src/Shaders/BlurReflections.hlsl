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

[[vk::binding(0, 1)]] Texture2D<float> g_roughness								: register(t0);
[[vk::binding(1, 1)]] Texture2D<float3> g_temporally_denoised_reflections       : register(t1); 
[[vk::binding(2, 1)]] StructuredBuffer<uint> g_tile_meta_data_mask              : register(t2);

[[vk::binding(3, 1)]] RWTexture2D<float3> g_denoised_reflections                : register(u0); 

groupshared uint g_shared_0[12][12];
groupshared uint g_shared_1[12][12];

void FFX_DNSR_Reflections_LoadFromGroupSharedMemory(int2 idx, out min16float3 radiance, out min16float roughness) {
	uint2 tmp;
	tmp.x = g_shared_0[idx.x][idx.y];
	tmp.y = g_shared_1[idx.x][idx.y];

	min16float4 min16tmp = min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y));
	radiance = min16tmp.xyz;
	roughness = min16tmp.w;
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 idx, min16float3 radiance, min16float roughness) {
	min16float4 tmp = min16float4(radiance, roughness);
	g_shared_0[idx.x][idx.y] = PackFloat16(tmp.xy);
	g_shared_1[idx.x][idx.y] = PackFloat16(tmp.zw);
}

min16float3 FFX_DNSR_Reflections_LoadRadianceFP16(int2 pixel_coordinate) {
	return g_temporally_denoised_reflections.Load(int3(pixel_coordinate, 0)).xyz;
}

min16float FFX_DNSR_Reflections_LoadRoughnessFP16(int2 pixel_coordinate) {
	return (min16float) g_roughness.Load(int3(pixel_coordinate, 0));
}

void FFX_DNSR_Reflections_StoreDenoisedReflectionResult(int2 pixel_coordinate, min16float3 value) {
	g_denoised_reflections[pixel_coordinate] = value;
}

uint FFX_DNSR_Reflections_LoadTileMetaDataMask(uint index) {
	return g_tile_meta_data_mask[index];
}

#include "ffx_denoiser_reflections_blur.h"

[numthreads(8, 8, 1)]
void main(int2 dispatch_thread_id : SV_DispatchThreadID, int2 group_thread_id : SV_GroupThreadID) {
	uint2 screen_dimensions;
	g_temporally_denoised_reflections.GetDimensions(screen_dimensions.x, screen_dimensions.y);
	FFX_DNSR_Reflections_Blur(dispatch_thread_id, group_thread_id, screen_dimensions);
}