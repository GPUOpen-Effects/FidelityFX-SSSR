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
#include "reflection_view_vk.h"

#include <string>
#include <array>

#include "context.h"
#include "reflection_error.h"
#include "reflection_view.h"
#include "context_vk.h"
#include "ffx_sssr_vk.h"

namespace ffx_sssr
{
    /**
        The constructor for the ReflectionViewVK class.
    */
    ReflectionViewVK::ReflectionViewVK()
        : width_(0)
        , height_(0)
        , flags_(0)
        , descriptor_pool_(0)
        , tile_list_()
        , tile_counter_()
        , ray_list_()
        , ray_counter_()
        , intersection_pass_indirect_args_()
        , denoiser_pass_indirect_args_()
        , temporal_denoiser_result_()
        , ray_lengths_()
        , temporal_variance_()
        , tile_classification_elapsed_time_(0)
        , intersection_elapsed_time_(0)
        , denoising_elapsed_time_(0)
        , timestamp_query_pool_(0)
        , timestamp_queries_()
        , timestamp_queries_index_(0)
        , scene_format_(VK_FORMAT_UNDEFINED)
        , tile_classification_descriptor_set_()
        , indirect_args_descriptor_set_()
        , intersection_descriptor_set_()
        , spatial_denoising_descriptor_set_()
        , temporal_denoising_descriptor_set_()
        , eaw_denoising_descriptor_set_()
        , prev_view_projection_()
        , uniform_buffer_descriptor_set_()
    {
    }

    /**
        The constructor for the ReflectionViewVK class.

        \param other The reflection view to be moved.
    */
    ReflectionViewVK::ReflectionViewVK(ReflectionViewVK&& other) noexcept
        : width_(other.width_)
        , height_(other.height_)
        , flags_(other.flags_)
        , descriptor_pool_(other.descriptor_pool_)
        , tile_classification_elapsed_time_(other.tile_classification_elapsed_time_)
        , intersection_elapsed_time_(other.intersection_elapsed_time_)
        , denoising_elapsed_time_(other.denoising_elapsed_time_)
        , timestamp_query_pool_(other.timestamp_query_pool_)
        , timestamp_queries_(std::move(other.timestamp_queries_))
        , timestamp_queries_index_(other.timestamp_queries_index_)
        , tile_list_(std::move(other.tile_list_))
        , tile_counter_(std::move(other.tile_counter_))
        , ray_list_(std::move(other.ray_list_))
        , ray_counter_(std::move(other.ray_counter_))
        , intersection_pass_indirect_args_(std::move(other.intersection_pass_indirect_args_))
        , denoiser_pass_indirect_args_(std::move(other.denoiser_pass_indirect_args_))
        , ray_lengths_(std::move(other.ray_lengths_))
        , temporal_variance_(std::move(other.temporal_variance_))
        , scene_format_(other.scene_format_)
        , prev_view_projection_(other.prev_view_projection_)
    {

        for (int i = 0; i < 2; ++i)
        {
            temporal_denoiser_result_[i] = std::move(other.temporal_denoiser_result_[i]);

            tile_classification_descriptor_set_[i] = other.tile_classification_descriptor_set_[i];
            indirect_args_descriptor_set_[i] = other.indirect_args_descriptor_set_[i];
            intersection_descriptor_set_[i] = other.intersection_descriptor_set_[i];
            spatial_denoising_descriptor_set_[i] = other.spatial_denoising_descriptor_set_[i];
            temporal_denoising_descriptor_set_[i] = other.temporal_denoising_descriptor_set_[i];
            eaw_denoising_descriptor_set_[i] = other.eaw_denoising_descriptor_set_[i];

            other.tile_classification_descriptor_set_[i] = VK_NULL_HANDLE;
            other.indirect_args_descriptor_set_[i] = VK_NULL_HANDLE;
            other.intersection_descriptor_set_[i] = VK_NULL_HANDLE;
            other.spatial_denoising_descriptor_set_[i] = VK_NULL_HANDLE;
            other.temporal_denoising_descriptor_set_[i] = VK_NULL_HANDLE;
            other.eaw_denoising_descriptor_set_[i] = VK_NULL_HANDLE;
        }

        for (int i = 0; i < FFX_SSSR_ARRAY_SIZE(uniform_buffer_descriptor_set_); ++i)
        {
            uniform_buffer_descriptor_set_[i] = other.uniform_buffer_descriptor_set_[i];
            other.uniform_buffer_descriptor_set_[i] = VK_NULL_HANDLE;
        }

        other.descriptor_pool_ = VK_NULL_HANDLE;
        other.timestamp_query_pool_ = VK_NULL_HANDLE;
    }

    /**
        The destructor for the ReflectionViewVK class.
    */
    ReflectionViewVK::~ReflectionViewVK()
    {
        if (linear_sampler_)
        {
            vkDestroySampler(device_, linear_sampler_, nullptr);
        }

        if (descriptor_pool_)
        {
            vkResetDescriptorPool(device_, descriptor_pool_, 0);
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        }

        if (timestamp_query_pool_)
        {
            vkDestroyQueryPool(device_, timestamp_query_pool_, nullptr);
        }
    }

    /**
        Assigns the reflection view.

        \param other The reflection view to be moved.
        \return The assigned reflection view.
    */
    ReflectionViewVK& ReflectionViewVK::operator =(ReflectionViewVK&& other) noexcept
    {
        if (this != &other)
        {
            width_ = other.width_;
            height_ = other.height_;
            flags_ = other.flags_;
            scene_format_ = other.scene_format_;
            prev_view_projection_ = other.prev_view_projection_;
            descriptor_pool_ = other.descriptor_pool_;
            device_ = other.device_;
            physical_device_ = other.physical_device_;

            timestamp_queries_ = other.timestamp_queries_;
            timestamp_queries_index_ = other.timestamp_queries_index_;
            tile_classification_elapsed_time_ = other.tile_classification_elapsed_time_;
            intersection_elapsed_time_ = other.intersection_elapsed_time_;
            denoising_elapsed_time_ = other.denoising_elapsed_time_;
            timestamp_query_pool_ = other.timestamp_query_pool_;

            tile_list_ = std::move(other.tile_list_);
            tile_counter_ = std::move(other.tile_counter_);
            ray_list_ = std::move(other.ray_list_);
            ray_counter_ = std::move(other.ray_counter_);
            intersection_pass_indirect_args_ = std::move(other.intersection_pass_indirect_args_);
            denoiser_pass_indirect_args_ = std::move(other.denoiser_pass_indirect_args_);
            ray_lengths_ = std::move(other.ray_lengths_);
            temporal_variance_ = std::move(other.temporal_variance_);

            other.descriptor_pool_ = VK_NULL_HANDLE;
            timestamp_query_pool_ = VK_NULL_HANDLE;

            for (int i = 0; i < 2; ++i)
            {
                temporal_denoiser_result_[i] = std::move(other.temporal_denoiser_result_[i]);

                tile_classification_descriptor_set_[i] = other.tile_classification_descriptor_set_[i];
                indirect_args_descriptor_set_[i] = other.indirect_args_descriptor_set_[i];
                intersection_descriptor_set_[i] = other.intersection_descriptor_set_[i];
                spatial_denoising_descriptor_set_[i] = other.spatial_denoising_descriptor_set_[i];
                temporal_denoising_descriptor_set_[i] = other.temporal_denoising_descriptor_set_[i];
                eaw_denoising_descriptor_set_[i] = other.eaw_denoising_descriptor_set_[i];

                other.tile_classification_descriptor_set_[i] = VK_NULL_HANDLE;
                other.indirect_args_descriptor_set_[i] = VK_NULL_HANDLE;
                other.intersection_descriptor_set_[i] = VK_NULL_HANDLE;
                other.spatial_denoising_descriptor_set_[i] = VK_NULL_HANDLE;
                other.temporal_denoising_descriptor_set_[i] = VK_NULL_HANDLE;
                other.eaw_denoising_descriptor_set_[i] = VK_NULL_HANDLE;
            }

            for (int i = 0; i < FFX_SSSR_ARRAY_SIZE(uniform_buffer_descriptor_set_); ++i)
            {
                uniform_buffer_descriptor_set_[i] = other.uniform_buffer_descriptor_set_[i];
                other.uniform_buffer_descriptor_set_[i] = VK_NULL_HANDLE;
            }
        }

        return *this;
    }

    /**
        Creates the reflection view.

        \param context The context to be used.
        \param create_reflection_view_info The reflection view creation information.
    */
    void ReflectionViewVK::Create(Context& context, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info)
    {
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo != nullptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->sceneFormat != VK_FORMAT_UNDEFINED);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->depthBufferHierarchySRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->motionBufferSRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->normalBufferSRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->roughnessBufferSRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->normalHistoryBufferSRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->roughnessHistoryBufferSRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->environmentMapSRV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->environmentMapSampler);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->reflectionViewUAV);
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo->uploadCommandBuffer);
        FFX_SSSR_ASSERT(create_reflection_view_info.outputWidth && create_reflection_view_info.outputHeight);

        // Populate the reflection view properties
        device_ = context.GetContextVK()->GetDevice();
        physical_device_ = context.GetContextVK()->GetPhysicalDevice();
        width_ = create_reflection_view_info.outputWidth;
        height_ = create_reflection_view_info.outputHeight;
        flags_ = create_reflection_view_info.flags;
        scene_format_ = create_reflection_view_info.pVkCreateReflectionViewInfo->sceneFormat;

        // Create pool for timestamp queries
        VkQueryPoolCreateInfo query_pool_create_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        query_pool_create_info.pNext = nullptr;
        query_pool_create_info.flags = 0;
        query_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_create_info.queryCount = kTimestampQuery_Count * context.GetFrameCountBeforeReuse();
        query_pool_create_info.pipelineStatistics = 0;
        if (VK_SUCCESS != vkCreateQueryPool(device_, &query_pool_create_info, NULL, &timestamp_query_pool_))
        {
            throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create timestamp query pool");
        }

        timestamp_queries_.resize(context.GetFrameCountBeforeReuse());
        for (auto& timestamp_queries : timestamp_queries_)
        {
            timestamp_queries.reserve(kTimestampQuery_Count);
        }

        // Create reflection view resources
        CreateDescriptorPool(context);
        SetupInternalResources(context, create_reflection_view_info);
        AllocateDescriptorSets(context);
        InitializeResourceDescriptorSets(context, create_reflection_view_info);
    }

    /**
        Returns an upper limit of required descriptors.

        \return The conservative count of total descriptors.
    */
    uint32_t ReflectionViewVK::GetConservativeResourceDescriptorCount(const Context& context) const
    {
        const ContextVK* vk_context = context.GetContextVK();
        uint32_t resource_descriptor_count = vk_context->GetTileClassificationPass().bindings_count_
            + vk_context->GetIndirectArgsPass().bindings_count_
            + vk_context->GetIntersectionPass().bindings_count_
            + vk_context->GetSpatialDenoisingPass().bindings_count_
            + vk_context->GetTemporalDenoisingPass().bindings_count_
            + vk_context->GetEawDenoisingPass().bindings_count_;
        resource_descriptor_count *= 2; // double buffering descriptors
        return resource_descriptor_count;
    }

    /**
        Creates the descriptor pool.

        \param context The context to be used.
    */
    void ReflectionViewVK::CreateDescriptorPool(const Context& context)
    {
        FFX_SSSR_ASSERT(!descriptor_pool_);
        uint32_t resource_descriptor_count = GetConservativeResourceDescriptorCount(context);

        uint32_t frame_count = context.GetFrameCountBeforeReuse();
        uint32_t uniform_buffer_descriptor_count = frame_count;
        
        // Low descriptor counts overall, so we just allocate the max count per type.
        VkDescriptorPoolSize pool_sizes[5];
        pool_sizes[0].descriptorCount = resource_descriptor_count;
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
        pool_sizes[1].descriptorCount = resource_descriptor_count;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        pool_sizes[2].descriptorCount = resource_descriptor_count;
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[3].descriptorCount = resource_descriptor_count;
        pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        pool_sizes[4].descriptorCount = uniform_buffer_descriptor_count;
        pool_sizes[4].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        uint32_t uniform_buffer_set_count = frame_count;
        uint32_t resources_set_count = 2 * 8; // 8 passes double buffered

        VkDescriptorPoolCreateInfo create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        create_info.pNext = nullptr;
        create_info.flags = 0;
        create_info.maxSets = uniform_buffer_set_count + resources_set_count;
        create_info.poolSizeCount = FFX_SSSR_ARRAY_SIZE(pool_sizes);
        create_info.pPoolSizes = pool_sizes;

        if (VK_SUCCESS != vkCreateDescriptorPool(device_, &create_info, nullptr, &descriptor_pool_))
        {
            throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create descriptor pool.");
        }
    }

    /**
        Creates all internal resources and handles initial resource transitions.

        \param context The context to be used.
        \param reflection_view The reflection view to be resolved.

    */
    void ReflectionViewVK::SetupInternalResources(Context & context, FfxSssrCreateReflectionViewInfo const & create_reflection_view_info)
    {
        VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler_info.pNext = nullptr;
        sampler_info.flags = 0;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler_info.mipLodBias = 0;
        sampler_info.anisotropyEnable = false;
        sampler_info.maxAnisotropy = 0;
        sampler_info.compareEnable = false;
        sampler_info.compareOp = VK_COMPARE_OP_NEVER;
        sampler_info.minLod = 0;
        sampler_info.maxLod = 16;
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        sampler_info.unnormalizedCoordinates = false;
        if (VK_SUCCESS != vkCreateSampler(device_, &sampler_info, nullptr, &linear_sampler_))
        {
            throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create linear sampler");
        }

        // Create tile classification-related buffers
        {
            uint32_t num_tiles = RoundedDivide(width_, 8u) * RoundedDivide(height_, 8u);
            uint32_t num_pixels = width_ * height_;

            uint32_t tile_list_element_count = num_tiles;
            uint32_t tile_counter_element_count = 1;
            uint32_t ray_list_element_count = num_pixels;
            uint32_t ray_counter_element_count = 1;
            uint32_t intersection_pass_indirect_args_element_count = 3;
            uint32_t denoiser_pass_indirect_args_element_count = 3;

            BufferVK::CreateInfo create_info = {};
            create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            create_info.format_ = VK_FORMAT_R32_UINT;
            create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

            create_info.size_in_bytes_ = tile_list_element_count * sizeof(uint32_t);
            create_info.name_ = "SSSR Tile List";
            tile_list_ = BufferVK(device_, physical_device_, create_info);

            create_info.size_in_bytes_ = ray_list_element_count * sizeof(uint32_t);
            create_info.name_ = "SSSR Ray List";
            ray_list_ = BufferVK(device_, physical_device_, create_info);

            create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            create_info.size_in_bytes_ = tile_counter_element_count * sizeof(uint32_t);
            create_info.name_ = "SSSR Tile Counter";
            tile_counter_ = BufferVK(device_, physical_device_, create_info);

            create_info.size_in_bytes_ = ray_counter_element_count * sizeof(uint32_t);
            create_info.name_ = "SSSR Ray Counter";
            ray_counter_ = BufferVK(device_, physical_device_, create_info);

            create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

            create_info.size_in_bytes_ = intersection_pass_indirect_args_element_count * sizeof(uint32_t);
            create_info.name_ = "SSSR Intersect Indirect Args";
            intersection_pass_indirect_args_ = BufferVK(device_, physical_device_, create_info);

            create_info.size_in_bytes_ = denoiser_pass_indirect_args_element_count * sizeof(uint32_t);
            create_info.name_ = "SSSR Denoiser Indirect Args";
            denoiser_pass_indirect_args_ = BufferVK(device_, physical_device_, create_info);
        }

        // Create denoising-related resources
        {
            ImageVK::CreateInfo create_info = {};
            create_info.width_ = width_;
            create_info.height_ = height_;
            create_info.mip_levels_ = 1;
            create_info.initial_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
            create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            create_info.image_usage_ = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            create_info.format_ = scene_format_;
            create_info.name_ = "SSSR Temporal Denoised Result 0";
            temporal_denoiser_result_[0] = ImageVK(device_, physical_device_, create_info);

            create_info.format_ = scene_format_;
            create_info.name_ = "SSSR Temporal Denoised Result 1";
            temporal_denoiser_result_[1] = ImageVK(device_, physical_device_, create_info);

            create_info.format_ = VK_FORMAT_R16_SFLOAT;
            create_info.name_ = "SSSR Ray Lengths";
            ray_lengths_ = ImageVK(device_, physical_device_, create_info);

            create_info.format_ = VK_FORMAT_R8_UNORM;
            create_info.name_ = "SSSR Temporal Variance";
            temporal_variance_ = ImageVK(device_, physical_device_, create_info);
        }

        VkCommandBuffer command_buffer = create_reflection_view_info.pVkCreateReflectionViewInfo->uploadCommandBuffer;

        VkImageMemoryBarrier image_barriers[] = {
            Transition(temporal_denoiser_result_[0].image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
            Transition(temporal_denoiser_result_[1].image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
            Transition(ray_lengths_.image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
            Transition(temporal_variance_.image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        };
        TransitionBarriers(command_buffer, image_barriers, FFX_SSSR_ARRAY_SIZE(image_barriers));

        // Initial clear of counters. Successive clears are handled by the indirect arguments pass. 
        vkCmdFillBuffer(command_buffer, ray_counter_.buffer_, 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(command_buffer, tile_counter_.buffer_, 0, VK_WHOLE_SIZE, 0);

        VkClearColorValue clear_calue = {};
        clear_calue.float32[0] = 0;
        clear_calue.float32[1] = 0;
        clear_calue.float32[2] = 0;
        clear_calue.float32[3] = 0;

        VkImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_range.baseArrayLayer = 0;
        subresource_range.baseMipLevel = 0;
        subresource_range.layerCount = 1;
        subresource_range.levelCount = 1;

        // Initial resource clears
        vkCmdClearColorImage(command_buffer, temporal_denoiser_result_[0].image_, VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
        vkCmdClearColorImage(command_buffer, temporal_denoiser_result_[1].image_, VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
        vkCmdClearColorImage(command_buffer, ray_lengths_.image_, VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
        vkCmdClearColorImage(command_buffer, temporal_variance_.image_, VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
    }

    /**
        Allocate all required descriptor sets from the descriptor pool.
        This includes double buffering of the resource descriptor sets and 
        multi-buffering of the descriptor set containing the uniform buffer descriptor.

        \param context The context to be used.
    */
    void ReflectionViewVK::AllocateDescriptorSets(Context& context)
    {
        ContextVK* vk_context = context.GetContextVK();
        for (int i = 0; i < 2; ++i)
        {
            tile_classification_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetTileClassificationPass().descriptor_set_layout_);
            indirect_args_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetIndirectArgsPass().descriptor_set_layout_);
            intersection_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetIntersectionPass().descriptor_set_layout_);
            spatial_denoising_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetSpatialDenoisingPass().descriptor_set_layout_);
            temporal_denoising_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetTemporalDenoisingPass().descriptor_set_layout_);
            eaw_denoising_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetEawDenoisingPass().descriptor_set_layout_);
        }

        uint32_t frame_count = context.GetFrameCountBeforeReuse();
        for (uint32_t i = 0; i < frame_count; ++i)
        {
            uniform_buffer_descriptor_set_[i] = AllocateDescriptorSet(context, vk_context->GetUniformBufferDescriptorSetLayout());
        }
    }

    /** 
        Allocate a single descriptor set from the descriptor pool.

        \param context The context to be used.
        \param layout The layout of the descriptor set.
        \return The allocated set.
    */
    VkDescriptorSet ReflectionViewVK::AllocateDescriptorSet(Context& context, VkDescriptorSetLayout layout)
    {
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = descriptor_pool_;
        alloc_info.pNext = nullptr;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout;

        VkDescriptorSet set;
        if (VK_SUCCESS != vkAllocateDescriptorSets(device_, &alloc_info, &set))
        {
            throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to allocate descriptor set");
        }
        return set;
    }

    /**
        Initializes the resource descriptor sets of each pass. 
        The uniform buffer on the other hand is updated each frame and thus not handled here.

        \param context The context to be used.
        \param reflection_view The reflection view to be resolved.

    */
    void ReflectionViewVK::InitializeResourceDescriptorSets(Context & context, FfxSssrCreateReflectionViewInfo const & create_reflection_view_info)
    {
        VkImageView scene_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->sceneSRV;
        VkImageView depth_hierarchy_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->depthBufferHierarchySRV;
        VkImageView motion_buffer_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->motionBufferSRV;
        VkImageView normal_buffer_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->normalBufferSRV;
        VkImageView roughness_buffer_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->roughnessBufferSRV;
        VkImageView normal_history_buffer_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->normalHistoryBufferSRV;
        VkImageView roughness_history_buffer_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->roughnessHistoryBufferSRV;
        VkSampler environment_map_sampler = create_reflection_view_info.pVkCreateReflectionViewInfo->environmentMapSampler;
        VkImageView environment_map_srv = create_reflection_view_info.pVkCreateReflectionViewInfo->environmentMapSRV;
        VkImageView output_buffer_uav = create_reflection_view_info.pVkCreateReflectionViewInfo->reflectionViewUAV;

        VkImageView normal_buffers[] = { normal_buffer_srv, normal_history_buffer_srv };
        VkImageView roughness_buffers[] = { roughness_buffer_srv, roughness_history_buffer_srv };

        bool ping_pong_normal = (create_reflection_view_info.flags & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_NORMAL_BUFFERS) != 0;
        bool ping_pong_roughness = (create_reflection_view_info.flags & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_ROUGHNESS_BUFFERS) != 0;

        uint32_t descriptor_count = GetConservativeResourceDescriptorCount(context);
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> write_desc_sets;
        image_infos.reserve(descriptor_count);
        write_desc_sets.reserve(descriptor_count);
        uint32_t binding = 0;
        VkDescriptorSet target_set = VK_NULL_HANDLE;

#define FFX_SSSR_DEBUG_DESCRIPTOR_SETUP 0

        auto BindSampler = [this, &target_set, &binding, &write_desc_sets, &image_infos](VkSampler sampler) {
            VkDescriptorImageInfo image_info = {};
            image_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_info.imageView = VK_NULL_HANDLE;
            image_info.sampler = sampler;
            image_infos.push_back(image_info);

            VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write_set.pNext = nullptr;
            write_set.dstSet = target_set;
            write_set.dstBinding = binding++;
            write_set.dstArrayElement = 0;
            write_set.descriptorCount = 1;
            write_set.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            write_set.pImageInfo = &image_infos.back();
            write_set.pBufferInfo = nullptr;
            write_set.pTexelBufferView = nullptr;
            write_desc_sets.push_back(write_set);

#if FFX_SSSR_DEBUG_DESCRIPTOR_SETUP
            vkUpdateDescriptorSets(device_, 1, &write_set, 0, nullptr);
#endif
        };

        auto BindImage = [this, &target_set, &binding, &write_desc_sets, &image_infos](VkDescriptorType type, VkImageView view, VkImageLayout layout) {
            VkDescriptorImageInfo image_info = {};
            image_info.imageLayout = layout;
            image_info.imageView = view;
            image_info.sampler = VK_NULL_HANDLE;
            image_infos.push_back(image_info);

            VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write_set.pNext = nullptr;
            write_set.dstSet = target_set;
            write_set.dstBinding = binding++;
            write_set.dstArrayElement = 0;
            write_set.descriptorCount = 1;
            write_set.descriptorType = type;
            write_set.pImageInfo = &image_infos.back();
            write_set.pBufferInfo = nullptr;
            write_set.pTexelBufferView = nullptr;
            write_desc_sets.push_back(write_set);

#if FFX_SSSR_DEBUG_DESCRIPTOR_SETUP
            vkUpdateDescriptorSets(device_, 1, &write_set, 0, nullptr);
#endif
        };

        auto BindBuffer = [this, &target_set, &binding, &write_desc_sets](VkDescriptorType type, const VkBufferView& buffer) {
            VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write_set.pNext = nullptr;
            write_set.dstSet = target_set;
            write_set.dstBinding = binding++;
            write_set.dstArrayElement = 0;
            write_set.descriptorCount = 1;
            write_set.descriptorType = type;
            write_set.pImageInfo = nullptr;
            write_set.pBufferInfo = nullptr;
            write_set.pTexelBufferView = &buffer;
            write_desc_sets.push_back(write_set);

#if FFX_SSSR_DEBUG_DESCRIPTOR_SETUP
            vkUpdateDescriptorSets(device_, 1, &write_set, 0, nullptr);
#endif
        };

        // Place the descriptors
        for (int i = 0; i < 2; ++i)
        {
            // Tile Classifier pass
            {
                target_set = tile_classification_descriptor_set_[i];
                binding = 0;
                
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_roughness
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, tile_list_.buffer_view_); // g_tile_list
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, ray_list_.buffer_view_); // g_ray_list
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, tile_counter_.buffer_view_); // g_tile_counter
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, ray_counter_.buffer_view_); // g_ray_counter
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_denoiser_result_[i].image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_temporally_denoised_reflections
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_denoiser_result_[1 - i].image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_temporally_denoised_reflections_history
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ray_lengths_.image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_ray_lengths
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_variance_.image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_temporal_variance
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output_buffer_uav, VK_IMAGE_LAYOUT_GENERAL); // g_denoised_reflections
            }

            // Indirect args pass
            {
                target_set = indirect_args_descriptor_set_[i];
                binding = 0;

                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, tile_counter_.buffer_view_); // g_tile_counter
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, ray_counter_.buffer_view_); // g_ray_counter
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, intersection_pass_indirect_args_.buffer_view_); // g_intersect_args
                BindBuffer(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, denoiser_pass_indirect_args_.buffer_view_); // g_denoiser_args
            }

            // Intersection pass
            {
                target_set = intersection_descriptor_set_[i];
                binding = 0;

                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, scene_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_lit_scene
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, depth_hierarchy_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_depth_buffer_hierarchy
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_normal
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_roughness
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, environment_map_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_environment_map

                auto const& sampler = context.GetContextVK()->GetSampler2SPP();
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, sampler.sobol_buffer_.buffer_view_); // g_sobol_buffer
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, sampler.ranking_tile_buffer_.buffer_view_); // g_ranking_tile_buffer
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, sampler.scrambling_tile_buffer_.buffer_view_); // g_scrambling_tile_buffer
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, ray_list_.buffer_view_); // g_ray_list

                BindSampler(linear_sampler_); // g_linear_sampler
                BindSampler(environment_map_sampler); // g_environment_map_sampler

                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_denoiser_result_[i].image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_intersection_result
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ray_lengths_.image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_ray_lengths
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output_buffer_uav, VK_IMAGE_LAYOUT_GENERAL); // g_denoised_reflections
            }

            // Spatial denoising pass
            {
                target_set = spatial_denoising_descriptor_set_[i];
                binding = 0;

                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, depth_hierarchy_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_depth_buffer
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_normal
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_roughness
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, temporal_denoiser_result_[i].image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_intersection_result
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, temporal_variance_.image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_has_ray
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, tile_list_.buffer_view_); // g_tile_list
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output_buffer_uav, VK_IMAGE_LAYOUT_GENERAL); // g_spatially_denoised_reflections
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ray_lengths_.image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_ray_lengths
            }

            // Temporal denoising pass
            {
                target_set = temporal_denoising_descriptor_set_[i];
                binding = 0;

                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_normal
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_roughness
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_normal ? normal_buffers[1 - i] : normal_history_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_normal_history
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_roughness ? roughness_buffers[1 - i] : roughness_history_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_roughness_history
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, depth_hierarchy_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_depth_buffer
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, motion_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_motion_vectors
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, temporal_denoiser_result_[1 - i].image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_temporally_denoised_reflections_history
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ray_lengths_.image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_ray_lengths
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, tile_list_.buffer_view_); // g_tile_list
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_denoiser_result_[i].image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_temporally_denoised_reflections
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output_buffer_uav, VK_IMAGE_LAYOUT_GENERAL); // g_spatially_denoised_reflections
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_variance_.image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_temporal_variance
            }

            // EAW denoising pass
            {
                target_set = eaw_denoising_descriptor_set_[i];
                binding = 0;

                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_normal
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_roughness
                BindImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, depth_hierarchy_srv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // g_depth_buffer
                BindBuffer(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, tile_list_.buffer_view_); // g_tile_list
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, temporal_denoiser_result_[i].image_view_, VK_IMAGE_LAYOUT_GENERAL); // g_temporally_denoised_reflections
                BindImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output_buffer_uav, VK_IMAGE_LAYOUT_GENERAL); // g_denoised_reflections
            }
        }
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(write_desc_sets.size()), write_desc_sets.data(), 0, nullptr);
    }

    /**
        Gets the index of the current timestamp query.

        \return The index of the current timestamp query.
    */
    std::uint32_t ReflectionViewVK::GetTimestampQueryIndex() const
    {
        return timestamp_queries_index_ * kTimestampQuery_Count + static_cast<std::uint32_t>(timestamp_queries_[timestamp_queries_index_].size());
    }

    float Clamp(float value, float min, float max)
    {
        if (value < min)
        {
            return min;
        }
        else if (value > max)
        {
            return max;
        }
        return value;
    }

    /**
        Resolves the Vulkan reflection view.

        \param context The context to be used.
        \param reflection_view The reflection view to be resolved.
        \param resolve_reflection_view_info The reflection view resolve information.
    */
    void ReflectionViewVK::Resolve(Context& context, ReflectionView const& reflection_view, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info)
    {
        auto const command_buffer = resolve_reflection_view_info.pVkCommandEncodeInfo->commandBuffer;
        if (!command_buffer)
        {
            throw reflection_error(context, FFX_SSSR_STATUS_INVALID_VALUE, "No command buffer was supplied, cannot encode device commands");
        }

        FFX_SSSR_ASSERT(resolve_reflection_view_info.pVkCommandEncodeInfo);
        FFX_SSSR_ASSERT(resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_1 || resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_2 || resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_4);

        // Query timestamp value prior to resolving the reflection view
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

            auto const start_index = timestamp_queries_index_ * kTimestampQuery_Count;

            if (!timestamp_queries.empty())
            {
                // Reset performance counters
                tile_classification_elapsed_time_ = 0ull;
                denoising_elapsed_time_ = 0ull;
                intersection_elapsed_time_ = 0ull;

                uint32_t timestamp_count = static_cast<uint32_t>(timestamp_queries.size());

                uint64_t data[kTimestampQuery_Count * 8]; // maximum of 8 frames in flight allowed
                VkResult result = vkGetQueryPoolResults(device_, 
                    timestamp_query_pool_, 
                    start_index, 
                    timestamp_count,
                    timestamp_count * sizeof(uint64_t),
                    data,
                    sizeof(uint64_t), 
                    VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

                if (result == VK_SUCCESS)
                {
                    for (auto i = 0u, j = 1u; j < timestamp_count; ++i, ++j)
                    {
                        auto const elapsed_time = (data[j] - data[i]);

                        switch (timestamp_queries[j])
                        {
                        case kTimestampQuery_TileClassification:
                            tile_classification_elapsed_time_ = elapsed_time;
                            break;
                        case kTimestampQuery_Intersection:
                            intersection_elapsed_time_ = elapsed_time;
                            break;
                        case kTimestampQuery_Denoising:
                            denoising_elapsed_time_ = elapsed_time;
                            break;
                        default:
                            // unrecognized timestamp query
                            break;
                        }
                    }
                }
                else if (result != VK_NOT_READY)
                {
                    throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to query timestamp query results");
                }
            }

            timestamp_queries.clear();

            vkCmdResetQueryPool(command_buffer, timestamp_query_pool_, start_index, kTimestampQuery_Count);

            vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_query_pool_, GetTimestampQueryIndex());
            timestamp_queries.push_back(kTimestampQuery_Init);
        }

        // Encode the relevant pass data
        struct PassData
        {
            matrix4 inv_view_projection_;
            matrix4 projection_;
            matrix4 inv_projection_;
            matrix4 view_;
            matrix4 inv_view_;
            matrix4 prev_view_projection_;
            std::uint32_t frame_index_;
            std::uint32_t max_traversal_intersections_;
            std::uint32_t min_traversal_occupancy_;
            std::uint32_t most_detailed_mip_;
            float temporal_stability_factor_;
            float depth_buffer_thickness_;
            std::uint32_t samples_per_quad_;
            std::uint32_t temporal_variance_guided_tracing_enabled_;
            float roughness_threshold_;
            std::uint32_t skip_denoiser_;
        };
        auto& upload_buffer = context.GetContextVK()->GetUploadBuffer();
        PassData* pass_data;
        if (!upload_buffer.AllocateBuffer(sizeof(PassData), pass_data))
        {
            throw reflection_error(context, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %u bytes of upload memory, consider increasing uploadBufferSize", sizeof(PassData));
        }

        // Fill constant buffer
        matrix4 view_projection = reflection_view.projection_matrix_ * reflection_view.view_matrix_;
        pass_data->inv_view_projection_ = matrix4::inverse(view_projection);
        pass_data->projection_ = reflection_view.projection_matrix_;
        pass_data->inv_projection_ = matrix4::inverse(reflection_view.projection_matrix_);
        pass_data->view_ = reflection_view.view_matrix_;
        pass_data->inv_view_ = matrix4::inverse(reflection_view.view_matrix_);
        pass_data->prev_view_projection_ = prev_view_projection_;
        pass_data->frame_index_ = context.GetFrameIndex();

        float temporal_stability_scale = Clamp(resolve_reflection_view_info.temporalStabilityScale, 0, 1);
        pass_data->max_traversal_intersections_ = resolve_reflection_view_info.maxTraversalIterations;
        pass_data->min_traversal_occupancy_ = resolve_reflection_view_info.minTraversalOccupancy;
        pass_data->most_detailed_mip_ = resolve_reflection_view_info.mostDetailedDepthHierarchyMipLevel;
        pass_data->temporal_stability_factor_ = temporal_stability_scale * temporal_stability_scale;
        pass_data->depth_buffer_thickness_ = resolve_reflection_view_info.depthBufferThickness;
        pass_data->samples_per_quad_ = resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_4 ? 4 : (resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_2 ? 2 : 1);
        pass_data->temporal_variance_guided_tracing_enabled_ = resolve_reflection_view_info.flags & FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_ENABLE_VARIANCE_GUIDED_TRACING ? 1 : 0;
        pass_data->roughness_threshold_ = resolve_reflection_view_info.roughnessThreshold;
        pass_data->skip_denoiser_ = resolve_reflection_view_info.flags & FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE ? 0 : 1;
        prev_view_projection_ = view_projection;
        
        uint32_t uniform_buffer_index = context.GetFrameIndex() % context.GetFrameCountBeforeReuse();
        VkDescriptorSet uniform_buffer_descriptor_set = uniform_buffer_descriptor_set_[uniform_buffer_index];

        // Update descriptor to sliding window in upload buffer that contains the updated pass data
        {
            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = upload_buffer.GetResource();
            buffer_info.offset = upload_buffer.GetOffset(pass_data);
            buffer_info.range = sizeof(PassData);

            VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write_set.pNext = nullptr;
            write_set.dstSet = uniform_buffer_descriptor_set;
            write_set.dstBinding = 0;
            write_set.dstArrayElement = 0;
            write_set.descriptorCount = 1;
            write_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write_set.pImageInfo = nullptr;
            write_set.pBufferInfo = &buffer_info;
            write_set.pTexelBufferView = nullptr;
            vkUpdateDescriptorSets(device_, 1, &write_set, 0, nullptr);
        }

        std::uint32_t resource_descriptor_set_index = context.GetFrameIndex() & 1u;

        ContextVK* vk_context = context.GetContextVK();

        // Tile Classification pass
        {
            VkDescriptorSet sets[] = { uniform_buffer_descriptor_set,  tile_classification_descriptor_set_[resource_descriptor_set_index] };
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetTileClassificationPass().pipeline_);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetTileClassificationPass().pipeline_layout_, 0, FFX_SSSR_ARRAY_SIZE(sets), sets, 0, nullptr);
            uint32_t dim_x = RoundedDivide(width_, 8u);
            uint32_t dim_y = RoundedDivide(height_, 8u);
            vkCmdDispatch(command_buffer, dim_x, dim_y, 1);
        }

        // Ensure that the tile classification pass finished
        ComputeBarrier(command_buffer);

        // Indirect Arguments pass
        {
            VkDescriptorSet sets[] = { uniform_buffer_descriptor_set,  indirect_args_descriptor_set_[resource_descriptor_set_index] };
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetIndirectArgsPass().pipeline_);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetIndirectArgsPass().pipeline_layout_, 0, FFX_SSSR_ARRAY_SIZE(sets), sets, 0, nullptr);
            vkCmdDispatch(command_buffer, 1, 1, 1);
        }

        // Query the amount of time spent in the intersection pass
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

            FFX_SSSR_ASSERT(timestamp_queries.size() == 1ull && timestamp_queries[0] == kTimestampQuery_Init);

            vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_query_pool_, GetTimestampQueryIndex());
            timestamp_queries.push_back(kTimestampQuery_TileClassification);
        }

        // Ensure that the arguments are written
        IndirectArgumentsBarrier(command_buffer);

        // Intersection pass
        {
            VkDescriptorSet sets[] = { uniform_buffer_descriptor_set,  intersection_descriptor_set_[resource_descriptor_set_index] };
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetIntersectionPass().pipeline_);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetIntersectionPass().pipeline_layout_, 0, FFX_SSSR_ARRAY_SIZE(sets), sets, 0, nullptr);
            vkCmdDispatchIndirect(command_buffer, intersection_pass_indirect_args_.buffer_, 0);
        }

        // Query the amount of time spent in the intersection pass
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

            FFX_SSSR_ASSERT(timestamp_queries.size() == 2ull && timestamp_queries[1] == kTimestampQuery_TileClassification);

            vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_query_pool_, GetTimestampQueryIndex());
            timestamp_queries.push_back(kTimestampQuery_Intersection);
        }

        if (resolve_reflection_view_info.flags & FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE)
        {
            // Ensure that the intersection pass finished
            VkImageMemoryBarrier intersection_finished_barriers[] = {
                Transition(temporal_denoiser_result_[resource_descriptor_set_index].image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                Transition(temporal_variance_.image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            };
            TransitionBarriers(command_buffer, intersection_finished_barriers, FFX_SSSR_ARRAY_SIZE(intersection_finished_barriers));

            // Spatial denoiser passes
            {
                VkDescriptorSet sets[] = { uniform_buffer_descriptor_set,  spatial_denoising_descriptor_set_[resource_descriptor_set_index] };
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetSpatialDenoisingPass().pipeline_);
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetSpatialDenoisingPass().pipeline_layout_, 0, FFX_SSSR_ARRAY_SIZE(sets), sets, 0, nullptr);
                vkCmdDispatchIndirect(command_buffer, denoiser_pass_indirect_args_.buffer_, 0);
            }

            // Ensure that the spatial denoising pass finished. We don't have the resource for the final result available, thus we have to wait for any UAV access to finish.
            VkImageMemoryBarrier spatial_denoiser_finished_barriers[] = {
                Transition(temporal_denoiser_result_[resource_descriptor_set_index].image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
                Transition(temporal_denoiser_result_[1 - resource_descriptor_set_index].image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                Transition(temporal_variance_.image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
                Transition(ray_lengths_.image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            };
            TransitionBarriers(command_buffer, spatial_denoiser_finished_barriers, FFX_SSSR_ARRAY_SIZE(spatial_denoiser_finished_barriers));
            
            // Temporal denoiser passes
            {
                VkDescriptorSet sets[] = { uniform_buffer_descriptor_set,  temporal_denoising_descriptor_set_[resource_descriptor_set_index] };
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetTemporalDenoisingPass().pipeline_);
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetTemporalDenoisingPass().pipeline_layout_, 0, FFX_SSSR_ARRAY_SIZE(sets), sets, 0, nullptr);
                vkCmdDispatchIndirect(command_buffer, denoiser_pass_indirect_args_.buffer_, 0);
            }

            // Ensure that the temporal denoising pass finished
            VkImageMemoryBarrier temporal_denoiser_finished_barriers[] = {
                Transition(ray_lengths_.image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
                Transition(temporal_denoiser_result_[1 - resource_descriptor_set_index].image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            };
            TransitionBarriers(command_buffer, temporal_denoiser_finished_barriers, FFX_SSSR_ARRAY_SIZE(temporal_denoiser_finished_barriers));

            // EAW denoiser passes
            {
                VkDescriptorSet sets[] = { uniform_buffer_descriptor_set,  eaw_denoising_descriptor_set_[resource_descriptor_set_index] };
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetEawDenoisingPass().pipeline_);
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_context->GetEawDenoisingPass().pipeline_layout_, 0, FFX_SSSR_ARRAY_SIZE(sets), sets, 0, nullptr);
                vkCmdDispatchIndirect(command_buffer, denoiser_pass_indirect_args_.buffer_, 0);
            }

            // Query the amount of time spent in the denoiser passes
            if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
            {
                auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

                FFX_SSSR_ASSERT(timestamp_queries.size() == 3ull && timestamp_queries[2] == kTimestampQuery_Intersection);

                vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_query_pool_, GetTimestampQueryIndex());
                timestamp_queries.push_back(kTimestampQuery_Denoising);
            }
        }

        // Move timestamp queries to next frame
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            timestamp_queries_index_ = (timestamp_queries_index_ + 1u) % context.GetFrameCountBeforeReuse();
        }
    }

    VkImageMemoryBarrier ReflectionViewVK::Transition(VkImage image, VkImageLayout before, VkImageLayout after) const
    {
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.pNext = nullptr;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = before;
        barrier.newLayout = after;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;

        barrier.subresourceRange = subresourceRange;
        return barrier;
    }

    void ReflectionViewVK::TransitionBarriers(VkCommandBuffer command_buffer, const VkImageMemoryBarrier * image_barriers, uint32_t image_barriers_count) const
    {
        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            image_barriers_count, image_barriers);
    }

    void ReflectionViewVK::ComputeBarrier(VkCommandBuffer command_buffer) const
    {
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.pNext = nullptr;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            1, &barrier,
            0, nullptr,
            0, nullptr);
    }

    void ReflectionViewVK::IndirectArgumentsBarrier(VkCommandBuffer command_buffer) const
    {
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.pNext = nullptr;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0,
            1, &barrier,
            0, nullptr,
            0, nullptr);
    }
}
