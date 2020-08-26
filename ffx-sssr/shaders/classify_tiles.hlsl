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

#ifndef FFX_SSSR_CLASSIFY_TILES
#define FFX_SSSR_CLASSIFY_TILES

// In:
[[vk::binding(0, 1)]] Texture2D<FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT> g_roughness            : register(t0);

// Out:
[[vk::binding(1, 1)]] RWBuffer<uint> g_tile_list                                          : register(u0);
[[vk::binding(2, 1)]] RWBuffer<uint> g_ray_list                                           : register(u1);
[[vk::binding(3, 1)]] globallycoherent RWBuffer<uint> g_tile_counter                      : register(u2);
[[vk::binding(4, 1)]] globallycoherent RWBuffer<uint> g_ray_counter                       : register(u3);
[[vk::binding(5, 1)]] RWTexture2D<float4> g_temporally_denoised_reflections               : register(u4);
[[vk::binding(6, 1)]] RWTexture2D<float4> g_temporally_denoised_reflections_history       : register(u5);
[[vk::binding(7, 1)]] RWTexture2D<float> g_ray_lengths                                    : register(u6);
[[vk::binding(8, 1)]] RWTexture2D<float> g_temporal_variance                              : register(u7);
[[vk::binding(9, 1)]] RWTexture2D<float4> g_denoised_reflections                          : register(u8); 

groupshared uint g_ray_count;
groupshared uint g_ray_base_index;
groupshared uint g_denoise_count;

[numthreads(8, 8, 1)]
void main(uint2 did : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    bool is_first_lane_of_wave = WaveIsFirstLane();
    bool is_first_lane_of_threadgroup = group_index == 0;

    // First we figure out on a per thread basis if we need to shoot a reflection ray.
    uint2 screen_size;
    g_roughness.GetDimensions(screen_size.x, screen_size.y);

    // Disable offscreen pixels
    bool needs_ray = !(did.x >= screen_size.x || did.y >= screen_size.y);

    // Dont shoot a ray on very rough surfaces.
    float roughness = FfxSssrUnpackRoughness(g_roughness.Load(int3(did, 0)));
    needs_ray = needs_ray && IsGlossy(roughness);

    // Also we dont need to run the denoiser on mirror reflections.
    bool needs_denoiser = needs_ray && !IsMirrorReflection(roughness);

    // Decide which ray to keep
    bool is_base_ray = IsBaseRay(did, g_samples_per_quad);
    needs_ray = needs_ray && (!needs_denoiser || is_base_ray); // Make sure to not deactivate mirror reflection rays.

    if (g_temporal_variance_guided_tracing_enabled && needs_denoiser && !needs_ray)
    {
        float temporal_variance = g_temporal_variance.Load(did);
        bool has_temporal_variance = temporal_variance != 0.0;

        // If temporal variance is too high, we enforce a ray anyway.
        needs_ray = needs_ray || has_temporal_variance;
    }

    // Now we know for each thread if it needs to shoot a ray and wether or not a denoiser pass has to run on this pixel.
    // Thus, we need to compact the rays and append them all at once to the ray list.
    // Also, if there is at least one pixel in that tile that needs a denoiser, we have to append that tile to the tile list.

    if (is_first_lane_of_threadgroup)
    {
        g_ray_count = 0;
        g_denoise_count = 0;
    }
    GroupMemoryBarrierWithGroupSync(); // Wait for reset to finish

    uint local_ray_index_in_wave = WavePrefixCountBits(needs_ray);
    uint wave_ray_count = WaveActiveCountBits(needs_ray);
    bool wave_needs_denoiser = WaveActiveAnyTrue(needs_denoiser);
    uint wave_count = wave_needs_denoiser ? 1 : 0;

    uint local_ray_index_of_wave;
    if (is_first_lane_of_wave)
    {
        InterlockedAdd(g_ray_count, wave_ray_count, local_ray_index_of_wave);
        InterlockedAdd(g_denoise_count, wave_count);
    }
    local_ray_index_of_wave = WaveReadLaneFirst(local_ray_index_of_wave);

    GroupMemoryBarrierWithGroupSync(); // Wait for ray compaction to finish

    if (is_first_lane_of_threadgroup)
    {
        bool must_denoise = g_denoise_count > 0;
        uint denoise_count = must_denoise ? 1 : 0;
        uint ray_count = g_ray_count;

        uint tile_index;
        uint ray_base_index = 0;

        InterlockedAdd(g_tile_counter[0], denoise_count, tile_index);
        InterlockedAdd(g_ray_counter[0], ray_count, ray_base_index);

        int cleaned_index = must_denoise ? tile_index : -1;
        g_tile_list[cleaned_index] = Pack(did); // Write out pixel coords of upper left pixel
        g_ray_base_index = ray_base_index;
    }
    GroupMemoryBarrierWithGroupSync(); // Wait for ray base index to become available

    int2 target = needs_ray ? int2(-1, -1) : did;
    int ray_index = needs_ray ? g_ray_base_index + local_ray_index_of_wave + local_ray_index_in_wave : -1;

    g_ray_list[ray_index] = Pack(did); // Write out pixel to trace
    // Clear intersection targets as there wont be any ray that overwrites them
    g_temporally_denoised_reflections[target] = 0;
    g_ray_lengths[target] = 0;
    g_temporal_variance[did] = needs_ray ? (1 - g_skip_denoiser) : 0; // Re-purpose g_temporal_variance to hold the information for the spatial pass if a ray has been shot. Always write 0 if no denoiser is running.
}

#endif // FFX_SSSR_CLASSIFY_TILES