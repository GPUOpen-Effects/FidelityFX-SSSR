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

#ifndef FFX_SSSR_TEMPORAL_RESOLVE
#define FFX_SSSR_TEMPORAL_RESOLVE

// In:
[[vk::binding(0, 1)]] Texture2D<FFX_SSSR_NORMALS_TEXTURE_FORMAT> g_normal               : register(t0);
[[vk::binding(1, 1)]] Texture2D<FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT> g_roughness          : register(t1);
[[vk::binding(2, 1)]] Texture2D<FFX_SSSR_NORMALS_TEXTURE_FORMAT> g_normal_history       : register(t2);
[[vk::binding(3, 1)]] Texture2D<FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT> g_roughness_history  : register(t3);
[[vk::binding(4, 1)]] Texture2D<FFX_SSSR_DEPTH_TEXTURE_FORMAT> g_depth_buffer           : register(t4);
[[vk::binding(5, 1)]] Texture2D<FFX_SSSR_MOTION_VECTOR_TEXTURE_FORMAT> g_motion_vectors : register(t5);
[[vk::binding(6, 1)]] Texture2D<float4> g_temporally_denoised_reflections_history       : register(t6); // reflection colors at the end of the temporal resolve pass of the previous frame.
[[vk::binding(7, 1)]] Texture2D<float> g_ray_lengths                                    : register(t7);
[[vk::binding(8, 1)]] Buffer<uint> g_tile_list                                          : register(t8);

// Out:
[[vk::binding(9, 1)]] RWTexture2D<float4> g_temporally_denoised_reflections             : register(u0);
[[vk::binding(10, 1)]] RWTexture2D<float4> g_spatially_denoised_reflections             : register(u1); // Technically still an input, but we have to keep it as UAV
[[vk::binding(11, 1)]] RWTexture2D<float>  g_temporal_variance                          : register(u2); 

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
/**********************************************************************
Copyright (c) [2015] [Playdead]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
********************************************************************/
float3 ClipAABB(float3 aabb_min, float3 aabb_max, float3 prev_sample)
{
    // Main idea behind clipping - it prevents clustering when neighbor color space
    // is distant from history sample

    // Here we find intersection between color vector and aabb color box

    // Note: only clips towards aabb center
    float3 aabb_center = 0.5 * (aabb_max + aabb_min);
    float3 extent_clip = 0.5 * (aabb_max - aabb_min) + 0.001;

    // Find color vector
    float3 color_vector = prev_sample - aabb_center;
    // Transform into clip space
    float3 color_vector_clip = color_vector / extent_clip;
    // Find max absolute component
    color_vector_clip = abs(color_vector_clip);
    float max_abs_unit = max(max(color_vector_clip.x, color_vector_clip.y), color_vector_clip.z);

    if (max_abs_unit > 1.0)
    {
        return aabb_center + color_vector / max_abs_unit; // clip towards color vector
    }
    else
    {
        return prev_sample; // point is inside aabb
    }
}

// Estimates spatial reflection radiance standard deviation
float3 EstimateStdDeviation(int2 did, RWTexture2D<float4> tex)
{
    float3 color_sum = 0.0;
    float3 color_sum_squared = 0.0;

    int radius = 1;
    float weight = (radius * 2.0 + 1.0) * (radius * 2.0 + 1.0);

    for (int dx = -radius; dx <= radius; dx++)
    {
        for (int dy = -radius; dy <= radius; dy++)
        {
            int2 texel_coords = did + int2(dx, dy);
            float3 value = tex.Load(texel_coords).xyz;
            color_sum += value;
            color_sum_squared += value * value;
        }
    }

    float3 color_std = (color_sum_squared - color_sum * color_sum / weight) / (weight - 1.0);
    return sqrt(max(color_std, 0.0));
}

float3 SampleRadiance(int2 texel_coords, Texture2D<float4> tex)
{
    return tex.Load(int3(texel_coords, 0)).xyz;
}

float2 GetSurfaceReprojection(int2 did, float2 uv, float2 motion_vector)
{
    // Reflector position reprojection
    float2 history_uv = uv - motion_vector;
    return history_uv;
}

float2 GetHitPositionReprojection(int2 did, float2 uv, float reflected_ray_length)
{
    float z = FfxSssrUnpackDepth(g_depth_buffer.Load(int3(did, 0)));
    float3 view_space_ray = CreateViewSpaceRay(float3(uv, z)).direction;

    // We start out with reconstructing the ray length in view space. 
    // This includes the portion from the camera to the reflecting surface as well as the portion from the surface to the hit position.
    float surface_depth = length(view_space_ray);
    float ray_length = surface_depth + reflected_ray_length;

    // We then perform a parallax correction by shooting a ray 
    // of the same length "straight through" the reflecting surface
    // and reprojecting the tip of that ray to the previous frame.
    view_space_ray /= surface_depth; // == normalize(view_space_ray)
    view_space_ray *= ray_length;
    float3 world_hit_position = mul(float4(view_space_ray, 1), g_inv_view).xyz; // This is the "fake" hit position if we would follow the ray straight through the surface.
    float3 prev_hit_position = ProjectPosition(world_hit_position, g_prev_view_proj);
    float2 history_uv = prev_hit_position.xy;
    return history_uv;
}

float SampleHistory(float2 uv, uint2 image_size, float3 normal, float roughness, float3 radiance_min, float3 radiance_max, out float3 radiance)
{
    int2 texel_coords = int2(image_size * uv);
    radiance = SampleRadiance(texel_coords, g_temporally_denoised_reflections_history);
    radiance = ClipAABB(radiance_min, radiance_max, radiance);

    float3 history_normal = LoadNormal(texel_coords, g_normal_history);
    float history_roughness = LoadRoughness(texel_coords, g_roughness_history);

    const float normal_sigma = 8.0;
    const float roughness_sigma_min = 0.01;
    const float roughness_sigma_max = 0.1;
    const float main_accumulation_factor = 0.90 + 0.1 * g_temporal_stability_factor;

    float accumulation_speed = main_accumulation_factor
        * GetEdgeStoppingNormalWeight(normal, history_normal, normal_sigma)
        * GetEdgeStoppingRoughnessWeight(roughness, history_roughness, roughness_sigma_min, roughness_sigma_max)
        * GetRoughnessAccumulationWeight(roughness)
        ;

    return saturate(accumulation_speed);
}

float ComputeTemporalVariance(float3 history_radiance, float3 radiance)
{
    // Check temporal variance. 
    float history_luminance = Luminance(history_radiance);
    float luminance = Luminance(radiance);
    return abs(history_luminance - luminance) / max(max(history_luminance, luminance), 0.00001);
}

float4 ResolveScreenspaceReflections(int2 did, float2 uv, uint2 image_size, float roughness)
{
    float3 normal = LoadNormal(did, g_normal);
    float3 radiance = g_spatially_denoised_reflections.Load(did).xyz;
    float3 radiance_history = g_temporally_denoised_reflections_history.Load(int3(did, 0)).xyz;
    float ray_length = g_ray_lengths.Load(int3(did, 0));

    // And clip it to the local neighborhood
    float2 motion_vector = FfxSssrUnpackMotionVectors(g_motion_vectors.Load(int3(did, 0)));
    float3 color_std = EstimateStdDeviation(did, g_spatially_denoised_reflections);
    color_std *= (dot(motion_vector, motion_vector) == 0) ? 8 : 2.2; // Allow more accumulation if the surface did not move.
    
    float3 radiance_min = radiance.xyz - color_std;
    float3 radiance_max = radiance + color_std;

    // Reproject point on the reflecting surface
    float2 surface_reprojection_uv = GetSurfaceReprojection(did, uv, motion_vector);

    // Reproject hit point
    float2 hit_reprojection_uv = GetHitPositionReprojection(did, uv, ray_length);

    float2 reprojection_uv;
    reprojection_uv = (roughness < 0.05) ? hit_reprojection_uv : surface_reprojection_uv;

    float3 reprojection = 0;
    float weight = 0;
    if (all(reprojection_uv > 0.0) && all(reprojection_uv < 1.0))
    {
        weight = SampleHistory(reprojection_uv, image_size, normal, roughness, radiance_min, radiance_max, reprojection);
    }

    radiance = lerp(radiance, reprojection, weight);
    float temporal_variance = ComputeTemporalVariance(radiance_history, radiance) > FFX_SSSR_TEMPORAL_VARIANCE_THRESHOLD ? 1 : 0;
    return float4(radiance.xyz, temporal_variance);
}

void Resolve(int2 did)
{
    float roughness = LoadRoughness(did, g_roughness);
    if (!IsGlossy(roughness) || IsMirrorReflection(roughness))
    {
        return;
    }

    uint2 image_size;
    g_temporally_denoised_reflections.GetDimensions(image_size.x, image_size.y);
    float2 uv = float2(did.x + 0.5, did.y + 0.5) / image_size;

    float4 resolve = ResolveScreenspaceReflections(did.xy, uv, image_size, roughness);
    g_temporally_denoised_reflections[did.xy] = float4(resolve.xyz, 1);
    g_temporal_variance[did.xy] = resolve.w;
}

[numthreads(8, 8, 1)]
void main(uint2 group_thread_id : SV_GroupThreadID, uint group_id : SV_GroupID)
{
    uint packed_base_coords = g_tile_list[group_id];
    uint2 base_coords = Unpack(packed_base_coords);
    uint2 coords = base_coords + group_thread_id;
    Resolve((int2)coords);
}

#endif // FFX_SSSR_TEMPORAL_RESOLVE