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

#include <array>

#include <vulkan/vulkan.h>

#define FFX_SSSR_DUMP_SHADERS 0

#include "sampler_vk.h"
#include "reflection_view_vk.h"
#include "upload_buffer_vk.h"
#include "shader_compiler_vk.h"

namespace ffx_sssr
{
    class Context;
    class ReflectionViewVK;

    /**
        The ContextVK class encapsulates the data for a single Vulkan stochastic screen space reflections execution context.
    */
    class ContextVK
    {
        FFX_SSSR_NON_COPYABLE(ContextVK);

    public:
        /**
            The available shaders.
        */
        enum Shader
        {
            kShader_IndirectArguments,
            kShader_TileClassification,
            kShader_Intersection,
            kShader_SpatialResolve,
            kShader_TemporalResolve,
            kShader_EAWResolve,

            kShader_Count
        };

        ContextVK(Context& context, FfxSssrCreateContextInfo const& create_context_info);
        ~ContextVK();

        inline Context& GetContext();
        inline Context const& GetContext() const;
        
        inline VkDevice GetDevice() const;
        inline VkPhysicalDevice GetPhysicalDevice() const;
        inline UploadBufferVK& GetUploadBuffer();

        inline ShaderVK const& GetShader(Shader shader) const;
        inline BlueNoiseSamplerVK const& GetSampler1SPP() const;
        inline BlueNoiseSamplerVK const& GetSampler2SPP() const;

        void GetReflectionViewTileClassificationElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;
        void GetReflectionViewIntersectionElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;
        void GetReflectionViewDenoisingElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;

        void CreateReflectionView(std::uint64_t reflection_view_id, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);
        void ResolveReflectionView(std::uint64_t reflection_view_id, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info);

    protected:
        friend class Context;
        friend class ReflectionViewVK;

        /**
            The ShaderPass class holds the data for an individual shader pass.
        */
        class ShaderPass
        {
            FFX_SSSR_NON_COPYABLE(ShaderPass);

        public:
            inline ShaderPass();
            inline ~ShaderPass();

            inline operator bool() const;

            inline ShaderPass(ShaderPass&& other) noexcept;
            inline ShaderPass& operator =(ShaderPass&& other) noexcept;

            void DumpInternalRepresentations(const char* path);

            // The device that created the pass.
            VkDevice device_;
            // The pipeline state object.
            VkPipeline pipeline_;
            // The pipeline layout.
            VkPipelineLayout pipeline_layout_;
            // The descriptor set layout.
            VkDescriptorSetLayout descriptor_set_layout_;
            // The number of resource bindings of this pass;
            uint32_t bindings_count_;

        };

        void CompileShaders(FfxSssrCreateContextInfo const& create_context_info);
        void CreatePipelines();

        const ShaderPass& GetTileClassificationPass() const;
        const ShaderPass& GetIndirectArgsPass() const;
        const ShaderPass& GetIntersectionPass() const;
        const ShaderPass& GetSpatialDenoisingPass() const;
        const ShaderPass& GetTemporalDenoisingPass() const;
        const ShaderPass& GetEawDenoisingPass() const;
        VkDescriptorSetLayout GetUniformBufferDescriptorSetLayout() const;

        // The execution context.
        Context& context_;
        // The device to be used.
        VkDevice device_;
        // The physical device to be used.
        VkPhysicalDevice physical_device_;
        // If the VK_EXT_subgroup_size_control extension is available.
        bool is_subgroup_size_control_extension_available_;
        // The compiled reflections shaders.
        std::array<ShaderVK, kShader_Count> shaders_;
        // The compiler to be used for building the Vulkan shaders.
        ShaderCompilerVK shader_compiler_;
        // The Blue Noise sampler optimized for 1 sample per pixel.
        BlueNoiseSamplerVK blue_noise_sampler_1spp_;
        // The Blue Noise sampler optimized for 2 samples per pixel.
        BlueNoiseSamplerVK blue_noise_sampler_2spp_;
        // The flag for whether the samplers were populated.
        bool samplers_were_populated_;
        // The buffer to be used for uploading memory from the CPU to the GPU.
        UploadBufferVK upload_buffer_;
        // The array of reflection views to be resolved.
        SparseArray<ReflectionViewVK> reflection_views_;

        // Same descriptor set layout for all passes.
        VkDescriptorSetLayout uniform_buffer_descriptor_set_layout_;
        // The shader pass that classifies tiles.
        ShaderPass tile_classification_pass_;
        // The shader pass that prepares the indirect arguments.
        ShaderPass indirect_args_pass_;
        // The shader pass intersecting reflection rays with the depth buffer.
        ShaderPass intersection_pass_;
        // The shader pass that does spatial denoising.
        ShaderPass spatial_denoising_pass_;
        // The shader pass that does temporal denoising.
        ShaderPass temporal_denoising_pass_;
        // The shader pass that does the second spatial denoising.
        ShaderPass eaw_denoising_pass_;
    };
}

#include "context_vk.inl"
