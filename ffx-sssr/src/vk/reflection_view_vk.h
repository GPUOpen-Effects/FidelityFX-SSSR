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
#include <vulkan/vulkan.h>

#include "macros.h"
#include "matrix4.h"
#include "ffx_sssr.h"
#include "buffer_vk.h"
#include "image_vk.h"

namespace ffx_sssr
{
    class Context;
    class ReflectionView;

    /**
        The ReflectionViewVK class encapsulates the data required for resolving an individual reflection view.
    */
    class ReflectionViewVK
    {
        FFX_SSSR_NON_COPYABLE(ReflectionViewVK);

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

        ReflectionViewVK();
        ~ReflectionViewVK();

        ReflectionViewVK(ReflectionViewVK&& other) noexcept;
        ReflectionViewVK& operator =(ReflectionViewVK&& other) noexcept;

        void Create(Context& context, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);

        uint32_t GetConservativeResourceDescriptorCount(const Context& context) const;
        void CreateDescriptorPool(const Context& context);
        void SetupInternalResources(Context& context, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);

        void AllocateDescriptorSets(Context& context);
        VkDescriptorSet AllocateDescriptorSet(Context& context, VkDescriptorSetLayout layout);
        void InitializeResourceDescriptorSets(Context& context, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);

        std::uint32_t GetTimestampQueryIndex() const;

        void Resolve(Context& context, ReflectionView const& reflection_view, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info);

        // The device that created the reflection view. Livetime handled by the context.
        VkDevice device_;
        // The physical device that created the reflection view. Livetime handled by the context.
        VkPhysicalDevice physical_device_;
        // The width of the reflection view (in texels).
        std::uint32_t width_;
        // The height of the reflection view (in texels).
        std::uint32_t height_;
        // The reflection view creation flags.
        FfxSssrCreateReflectionViewFlags flags_;

        // The descriptor pool for all resource views.
        VkDescriptorPool descriptor_pool_;

        // Linear sampler.
        VkSampler linear_sampler_;
        // Containing all tiles that need at least one ray.
        BufferVK tile_list_;
        BufferVK tile_counter_;
        // Containing all rays that need to be traced.
        BufferVK ray_list_;
        BufferVK ray_counter_;
        // Indirect arguments for intersection pass.
        BufferVK intersection_pass_indirect_args_;
        // Indirect arguments for denoiser pass.
        BufferVK denoiser_pass_indirect_args_;
        // Intermediate result of the temporal denoising pass - double buffered to keep history and aliases the intersection result.
        ImageVK temporal_denoiser_result_[2];
        // Holds the length of each reflection ray - used for temporal reprojection.
        ImageVK ray_lengths_;
        // Holds the temporal variance of the last two frames.
        ImageVK temporal_variance_;

        // The query pool containing the recorded timestamps.
        VkQueryPool timestamp_query_pool_;
        // The number of GPU ticks spent in the tile classification pass.
        std::uint64_t tile_classification_elapsed_time_;
        // The number of GPU ticks spent in depth buffer intersection.
        std::uint64_t intersection_elapsed_time_;
        // The number of GPU ticks spent denoising.
        std::uint64_t denoising_elapsed_time_;
        // The array of timestamp that were queried.
        std::vector<TimestampQueries> timestamp_queries_;
        // The index of the active set of timestamp queries.
        std::uint32_t timestamp_queries_index_;

        // Format of the resolved scene.
        VkFormat scene_format_;

        // The descriptor tables. One per shader pass per frame. 
        // Even with more than 2 frames in flight we only swap between the last two
        // as we keep only one frame of history.

        // Descriptor set for uniform buffers. Be conservative in the number of frames in flight.
        VkDescriptorSet uniform_buffer_descriptor_set_[8];
        // Descriptor sets of the tile classification pass.
        VkDescriptorSet tile_classification_descriptor_set_[2];
        // Descriptor sets of the indirect arguments pass.
        VkDescriptorSet indirect_args_descriptor_set_[2];
        // Descriptor sets of the depth buffer intersection pass.
        VkDescriptorSet intersection_descriptor_set_[2];
        // Descriptor sets of the spatial denoising pass.
        VkDescriptorSet spatial_denoising_descriptor_set_[2];
        // Descriptor sets of the temporal denoising pass.
        VkDescriptorSet temporal_denoising_descriptor_set_[2];
        // Descriptor sets of the eaw denoising pass.
        VkDescriptorSet eaw_denoising_descriptor_set_[2];

        // The view projection matrix of the last frame.
        matrix4 prev_view_projection_;

    private:
        VkImageMemoryBarrier Transition(VkImage image, VkImageLayout before, VkImageLayout after) const;
        void TransitionBarriers(VkCommandBuffer command_buffer, const VkImageMemoryBarrier* image_barriers, uint32_t image_barriers_count) const;
        void ComputeBarrier(VkCommandBuffer command_buffer) const;
        void IndirectArgumentsBarrier(VkCommandBuffer command_buffer) const;
    };
}

