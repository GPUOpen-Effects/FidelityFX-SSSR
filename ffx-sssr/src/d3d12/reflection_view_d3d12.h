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
#pragma once

#include <deque>
#include <vector>
#include <d3d12.h>

#include "macros.h"
#include "matrix4.h"
#include "ffx_sssr.h"
#include "descriptor_heap_d3d12.h"

namespace ffx_sssr
{
    class Context;
    class ReflectionView;

    /**
        The ReflectionViewD3D12 class encapsulates the data required for resolving an individual reflection view.
    */
    class ReflectionViewD3D12
    {
        FFX_SSSR_NON_COPYABLE(ReflectionViewD3D12);

    public:

        /**
            The available timestamp queries.
        */
        enum TimestampQuery
        {
            kTimestampQuery_Init,
            kTimestampQuery_TileClassification,
            kTimestampQuery_Intersection,
            kTimestampQuery_Denoising,

            kTimestampQuery_Count
        };

        /**
            The type definition for an array of timestamp queries.
        */
        using TimestampQueries = std::vector<TimestampQuery>;

        ReflectionViewD3D12();
        ~ReflectionViewD3D12();

        ReflectionViewD3D12(ReflectionViewD3D12&& other) noexcept;
        ReflectionViewD3D12& operator =(ReflectionViewD3D12&& other) noexcept;

        void Create(Context& context, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);
        void Destroy();

        void CreateDescriptorHeaps(Context& context);

        std::uint32_t GetTimestampQueryIndex() const;

        void Resolve(Context& context, ReflectionView const& reflection_view, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info);

        // The width of the reflection view (in texels).
        std::uint32_t width_;
        // The height of the reflection view (in texels).
        std::uint32_t height_;
        // The reflection view creation flags.
        FfxSssrCreateReflectionViewFlags flags_;

        // The descriptor heap for CBVs, SRVs, and UAVs.
        DescriptorHeapD3D12* descriptor_heap_cbv_srv_uav_;

        // The descriptor heap for samplers.
        DescriptorHeapD3D12* descriptor_heap_samplers_;

        // Single heap containing all resources.
        ID3D12Heap * resource_heap_;

        // Containing all tiles that need at least one ray.
        ID3D12Resource * tile_list_;
        ID3D12Resource * tile_counter_;
        // Containing all rays that need to be traced.
        ID3D12Resource * ray_list_;
        ID3D12Resource * ray_counter_;
        // Indirect arguments for intersection pass.
        ID3D12Resource * intersection_pass_indirect_args_;
        // Indirect arguments for denoiser pass.
        ID3D12Resource * denoiser_pass_indirect_args_;
        // Intermediate result of the temporal denoising pass - double buffered to keep history and aliases the intersection result.
        ID3D12Resource * temporal_denoiser_result_[2];
        // Holds the length of each reflection ray - used for temporal reprojection.
        ID3D12Resource * ray_lengths_;
        // Holds the temporal variance of the last two frames.
        ID3D12Resource * temporal_variance_;

        // The number of GPU ticks spent in the tile classification pass.
        std::uint64_t tile_classification_elapsed_time_;
        // The number of GPU ticks spent in depth buffer intersection.
        std::uint64_t intersection_elapsed_time_;
        // The number of GPU ticks spent denoising.
        std::uint64_t denoising_elapsed_time_;
        // The query heap for the recorded timestamps.
        ID3D12QueryHeap * timestamp_query_heap_;
        // The buffer for reading the timestamp queries.
        ID3D12Resource * timestamp_query_buffer_;
        // The array of timestamp that were queried.
        std::vector<TimestampQueries> timestamp_queries_;
        // The index of the active set of timestamp queries.
        std::uint32_t timestamp_queries_index_;

        // Format of the resolved scene.
        DXGI_FORMAT scene_format_;

        // The descriptor tables. One per shader pass per frame. 
        // Even with more than 2 frames in flight we only swap between the last two
        // as we keep only one frame of history.

        // Descriptor tables of the tile classification pass.
        DescriptorD3D12 tile_classification_descriptor_table_[2];
        // Descriptor tables of the indirect arguments pass.
        DescriptorD3D12 indirect_args_descriptor_table_[2];
        // Descriptor tables of the depth buffer intersection pass.
        DescriptorD3D12 intersection_descriptor_table_[2];
        // Descriptor tables of the spatial denoising pass.
        DescriptorD3D12 spatial_denoising_descriptor_table_[2];
        // Descriptor tables of the temporal denoising pass.
        DescriptorD3D12 temporal_denoising_descriptor_table_[2];
        // Descriptor tables of the eaw denoising pass.
        DescriptorD3D12 eaw_denoising_descriptor_table_[2];
        // Descriptor tables for the environment map sampler.
        DescriptorD3D12 sampler_descriptor_table_;

        // The view projection matrix of the last frame.
        matrix4 prev_view_projection_;
    };
}
