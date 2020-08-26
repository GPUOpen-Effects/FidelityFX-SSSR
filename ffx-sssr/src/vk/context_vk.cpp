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
#include "context_vk.h"

#include <functional>
#include <sstream>

#if FFX_SSSR_DUMP_SHADERS
#include <fstream>
#endif // FFX_SSSR_DUMP_SHADERS

#include "utils.h"
#include "context.h"
#include "reflection_view.h"
#include "ffx_sssr_vk.h"

#include "shader_common.h"
#include "shader_classify_tiles.h"
#include "shader_intersect.h"
#include "shader_prepare_indirect_args.h"
#include "shader_resolve_eaw.h"
#include "shader_resolve_spatial.h"
#include "shader_resolve_temporal.h"

namespace
{
    auto constexpr D3D12_VENDOR_ID_AMD    = 0x1002u;
    auto constexpr D3D12_VENDOR_ID_INTEL  = 0x8086u;
    auto constexpr D3D12_VENDOR_ID_NVIDIA = 0x10DEu;


    namespace _1
    {
        #include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
    }

    namespace _2
    {
        #include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_2spp.cpp"
    }

    /**
        The available blue noise samplers for various sampling modes.
    */
    struct
    {
        std::int32_t const (&sobol_buffer_)[256 * 256];
        std::int32_t const (&ranking_tile_buffer_)[128 * 128 * 8];
        std::int32_t const (&scrambling_tile_buffer_)[128 * 128 * 8];
    }
    const g_sampler_states[] =
    {
        {  _1::sobol_256spp_256d,  _1::rankingTile,  _1::scramblingTile },
        {  _2::sobol_256spp_256d,  _2::rankingTile,  _2::scramblingTile },
    };
}

namespace ffx_sssr
{
    /**
        The constructor for the ContextVK class.

        \param context The execution context.
        \param create_context_info The context creation information.
    */
    ContextVK::ContextVK(Context& context, FfxSssrCreateContextInfo const& create_context_info) : 
        context_(context)
        , device_(create_context_info.pVkCreateContextInfo->device)
        , physical_device_(create_context_info.pVkCreateContextInfo->physicalDevice)
        , upload_buffer_(*this, create_context_info.uploadBufferSize)
        , shader_compiler_(context)
        , samplers_were_populated_(false)
        , is_subgroup_size_control_extension_available_(false)
        , tile_classification_pass_()
        , indirect_args_pass_()
        , intersection_pass_()
        , spatial_denoising_pass_()
        , temporal_denoising_pass_()
        , eaw_denoising_pass_()
        , reflection_views_(create_context_info.maxReflectionViewCount)
    {
        if (!device_)
        {
            throw reflection_error(context, FFX_SSSR_STATUS_INVALID_VALUE, "No device was supplied.");
        }

        // Query if the implementation supports VK_EXT_subgroup_size_control
        // This is the case if VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME is present.
        // Rely on the application to enable the extension if it's available.
        uint32_t extension_count;
        if (VK_SUCCESS != vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, NULL))
        {
            throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to enumerate device extension properties.");
        }
        std::vector<VkExtensionProperties> device_extension_properties(extension_count);
        if (VK_SUCCESS != vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, device_extension_properties.data()))
        {
            throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to query device extension properties.");
        }

        is_subgroup_size_control_extension_available_ = std::find_if(device_extension_properties.begin(), device_extension_properties.end(),
            [](const VkExtensionProperties& extensionProps) -> bool { return strcmp(extensionProps.extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) == 0; })
            != device_extension_properties.end();
        
        upload_buffer_.Initialize();
        CompileShaders(create_context_info);
        CreatePipelines();

        // Create our blue noise samplers
        BlueNoiseSamplerVK* blue_noise_samplers[] = { &blue_noise_sampler_1spp_, &blue_noise_sampler_2spp_ };
        static_assert(FFX_SSSR_ARRAY_SIZE(blue_noise_samplers) == FFX_SSSR_ARRAY_SIZE(g_sampler_states), "Sampler arrays don't match.");
        for (auto i = 0u; i < FFX_SSSR_ARRAY_SIZE(g_sampler_states); ++i)
        {
            auto const& sampler_state = g_sampler_states[i];
            BlueNoiseSamplerVK* sampler = blue_noise_samplers[i];

            BufferVK::CreateInfo create_info = {};
            create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            create_info.buffer_usage_ = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
            create_info.format_ = VK_FORMAT_R32_UINT;

            create_info.size_in_bytes_ = sizeof(sampler_state.sobol_buffer_);
            create_info.name_ = "SSSR Sobol Buffer";
            sampler->sobol_buffer_ = BufferVK(device_, physical_device_, create_info);

            create_info.size_in_bytes_ = sizeof(sampler_state.ranking_tile_buffer_);
            create_info.name_ = "SSSR Ranking Tile Buffer";
            sampler->ranking_tile_buffer_ = BufferVK(device_, physical_device_, create_info);
            
            create_info.size_in_bytes_ = sizeof(sampler_state.scrambling_tile_buffer_);
            create_info.name_ = "SSSR Scrambling Tile Buffer";
            sampler->scrambling_tile_buffer_ = BufferVK(device_, physical_device_, create_info);
        }

        VkCommandBuffer command_buffer = create_context_info.pVkCreateContextInfo->uploadCommandBuffer;
        if (!samplers_were_populated_)
        {
            std::int32_t* upload_buffer;

            // Upload the relevant data to the various samplers
            for (auto i = 0u; i < FFX_SSSR_ARRAY_SIZE(g_sampler_states); ++i)
            {
                auto const& sampler_state = g_sampler_states[i];
                BlueNoiseSamplerVK* sampler = blue_noise_samplers[i];

                if (!upload_buffer_.AllocateBuffer(sizeof(sampler_state.sobol_buffer_), upload_buffer))
                {
                    throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %llukiB of upload memory, consider increasing uploadBufferSize", RoundedDivide(sizeof(sampler_state.sobol_buffer_), 1024ull));
                }
                memcpy(upload_buffer, sampler_state.sobol_buffer_, sizeof(sampler_state.sobol_buffer_));

                VkBufferCopy region = {};
                region.srcOffset = static_cast<VkDeviceSize>(upload_buffer_.GetOffset(upload_buffer));
                region.dstOffset = 0;
                region.size = sizeof(sampler_state.sobol_buffer_);
                vkCmdCopyBuffer(command_buffer, upload_buffer_.GetResource(), sampler->sobol_buffer_.buffer_, 1, &region);

                if (!upload_buffer_.AllocateBuffer(sizeof(sampler_state.ranking_tile_buffer_), upload_buffer))
                {
                    throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %llukiB of upload memory, consider increasing uploadBufferSize", RoundedDivide(sizeof(sampler_state.ranking_tile_buffer_), 1024ull));
                }
                memcpy(upload_buffer, sampler_state.ranking_tile_buffer_, sizeof(sampler_state.ranking_tile_buffer_));

                region.srcOffset = static_cast<VkDeviceSize>(upload_buffer_.GetOffset(upload_buffer));
                region.dstOffset = 0;
                region.size = sizeof(sampler_state.ranking_tile_buffer_);
                vkCmdCopyBuffer(command_buffer, upload_buffer_.GetResource(), sampler->ranking_tile_buffer_.buffer_, 1, &region);
                
                if (!upload_buffer_.AllocateBuffer(sizeof(sampler_state.scrambling_tile_buffer_), upload_buffer))
                {
                    throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %llukiB of upload memory, consider increasing uploadBufferSize", RoundedDivide(sizeof(sampler_state.scrambling_tile_buffer_), 1024ull));
                }
                memcpy(upload_buffer, sampler_state.scrambling_tile_buffer_, sizeof(sampler_state.scrambling_tile_buffer_));

                region.srcOffset = static_cast<VkDeviceSize>(upload_buffer_.GetOffset(upload_buffer));
                region.dstOffset = 0;
                region.size = sizeof(sampler_state.scrambling_tile_buffer_);
                vkCmdCopyBuffer(command_buffer, upload_buffer_.GetResource(), sampler->scrambling_tile_buffer_.buffer_, 1, &region);
            }
            
            // Flag that the samplers are now ready to use
            samplers_were_populated_ = true;
        }
    }

    /**
        The destructor for the ContextVK class.
    */
    ContextVK::~ContextVK()
    {
        if (uniform_buffer_descriptor_set_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, uniform_buffer_descriptor_set_layout_, nullptr);
        }
    }

    /**
        Gets the number of GPU ticks spent in the tile classification pass.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent in the tile classification pass.
    */
    void ContextVK::GetReflectionViewTileClassificationElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
    {
        FFX_SSSR_ASSERT(reflection_views_.At(ID(reflection_view_id)));    // not created properly?
        FFX_SSSR_ASSERT(context_.IsOfType<kResourceType_ReflectionView>(reflection_view_id) && context_.IsObjectValid(reflection_view_id));

        auto const& reflection_view = reflection_views_[ID(reflection_view_id)];

        if (!((reflection_view.flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0))
        {
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_OPERATION, "Cannot query the tile classification elapsed time of a reflection view that was not created with the FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS flag");
        }

        elapsed_time = reflection_view.tile_classification_elapsed_time_;
    }

    /**
        Gets the number of GPU ticks spent intersecting the depth buffer.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent intersecting the depth buffer.
    */
    void ContextVK::GetReflectionViewIntersectionElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
    {
        FFX_SSSR_ASSERT(reflection_views_.At(ID(reflection_view_id)));    // not created properly?
        FFX_SSSR_ASSERT(context_.IsOfType<kResourceType_ReflectionView>(reflection_view_id) && context_.IsObjectValid(reflection_view_id));

        auto const& reflection_view = reflection_views_[ID(reflection_view_id)];

        if (!((reflection_view.flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0))
        {
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_OPERATION, "Cannot query the intersection elapsed time of a reflection view that was not created with the FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS flag");
        }

        elapsed_time = reflection_view.intersection_elapsed_time_;
    }

    /**
        Gets the number of GPU ticks spent denoising the Vulkan reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent denoising.
    */
    void ContextVK::GetReflectionViewDenoisingElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
    {
        FFX_SSSR_ASSERT(reflection_views_.At(ID(reflection_view_id)));    // not created properly?
        FFX_SSSR_ASSERT(context_.IsOfType<kResourceType_ReflectionView>(reflection_view_id) && context_.IsObjectValid(reflection_view_id));

        auto const& reflection_view = reflection_views_[ID(reflection_view_id)];

        if (!((reflection_view.flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0))
        {
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_OPERATION, "Cannot query the denoising elapsed time of a reflection view that was not created with the FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS flag");
        }

        elapsed_time = reflection_view.denoising_elapsed_time_;
    }

    /**
        Creates the Vulkan reflection view.

        \param reflection_view_id The identifier of the reflection view object.
        \param create_reflection_view_info The reflection view creation information.
    */
    void ContextVK::CreateReflectionView(std::uint64_t reflection_view_id, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info)
    {
        FFX_SSSR_ASSERT(create_reflection_view_info.pVkCreateReflectionViewInfo);
        FFX_SSSR_ASSERT(context_.IsOfType<kResourceType_ReflectionView>(reflection_view_id) && context_.IsObjectValid(reflection_view_id));

        // Check user arguments
        if (!create_reflection_view_info.outputWidth || !create_reflection_view_info.outputHeight)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The outputWidth and outputHeight parameters are required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->depthBufferHierarchySRV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The depthBufferHierarchySRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->motionBufferSRV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The motionBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->normalBufferSRV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The normalBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->roughnessBufferSRV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The roughnessBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->normalHistoryBufferSRV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The normalHistoryBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->roughnessHistoryBufferSRV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The roughnessHistoryBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->reflectionViewUAV)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The environmentMapSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->environmentMapSampler)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The environmentMapSampler parameter is required when creating a reflection view");
        if(create_reflection_view_info.pVkCreateReflectionViewInfo->sceneFormat == VK_FORMAT_UNDEFINED)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The sceneFormat parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pVkCreateReflectionViewInfo->uploadCommandBuffer)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The uploadCommandBuffer parameter is required when creating a reflection view");

        // Create the reflection view
        auto& reflection_view = reflection_views_.Insert(ID(reflection_view_id));
        reflection_view.Create(context_, create_reflection_view_info);
    }

    /**
        Resolves the Vulkan reflection view.

        \param reflection_view_id The identifier of the reflection view object.
        \param resolve_reflection_view_info The reflection view resolve information.
    */
    void ContextVK::ResolveReflectionView(std::uint64_t reflection_view_id, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info)
    {
        FFX_SSSR_ASSERT(reflection_views_.At(ID(reflection_view_id)));    // not created properly?
        FFX_SSSR_ASSERT(context_.IsOfType<kResourceType_ReflectionView>(reflection_view_id) && context_.IsObjectValid(reflection_view_id));
        FFX_SSSR_ASSERT(context_.reflection_view_view_matrices_.At(ID(reflection_view_id)));
        FFX_SSSR_ASSERT(context_.reflection_view_projection_matrices_.At(ID(reflection_view_id)));

        ReflectionView reflection_view;
        reflection_view.view_matrix_ = context_.reflection_view_view_matrices_[ID(reflection_view_id)];
        reflection_view.projection_matrix_ = context_.reflection_view_projection_matrices_[ID(reflection_view_id)];

        reflection_views_[ID(reflection_view_id)].Resolve(context_, reflection_view, resolve_reflection_view_info);
    }


    void ContextVK::CompileShaders(FfxSssrCreateContextInfo const& create_context_info)
    {
        struct
        {
            char const* shader_name_ = nullptr;
            char const* content_ = nullptr;
            char const* profile_ = nullptr;
        }
        const shader_source[] =
        {
            { "prepare_indirect_args",   prepare_indirect_args,    "cs_6_0"},
            { "classify_tiles",          classify_tiles,           "cs_6_0"},
            { "intersect",               intersect,                "cs_6_0"},
            { "resolve_spatial",         resolve_spatial,          "cs_6_0"},
            { "resolve_temporal",        resolve_temporal,         "cs_6_0"},
            { "resolve_eaw",             resolve_eaw,              "cs_6_0"},
        };

        auto const common_include = std::string(common);

        DxcDefine defines[10];
        defines[0].Name = L"FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT";
        defines[0].Value = create_context_info.pRoughnessTextureFormat;
        defines[1].Name = L"FFX_SSSR_ROUGHNESS_UNPACK_FUNCTION";
        defines[1].Value = create_context_info.pUnpackRoughnessSnippet;
        defines[2].Name = L"FFX_SSSR_NORMALS_TEXTURE_FORMAT";
        defines[2].Value = create_context_info.pNormalsTextureFormat;
        defines[3].Name = L"FFX_SSSR_NORMALS_UNPACK_FUNCTION";
        defines[3].Value = create_context_info.pUnpackNormalsSnippet;
        defines[4].Name = L"FFX_SSSR_MOTION_VECTOR_TEXTURE_FORMAT";
        defines[4].Value = create_context_info.pMotionVectorFormat;
        defines[5].Name = L"FFX_SSSR_MOTION_VECTOR_UNPACK_FUNCTION";
        defines[5].Value = create_context_info.pUnpackMotionVectorsSnippet;
        defines[6].Name = L"FFX_SSSR_DEPTH_TEXTURE_FORMAT";
        defines[6].Value = create_context_info.pDepthTextureFormat;
        defines[7].Name = L"FFX_SSSR_DEPTH_UNPACK_FUNCTION";
        defines[7].Value = create_context_info.pUnpackDepthSnippet;
        defines[8].Name = L"FFX_SSSR_SCENE_TEXTURE_FORMAT";
        defines[8].Value = create_context_info.pSceneTextureFormat;
        defines[9].Name = L"FFX_SSSR_SCENE_RADIANCE_UNPACK_FUNCTION";
        defines[9].Value = create_context_info.pUnpackSceneRadianceSnippet;

        static_assert(FFX_SSSR_ARRAY_SIZE(shader_source) == kShader_Count, "'kShader_Count' filenames must be provided for building the various shaders");
        std::stringstream shader_content;
        LPCWSTR dxc_arguments[] = { L"-spirv", L"-fspv-target-env=vulkan1.1" };
        for (auto i = 0u; i < kShader_Count; ++i)
        {
            // Append common includes
            shader_content.str(std::string());
            shader_content.clear();
            shader_content << common << std::endl << shader_source[i].content_;

            shaders_[i] = shader_compiler_.CompileShaderString(
                shader_content.str().c_str(),
                static_cast<uint32_t>(shader_content.str().size()),
                shader_source[i].shader_name_,
                shader_source[i].profile_,
                dxc_arguments, FFX_SSSR_ARRAY_SIZE(dxc_arguments),
                defines, FFX_SSSR_ARRAY_SIZE(defines));
        }
    }

    /**
        Creates the reflection view pipeline state.

        \param context The Vulkan context to be used.
    */
    void ContextVK::CreatePipelines()
    {
        VkDescriptorSetLayoutBinding layout_binding = {};
        layout_binding.binding = 0;
        layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        layout_binding.descriptorCount = 1;
        layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layout_binding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptor_set_layout_create_info.pNext = nullptr;
        descriptor_set_layout_create_info.flags = 0;
        descriptor_set_layout_create_info.bindingCount = 1;
        descriptor_set_layout_create_info.pBindings = &layout_binding;
        if (VK_SUCCESS != vkCreateDescriptorSetLayout(device_, &descriptor_set_layout_create_info, nullptr, &uniform_buffer_descriptor_set_layout_))
        {
            throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create descriptor set layout for uniform buffer");
        }

        auto Setup = [this](ShaderPass& pass, ContextVK::Shader shader, const VkDescriptorSetLayoutBinding* bindings, uint32_t bindings_count, VkPipelineShaderStageCreateFlags flags = 0) {

            pass.device_ = device_;
            pass.bindings_count_ = bindings_count;

            VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            descriptor_set_layout_create_info.pNext = nullptr;
            descriptor_set_layout_create_info.flags = 0;
            descriptor_set_layout_create_info.bindingCount = bindings_count;
            descriptor_set_layout_create_info.pBindings = bindings;
            if (VK_SUCCESS != vkCreateDescriptorSetLayout(device_, &descriptor_set_layout_create_info, nullptr, &pass.descriptor_set_layout_))
            {
                throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create descriptor set layout");
            }

            VkDescriptorSetLayout layouts[2];
            layouts[0] = uniform_buffer_descriptor_set_layout_;
            layouts[1] = pass.descriptor_set_layout_;

            VkPipelineLayoutCreateInfo layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layout_create_info.pNext = nullptr;
            layout_create_info.flags = 0;
            layout_create_info.setLayoutCount = FFX_SSSR_ARRAY_SIZE(layouts);
            layout_create_info.pSetLayouts = layouts;
            layout_create_info.pushConstantRangeCount = 0;
            layout_create_info.pPushConstantRanges = nullptr;
            if (VK_SUCCESS != vkCreatePipelineLayout(device_, &layout_create_info, nullptr, &pass.pipeline_layout_))
            {
                throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create pipeline layout");
            }

            const ShaderVK& shader_vk = GetShader(shader);

            VkShaderModuleCreateInfo shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            shader_create_info.pNext = nullptr;
            shader_create_info.flags = 0;
            shader_create_info.codeSize = shader_vk.BytecodeLength;
            shader_create_info.pCode = static_cast<const uint32_t*>(shader_vk.pShaderBytecode);

            VkShaderModule shader_module = VK_NULL_HANDLE;
            if (VK_SUCCESS != vkCreateShaderModule(device_, &shader_create_info, nullptr, &shader_module))
            {
                throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create shader module");
            }

            VkPipelineShaderStageCreateInfo stage_create_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stage_create_info.pNext = nullptr;
            stage_create_info.flags = flags;
            stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stage_create_info.module = shader_module;
            stage_create_info.pName = "main";
            stage_create_info.pSpecializationInfo = nullptr;

            VkComputePipelineCreateInfo create_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            create_info.pNext = nullptr;
            create_info.basePipelineHandle = VK_NULL_HANDLE;
            create_info.basePipelineIndex = 0;
            create_info.flags = 0;
#if FFX_SSSR_DUMP_SHADERS
            create_info.flags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
#endif // FFX_SSSR_DUMP_SHADERS
            create_info.layout = pass.pipeline_layout_;
            create_info.stage = stage_create_info;
            if (VK_SUCCESS != vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &create_info, nullptr, &pass.pipeline_))
            {
                throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create compute pipeline state");
            }

            vkDestroyShaderModule(device_, shader_module, nullptr);
        };

        auto Bind = [](uint32_t binding, VkDescriptorType type)
        {
            VkDescriptorSetLayoutBinding layout_binding = {};
            layout_binding.binding = binding;
            layout_binding.descriptorType = type;
            layout_binding.descriptorCount = 1;
            layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layout_binding.pImmutableSamplers = nullptr;
            return layout_binding;
        };

        // Assemble the shader pass for tile classification
        {
            uint32_t binding = 0;
            VkDescriptorSetLayoutBinding layout_bindings[] = {
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_tile_list
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_list
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_tile_counter
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporally_denoised_reflections
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporally_denoised_reflections_history
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_ray_lengths
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporal_variance
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_denoised_reflections
            };
            Setup(tile_classification_pass_, ContextVK::kShader_TileClassification, layout_bindings, FFX_SSSR_ARRAY_SIZE(layout_bindings));
        }

        // Assemble the shader pass that prepares the indirect arguments
        {
            uint32_t binding = 0;
            VkDescriptorSetLayoutBinding layout_bindings[] = {
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_tile_counter
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_intersect_args
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_denoiser_args
            };
            Setup(indirect_args_pass_, ContextVK::kShader_IndirectArguments, layout_bindings, FFX_SSSR_ARRAY_SIZE(layout_bindings));
        }

        // Assemble the shader pass for intersecting reflection rays with the depth buffer
        {
            uint32_t binding = 0;
            VkDescriptorSetLayoutBinding layout_bindings[] = {
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_lit_scene
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer_hierarchy
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_environment_map
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_sobol_buffer
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_ranking_tile_buffer
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_scrambling_tile_buffer
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_ray_list
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_linear_sampler
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_environment_map_sampler
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_intersection_result
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_ray_lengths
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_denoised_reflections
            };
            Setup(intersection_pass_, ContextVK::kShader_Intersection, layout_bindings, FFX_SSSR_ARRAY_SIZE(layout_bindings));
        }

        // Assemble the shader pass for spatial resolve
        {
            uint32_t binding = 0;
            VkDescriptorSetLayoutBinding layout_bindings[] = {
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_intersection_result
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_has_ray
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_tile_list
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_spatially_denoised_reflections
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_ray_lengths
            };
            Setup(spatial_denoising_pass_, ContextVK::kShader_SpatialResolve, layout_bindings, FFX_SSSR_ARRAY_SIZE(layout_bindings), 
                is_subgroup_size_control_extension_available_ ? VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT : 0);
        }

        // Assemble the shader pass for temporal resolve
        {
            uint32_t binding = 0;
            VkDescriptorSetLayoutBinding layout_bindings[] = {
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal_history
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness_history
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_motion_vectors
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_temporally_denoised_reflections_history
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_ray_lengths
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_tile_list
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporally_denoised_reflections
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_spatially_denoised_reflections
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporal_variance
            };
            Setup(temporal_denoising_pass_, ContextVK::kShader_TemporalResolve, layout_bindings, FFX_SSSR_ARRAY_SIZE(layout_bindings));
        }

        // Assemble the shader pass for EAW resolve
        {
            uint32_t binding = 0;
            VkDescriptorSetLayoutBinding layout_bindings[] = {
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
                Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
                Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_tile_list
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporally_denoised_reflections
                Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_denoised_reflections
            };
            Setup(eaw_denoising_pass_, ContextVK::kShader_EAWResolve, layout_bindings, FFX_SSSR_ARRAY_SIZE(layout_bindings));
        }

#if FFX_SSSR_DUMP_SHADERS
        tile_classification_pass_.DumpInternalRepresentations("classify_tiles.dump.spirv.amdil.isa");
        indirect_args_pass_.DumpInternalRepresentations("prepare_indirect_args.dump.spirv.amdil.isa");
        intersection_pass_.DumpInternalRepresentations("intersect.dump.spirv.amdil.isa");
        spatial_denoising_pass_.DumpInternalRepresentations("resolve_spatial.dump.spirv.amdil.isa");
        temporal_denoising_pass_.DumpInternalRepresentations("resolve_temporal.dump.spirv.amdil.isa");
        eaw_denoising_pass_.DumpInternalRepresentations("resolve_eaw.dump.spirv.amdil.isa");
#endif // FFX_SSSR_DUMP_SHADERS
    }

    const ContextVK::ShaderPass& ContextVK::GetTileClassificationPass() const
    {
        return tile_classification_pass_;
    }

    const ContextVK::ShaderPass& ContextVK::GetIndirectArgsPass() const
    {
        return indirect_args_pass_;
    }

    const ContextVK::ShaderPass& ContextVK::GetIntersectionPass() const
    {
        return intersection_pass_;
    }

    const ContextVK::ShaderPass& ContextVK::GetSpatialDenoisingPass() const
    {
        return spatial_denoising_pass_;
    }

    const ContextVK::ShaderPass& ContextVK::GetTemporalDenoisingPass() const
    {
        return temporal_denoising_pass_;
    }

    const ContextVK::ShaderPass& ContextVK::GetEawDenoisingPass() const
    {
        return eaw_denoising_pass_;
    }

    VkDescriptorSetLayout ContextVK::GetUniformBufferDescriptorSetLayout() const
    {
        return uniform_buffer_descriptor_set_layout_;
    }

    void ffx_sssr::ContextVK::ShaderPass::DumpInternalRepresentations(const char* path)
    {
#if FFX_SSSR_DUMP_SHADERS
        VkResult res = VK_SUCCESS;

        std::ofstream filestream(path);

        PFN_vkGetPipelineExecutablePropertiesKHR vkGetPipelineExecutablePropertiesKHR = (PFN_vkGetPipelineExecutablePropertiesKHR)vkGetDeviceProcAddr(device_, "vkGetPipelineExecutablePropertiesKHR");
        PFN_vkGetPipelineExecutableInternalRepresentationsKHR vkGetPipelineExecutableInternalRepresentationsKHR = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)vkGetDeviceProcAddr(device_, "vkGetPipelineExecutableInternalRepresentationsKHR");
        if (!vkGetPipelineExecutablePropertiesKHR || !vkGetPipelineExecutableInternalRepresentationsKHR)
        {
            FFX_SSSR_ASSERT(false); // Could not retrieve pipeline executable function pointers - is VK_KHR_pipeline_executable_properties enabled?
            return;
        }

        VkPipelineInfoKHR pipeline_info = {
            VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, NULL, pipeline_,
        };

        uint32_t executables_count = 0;
        res = vkGetPipelineExecutablePropertiesKHR(device_, &pipeline_info, &executables_count, NULL);
        FFX_SSSR_ASSERT(res == VK_SUCCESS);
        std::vector<VkPipelineExecutablePropertiesKHR> executables(executables_count);
        for (uint32_t i = 0; i < executables_count; ++i)
        {
            executables[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
        }
        res = vkGetPipelineExecutablePropertiesKHR(device_, &pipeline_info, &executables_count, executables.data());
        FFX_SSSR_ASSERT(res == VK_SUCCESS);
        for (uint32_t j = 0; j < executables_count; j++)
        {
            const VkPipelineExecutablePropertiesKHR& exec = executables[j];

            VkPipelineExecutableInfoKHR pipeline_exec_info = { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR };
            pipeline_exec_info.pNext = nullptr;
            pipeline_exec_info.pipeline = pipeline_;
            pipeline_exec_info.executableIndex = j;

            // Internal representations
            uint32_t internal_representation_count = 0;
            res = vkGetPipelineExecutableInternalRepresentationsKHR(device_, &pipeline_exec_info, &internal_representation_count, NULL);
            FFX_SSSR_ASSERT(res == VK_SUCCESS);
            std::vector<VkPipelineExecutableInternalRepresentationKHR> internal_representations(internal_representation_count);
            for (uint32_t i = 0; i < internal_representation_count; i++)
            {
                internal_representations[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR;
            }
            res = vkGetPipelineExecutableInternalRepresentationsKHR(device_, &pipeline_exec_info, &internal_representation_count, internal_representations.data());
            FFX_SSSR_ASSERT(res == VK_SUCCESS);

            // For each VkPipelineExecutableInternalRepresentationKHR we now know the data size --> allocate space for pData and call vkGetPipelineExecutableInternalRepresentationsKHR again.
            std::vector<std::unique_ptr<char[]>> data_pointers(internal_representation_count);
            for (uint32_t i = 0; i < internal_representation_count; i++)
            {
                data_pointers[i] = std::make_unique<char[]>(internal_representations[i].dataSize);
                internal_representations[i].pData = data_pointers[i].get();
            }
            res = vkGetPipelineExecutableInternalRepresentationsKHR(device_, &pipeline_exec_info, &internal_representation_count, internal_representations.data());
            FFX_SSSR_ASSERT(res == VK_SUCCESS);

            for (uint32_t i = 0; i < internal_representation_count; i++)
            {
                filestream.write(data_pointers[i].get(), internal_representations[i].dataSize);
            }
        }

        filestream.close();
#endif // FFX_SSSR_DUMP_SHADERS
    }

}
