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
#include <d3d12.h>
#include <dxcapi.use.h>

#include "sampler_d3d12.h"
#include "reflection_view_d3d12.h"
#include "upload_buffer_d3d12.h"
#include "shader_compiler_d3d12.h"

namespace ffx_sssr
{
    class Context;
    class ReflectionViewD3D12;

    /**
        The ContextD3D12 class encapsulates the data for a single Direct3D12 stochastic screen space reflections execution context.
    */
    class ContextD3D12
    {
        FFX_SSSR_NON_COPYABLE(ContextD3D12);

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

        ContextD3D12(Context& context, FfxSssrCreateContextInfo const& create_context_info);
        ~ContextD3D12();

        inline Context& GetContext();
        inline ID3D12Device* GetDevice() const;
        inline Context const& GetContext() const;
        inline UploadBufferD3D12& GetUploadBuffer();

        inline ShaderD3D12 const& GetShader(Shader shader) const;
        inline BlueNoiseSamplerD3D12 const& GetSampler1SPP() const;
        inline BlueNoiseSamplerD3D12 const& GetSampler2SPP() const;

        void GetReflectionViewTileClassificationElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;
        void GetReflectionViewIntersectionElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;
        void GetReflectionViewDenoisingElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;

        void CreateReflectionView(std::uint64_t reflection_view_id, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);
        void ResolveReflectionView(std::uint64_t reflection_view_id, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info);

        static inline ID3D12Device* GetValidDevice(Context& context, ID3D12Device* device);
        static inline ID3D12GraphicsCommandList* GetCommandList(Context& context, ID3D12GraphicsCommandList* command_list);

    protected:
        friend class Context;
        friend class ReflectionViewD3D12;

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

            inline void SafeRelease();

            // The pipeline state object.
            ID3D12PipelineState* pipeline_state_;
            // The root signature to be used.
            ID3D12RootSignature* root_signature_;
            // The number of descriptors in the root signature.
            std::uint32_t descriptor_count_;
        };

        void CompileShaders(FfxSssrCreateContextInfo const& create_context_info);
        void CreateRootSignatures();
        void CreatePipelineStates();

        const ShaderPass& GetTileClassificationPass() const;
        const ShaderPass& GetIndirectArgsPass() const;
        const ShaderPass& GetIntersectionPass() const;
        const ShaderPass& GetSpatialDenoisingPass() const;
        const ShaderPass& GetTemporalDenoisingPass() const;
        const ShaderPass& GetEawDenoisingPass() const;

        ID3D12CommandSignature* GetIndirectDispatchCommandSignature();

        bool AllocateSRVBuffer(std::size_t buffer_size, ID3D12Resource** resource, D3D12_RESOURCE_STATES initial_resource_state, wchar_t const* resource_name = nullptr) const;
        bool AllocateUAVBuffer(std::size_t buffer_size, ID3D12Resource** resource, D3D12_RESOURCE_STATES initial_resource_state, wchar_t const* resource_name = nullptr) const;
        bool AllocateReadbackBuffer(std::size_t buffer_size, ID3D12Resource** resource, D3D12_RESOURCE_STATES initial_resource_state, wchar_t const* resource_name = nullptr) const;

        // The execution context.
        Context& context_;
        // The device to be used.
        ID3D12Device* device_;
        // The compiled reflections shaders.
        std::array<ShaderD3D12, kShader_Count> shaders_;
        // The compiler to be used for building the Direct3D12 shaders.
        ShaderCompilerD3D12 shader_compiler_;
        // The Blue Noise sampler optimized for 1 sample per pixel.
        BlueNoiseSamplerD3D12 blue_noise_sampler_1spp_;
        // The Blue Noise sampler optimized for 2 samples per pixel.
        BlueNoiseSamplerD3D12 blue_noise_sampler_2spp_;
        // The flag for whether the samplers were populated.
        bool samplers_were_populated_;
        // The buffer to be used for uploading memory from the CPU to the GPU.
        UploadBufferD3D12 upload_buffer_;
        // The array of reflection views to be resolved.
        SparseArray<ReflectionViewD3D12> reflection_views_;

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

        // The command signature for the indirect dispatches.
        ID3D12CommandSignature* indirect_dispatch_command_signature_;
    };
}

#include "context_d3d12.inl"
