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
#include "context_d3d12.h"

#include <functional>
#include <sstream>

#include "utils.h"
#include "context.h"
#include "reflection_view.h"
#include "ffx_sssr_d3d12.h"

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

    /**
        Initializes the descriptor range.

        \param range_type The type of the descriptor range.
        \param num_descriptors The number of descriptors in the range.
        \param base_shader_register The base descriptor for the range in shader code.
        \return The resulting descriptor range.
    */
    inline D3D12_DESCRIPTOR_RANGE InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE range_type, std::uint32_t num_descriptors, std::uint32_t base_shader_register)
    {
        D3D12_DESCRIPTOR_RANGE descriptor_range = {};
        descriptor_range.RangeType = range_type;
        descriptor_range.NumDescriptors = num_descriptors;
        descriptor_range.BaseShaderRegister = base_shader_register;
        descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        return descriptor_range;
    }

    /**
        Initializes the root parameter as descriptor table.

        \param num_descriptor_ranges The number of descriptor ranges for this parameter.
        \param descriptor_ranges The array of descriptor ranges for this parameter.
        \return The resulting root parameter.
    */
    inline D3D12_ROOT_PARAMETER InitAsDescriptorTable(std::uint32_t num_descriptor_ranges, D3D12_DESCRIPTOR_RANGE const* descriptor_ranges)
    {
        D3D12_ROOT_PARAMETER root_parameter = {};
        root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameter.DescriptorTable.NumDescriptorRanges = num_descriptor_ranges;
        root_parameter.DescriptorTable.pDescriptorRanges = descriptor_ranges;
        root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // CS
        return root_parameter;
    }

    /**
        Initializes the root parameter as constant buffer view.

        \param shader_register The slot of this constant buffer view.
        \return The resulting root parameter.
    */
    inline D3D12_ROOT_PARAMETER InitAsConstantBufferView(std::uint32_t shader_register)
    {
        D3D12_ROOT_PARAMETER root_parameter = {};
        root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameter.Descriptor.RegisterSpace = 0;
        root_parameter.Descriptor.ShaderRegister = shader_register;
        root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // CS
        return root_parameter;
    }

    /**
        Initializes a linear sampler for a static sampler description.

        \param shader_register The slot of this sampler.
        \return The resulting sampler description.
    */
    inline D3D12_STATIC_SAMPLER_DESC InitLinearSampler(std::uint32_t shader_register)
    {
        D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ShaderRegister = shader_register;
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // Compute
        return samplerDesc;
    }
}

namespace ffx_sssr
{
    /**
        The constructor for the ContextD3D12 class.

        \param context The execution context.
        \param create_context_info The context creation information.
    */
    ContextD3D12::ContextD3D12(Context& context, FfxSssrCreateContextInfo const& create_context_info) : 
        context_(context)
        , device_(GetValidDevice(context, create_context_info.pD3D12CreateContextInfo->pDevice))
        , shader_compiler_(context)
        , samplers_were_populated_(false)
        , upload_buffer_(*this, create_context_info.uploadBufferSize)
        , tile_classification_pass_()
        , indirect_args_pass_()
        , intersection_pass_()
        , spatial_denoising_pass_()
        , temporal_denoising_pass_()
        , eaw_denoising_pass_()
        , indirect_dispatch_command_signature_(nullptr)
        , reflection_views_(create_context_info.maxReflectionViewCount)
    {
        FFX_SSSR_ASSERT(device_ != nullptr);
        CompileShaders(create_context_info);
        CreateRootSignatures();
        CreatePipelineStates();

        // Create command signature for indirect arguments 
        {
            D3D12_INDIRECT_ARGUMENT_DESC dispatch = {};
            dispatch.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

            D3D12_COMMAND_SIGNATURE_DESC desc = {};
            desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
            desc.NodeMask = 0;
            desc.NumArgumentDescs = 1;
            desc.pArgumentDescs = &dispatch;

            HRESULT hr;
            hr = device_->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&indirect_dispatch_command_signature_));
            if (!SUCCEEDED(hr))
            {
                throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create command signature for indirect dispatch.");
            }
        }

        // Create our blue noise samplers
        BlueNoiseSamplerD3D12* blue_noise_samplers[] = { &blue_noise_sampler_1spp_, &blue_noise_sampler_2spp_ };
        static_assert(FFX_SSSR_ARRAY_SIZE(blue_noise_samplers) == FFX_SSSR_ARRAY_SIZE(g_sampler_states), "Sampler arrays don't match.");
        for (auto i = 0u; i < FFX_SSSR_ARRAY_SIZE(g_sampler_states); ++i)
        {
            auto const& sampler_state = g_sampler_states[i];
            BlueNoiseSamplerD3D12* sampler = blue_noise_samplers[i];

            if (!AllocateSRVBuffer(sizeof(sampler_state.sobol_buffer_),
                                   &sampler->sobol_buffer_,
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   L"SSSR Sobol Buffer") ||
                !AllocateSRVBuffer(sizeof(sampler_state.ranking_tile_buffer_),
                                   &sampler->ranking_tile_buffer_,
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   L"SSSR Ranking Tile Buffer") ||
                !AllocateSRVBuffer(sizeof(sampler_state.scrambling_tile_buffer_),
                                   &sampler->scrambling_tile_buffer_,
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   L"SSSR Scrambling Tile Buffer"))
            {
                throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Unable to create SRV buffer(s) for sampler.");
            }
        }

        ID3D12GraphicsCommandList * command_list = create_context_info.pD3D12CreateContextInfo->pUploadCommandList;
        if (!samplers_were_populated_)
        {
            std::int32_t* upload_buffer;

            // Upload the relevant data to the various samplers
            for (auto i = 0u; i < FFX_SSSR_ARRAY_SIZE(g_sampler_states); ++i)
            {
                auto const& sampler_state = g_sampler_states[i];
                BlueNoiseSamplerD3D12* sampler = blue_noise_samplers[i];

                FFX_SSSR_ASSERT(sampler->sobol_buffer_);
                FFX_SSSR_ASSERT(sampler->ranking_tile_buffer_);
                FFX_SSSR_ASSERT(sampler->scrambling_tile_buffer_);

                if (!upload_buffer_.AllocateBuffer(sizeof(sampler_state.sobol_buffer_), upload_buffer))
                {
                    throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %llukiB of upload memory, consider increasing uploadBufferSize", RoundedDivide(sizeof(sampler_state.sobol_buffer_), 1024ull));
                }
                memcpy(upload_buffer, sampler_state.sobol_buffer_, sizeof(sampler_state.sobol_buffer_));

                command_list->CopyBufferRegion(sampler->sobol_buffer_,
                    0ull,
                    upload_buffer_.GetResource(),
                    static_cast<UINT64>(upload_buffer_.GetOffset(upload_buffer)),
                    sizeof(sampler_state.sobol_buffer_));

                if (!upload_buffer_.AllocateBuffer(sizeof(sampler_state.ranking_tile_buffer_), upload_buffer))
                {
                    throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %llukiB of upload memory, consider increasing uploadBufferSize", RoundedDivide(sizeof(sampler_state.ranking_tile_buffer_), 1024ull));
                }
                memcpy(upload_buffer, sampler_state.ranking_tile_buffer_, sizeof(sampler_state.ranking_tile_buffer_));

                command_list->CopyBufferRegion(sampler->ranking_tile_buffer_,
                    0ull,
                    upload_buffer_.GetResource(),
                    static_cast<UINT64>(upload_buffer_.GetOffset(upload_buffer)),
                    sizeof(sampler_state.ranking_tile_buffer_));

                if (!upload_buffer_.AllocateBuffer(sizeof(sampler_state.scrambling_tile_buffer_), upload_buffer))
                {
                    throw reflection_error(context_, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %llukiB of upload memory, consider increasing uploadBufferSize", RoundedDivide(sizeof(sampler_state.scrambling_tile_buffer_), 1024ull));
                }
                memcpy(upload_buffer, sampler_state.scrambling_tile_buffer_, sizeof(sampler_state.scrambling_tile_buffer_));

                command_list->CopyBufferRegion(sampler->scrambling_tile_buffer_,
                    0ull,
                    upload_buffer_.GetResource(),
                    static_cast<UINT64>(upload_buffer_.GetOffset(upload_buffer)),
                    sizeof(sampler_state.scrambling_tile_buffer_));
            }

            // Transition the resources for usage
            D3D12_RESOURCE_BARRIER resource_barriers[3 * FFX_SSSR_ARRAY_SIZE(g_sampler_states)];
            memset(resource_barriers, 0, sizeof(resource_barriers));

            for (auto i = 0u; i < FFX_SSSR_ARRAY_SIZE(g_sampler_states); ++i)
            {
                BlueNoiseSamplerD3D12* sampler = blue_noise_samplers[i];

                auto& sobol_buffer_resource_barrier = resource_barriers[3u * i + 0u];
                sobol_buffer_resource_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                sobol_buffer_resource_barrier.Transition.pResource = sampler->sobol_buffer_;
                sobol_buffer_resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                sobol_buffer_resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                sobol_buffer_resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                auto& ranking_tile_buffer_resource_barrier = resource_barriers[3u * i + 1u];
                ranking_tile_buffer_resource_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                ranking_tile_buffer_resource_barrier.Transition.pResource = sampler->ranking_tile_buffer_;
                ranking_tile_buffer_resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                ranking_tile_buffer_resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                ranking_tile_buffer_resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                auto& scrambling_tile_buffer_resource_barrier = resource_barriers[3u * i + 2u];
                scrambling_tile_buffer_resource_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                scrambling_tile_buffer_resource_barrier.Transition.pResource = sampler->scrambling_tile_buffer_;
                scrambling_tile_buffer_resource_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                scrambling_tile_buffer_resource_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                scrambling_tile_buffer_resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }

            command_list->ResourceBarrier(FFX_SSSR_ARRAY_SIZE(resource_barriers),
                resource_barriers);

            // Flag that the samplers are now ready to use
            samplers_were_populated_ = true;
        }
    }

    /**
        The destructor for the ContextD3D12 class.
    */
    ContextD3D12::~ContextD3D12()
    {
        if (indirect_dispatch_command_signature_) 
            indirect_dispatch_command_signature_->Release();
        indirect_dispatch_command_signature_ = nullptr;
    }

    /**
        Gets the number of GPU ticks spent in the tile classification pass.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent in the tile classification pass.
    */
    void ContextD3D12::GetReflectionViewTileClassificationElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
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
    void ContextD3D12::GetReflectionViewIntersectionElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
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
        Gets the number of GPU ticks spent denoising the Direct3D12 reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent denoising.
    */
    void ContextD3D12::GetReflectionViewDenoisingElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
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
        Creates the Direct3D12 reflection view.

        \param reflection_view_id The identifier of the reflection view object.
        \param create_reflection_view_info The reflection view creation information.
    */
    void ContextD3D12::CreateReflectionView(std::uint64_t reflection_view_id, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info)
    {
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo);
        FFX_SSSR_ASSERT(context_.IsOfType<kResourceType_ReflectionView>(reflection_view_id) && context_.IsObjectValid(reflection_view_id));

        // Check user arguments
        if (!create_reflection_view_info.outputWidth || !create_reflection_view_info.outputHeight)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The outputWidth and outputHeight parameters are required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->depthBufferHierarchySRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The depthBufferHierarchySRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->motionBufferSRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The motionBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->normalBufferSRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The normalBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->roughnessBufferSRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The roughnessBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->normalHistoryBufferSRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The normalHistoryBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->roughnessHistoryBufferSRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The roughnessHistoryBufferSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->reflectionViewUAV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The reflectionViewUAV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->environmentMapSRV.ptr)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The environmentMapSRV parameter is required when creating a reflection view");
        if (!create_reflection_view_info.pD3D12CreateReflectionViewInfo->pEnvironmentMapSamplerDesc)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The pEnvironmentMapSamplerDesc parameter is required when creating a reflection view");
        if(create_reflection_view_info.pD3D12CreateReflectionViewInfo->sceneFormat == DXGI_FORMAT_UNKNOWN)
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_VALUE, "The sceneFormat parameter is required when creating a reflection view");

        // Create the reflection view
        auto& reflection_view = reflection_views_.Insert(ID(reflection_view_id));
        reflection_view.Create(context_, create_reflection_view_info);
    }

    /**
        Resolves the Direct3D12 reflection view.

        \param reflection_view_id The identifier of the reflection view object.
        \param resolve_reflection_view_info The reflection view resolve information.
    */
    void ContextD3D12::ResolveReflectionView(std::uint64_t reflection_view_id, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info)
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

    void ContextD3D12::CompileShaders(FfxSssrCreateContextInfo const& create_context_info)
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
                nullptr, 0,
                defines, FFX_SSSR_ARRAY_SIZE(defines));
        }
    }

    void ContextD3D12::CreateRootSignatures()
    {
        auto CreateRootSignature = [this](
            ShaderPass& pass
            , const LPCWSTR name
            , std::uint32_t num_descriptor_ranges
            , D3D12_DESCRIPTOR_RANGE const* descriptor_ranges
            ) {

                D3D12_DESCRIPTOR_RANGE environment_map_sampler_range = {};
                environment_map_sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                environment_map_sampler_range.NumDescriptors = 1;
                environment_map_sampler_range.BaseShaderRegister = 1;
                environment_map_sampler_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                D3D12_ROOT_PARAMETER root[] = {
                    InitAsDescriptorTable(num_descriptor_ranges, descriptor_ranges),
                    InitAsConstantBufferView(0),
                    InitAsDescriptorTable(1, &environment_map_sampler_range), // g_environment_map_sampler
                };

                D3D12_STATIC_SAMPLER_DESC sampler_descs[] = { InitLinearSampler(0) }; // g_linear_sampler

                D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
                rs_desc.NumParameters = FFX_SSSR_ARRAY_SIZE(root);
                rs_desc.pParameters = root;
                rs_desc.NumStaticSamplers = FFX_SSSR_ARRAY_SIZE(sampler_descs);
                rs_desc.pStaticSamplers = sampler_descs;

                HRESULT hr;
                ID3DBlob* rs, * rsError;
                hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &rsError);
                if (FAILED(hr))
                {
                    if (rsError)
                    {
                        std::string const error_message(static_cast<char const*>(rsError->GetBufferPointer()));
                        rsError->Release();
                        throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to serialize root signature:\r\n> %s", error_message.c_str());
                    }
                    else
                    {
                        throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to serialize root signature");
                    }
                }

                hr = GetDevice()->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), IID_PPV_ARGS(&pass.root_signature_));
                rs->Release();
                if (FAILED(hr))
                {
                    throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create root signature.");
                }

                pass.root_signature_->SetName(name);
                pass.descriptor_count_ = num_descriptor_ranges;
        };

        // Assemble the shader pass for tile classification
        {
            D3D12_DESCRIPTOR_RANGE ranges[] = {
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0), // g_roughness
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0), // g_tile_list
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1), // g_ray_list
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2), // g_tile_counter
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3), // g_ray_counter
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4), // g_temporally_denoised_reflections
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5), // g_temporally_denoised_reflections_history
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 6), // g_ray_lengths
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7), // g_temporal_variance
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 8), // g_denoised_reflections
            };
            CreateRootSignature(tile_classification_pass_, L"SSSR Tile Classification Root Signature", FFX_SSSR_ARRAY_SIZE(ranges), ranges);
        }

        // Assemble the shader pass that prepares the indirect arguments
        {
            D3D12_DESCRIPTOR_RANGE ranges[] = {
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0), // g_tile_counter
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1), // g_ray_counter
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2), // g_intersect_args
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3), // g_denoiser_args
            };
            CreateRootSignature(indirect_args_pass_, L"SSSR Indirect Arguments Pass Root Signature", FFX_SSSR_ARRAY_SIZE(ranges), ranges);
        }

        // Assemble the shader pass for intersecting reflection rays with the depth buffer
        {
            D3D12_DESCRIPTOR_RANGE ranges[] = {
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0), // g_lit_scene
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1), // g_depth_buffer_hierarchy
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2), // g_normal
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3), // g_roughness
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4), // g_environment_map
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5), // g_sobol_buffer
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6), // g_ranking_tile_buffer
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7), // g_scrambling_tile_buffer
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8), // g_ray_list
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0), // g_intersection_result
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1), // g_ray_lengths
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2), // g_denoised_reflections

            };
            CreateRootSignature(intersection_pass_, L"SSSR Depth Buffer Intersection Root Signature", FFX_SSSR_ARRAY_SIZE(ranges), ranges);
        }

        // Assemble the shader pass for spatial resolve
        {
            D3D12_DESCRIPTOR_RANGE ranges[] = {
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0), // g_depth_buffer
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1), // g_normal
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2), // g_roughness
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3), // g_intersection_result
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4), // g_has_ray
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5), // g_tile_list
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0), // g_spatially_denoised_reflections
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1), // g_ray_lengths
            };
            CreateRootSignature(spatial_denoising_pass_, L"SSSR Spatial Resolve Root Signature", FFX_SSSR_ARRAY_SIZE(ranges), ranges);
        }

        // Assemble the shader pass for temporal resolve
        {
            D3D12_DESCRIPTOR_RANGE ranges[] = {
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0), // g_normal
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1), // g_roughness
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2), // g_normal_history
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3), // g_roughness_history
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4), // g_depth_buffer
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5), // g_motion_vectors
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6), // g_temporally_denoised_reflections_history
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7), // g_ray_lengths
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8), // g_tile_list
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0), // g_temporally_denoised_reflections
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1), // g_spatially_denoised_reflections
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2), // g_temporal_variance
            };
            CreateRootSignature(temporal_denoising_pass_, L"SSSR Temporal Resolve Root Signature", FFX_SSSR_ARRAY_SIZE(ranges), ranges);
        }

        // Assemble the shader pass for EAW resolve
        {
            D3D12_DESCRIPTOR_RANGE ranges[] = {
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0), // g_normal
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1), // g_roughness
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2), // g_depth_buffer
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3), // g_tile_list
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0), // g_temporally_denoised_reflections
                InitDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1), // g_denoised_reflections
            };
            CreateRootSignature(eaw_denoising_pass_, L"SSSR EAW Resolve Root Signature", FFX_SSSR_ARRAY_SIZE(ranges), ranges);
        }
    }

    void ContextD3D12::CreatePipelineStates()
    {
        auto Compile = [this](ShaderPass& pass, ContextD3D12::Shader shader, const LPCWSTR name) {
            FFX_SSSR_ASSERT(pass.root_signature_ != nullptr);

            // Create the pipeline state object
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
            pipeline_state_desc.pRootSignature = pass.root_signature_;
            pipeline_state_desc.CS = GetShader(shader);

            HRESULT hr = GetDevice()->CreateComputePipelineState(&pipeline_state_desc,
                IID_PPV_ARGS(&pass.pipeline_state_));
            if (!SUCCEEDED(hr))
            {
                throw reflection_error(GetContext(), FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create compute pipeline state");
            }

            pass.pipeline_state_->SetName(name);
        };

        Compile(tile_classification_pass_, ContextD3D12::kShader_TileClassification, L"SSSR Tile Classification Pipeline");
        Compile(indirect_args_pass_, ContextD3D12::kShader_IndirectArguments, L"SSSR Indirect Arguments Pipeline");
        Compile(intersection_pass_, ContextD3D12::kShader_Intersection, L"SSSR Intersect Pipeline");
        Compile(spatial_denoising_pass_, ContextD3D12::kShader_SpatialResolve, L"SSSR Spatial Resolve Pipeline");
        Compile(temporal_denoising_pass_, ContextD3D12::kShader_TemporalResolve, L"SSSR Temporal Resolve Pipeline");
        Compile(eaw_denoising_pass_, ContextD3D12::kShader_EAWResolve, L"SSSR EAW Resolve Pipeline");
    }

    const ContextD3D12::ShaderPass& ContextD3D12::GetTileClassificationPass() const
    {
        return tile_classification_pass_;
    }

    const ContextD3D12::ShaderPass& ContextD3D12::GetIndirectArgsPass() const
    {
        return indirect_args_pass_;
    }

    const ContextD3D12::ShaderPass& ContextD3D12::GetIntersectionPass() const
    {
        return intersection_pass_;
    }

    const ContextD3D12::ShaderPass& ContextD3D12::GetSpatialDenoisingPass() const
    {
        return spatial_denoising_pass_;
    }

    const ContextD3D12::ShaderPass& ContextD3D12::GetTemporalDenoisingPass() const
    {
        return temporal_denoising_pass_;
    }

    const ContextD3D12::ShaderPass& ContextD3D12::GetEawDenoisingPass() const
    {
        return eaw_denoising_pass_;
    }

    ID3D12CommandSignature* ContextD3D12::GetIndirectDispatchCommandSignature()
    {
        return indirect_dispatch_command_signature_;
    }

    /**
        Allocate a buffer resource to use as a shader resource view.

        \param buffer_size The size of the buffer (in bytes).
        \param resource The created SRV buffer resource.
        \param initial_resource_state The initial resource state.
        \param resource_name An optional name for the resource.
        \return true if the resource was allocated successfully.
    */
    bool ContextD3D12::AllocateSRVBuffer(std::size_t buffer_size, ID3D12Resource** resource, D3D12_RESOURCE_STATES initial_resource_state, wchar_t const* resource_name) const
    {
        FFX_SSSR_ASSERT(resource != nullptr);

        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CreationNodeMask = 1u;
        heap_properties.VisibleNodeMask = 1u;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Width = static_cast<UINT64>(buffer_size);
        resource_desc.Height = 1u;
        resource_desc.DepthOrArraySize = 1u;
        resource_desc.MipLevels = 1u;
        resource_desc.SampleDesc.Count = 1u;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (!SUCCEEDED(device_->CreateCommittedResource(&heap_properties,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &resource_desc,
                                                        initial_resource_state,
                                                        nullptr,
                                                        IID_PPV_ARGS(resource))))
        {
            return false;   // failed to create committed resource
        }

        if (resource_name)
        {
            (*resource)->SetName(resource_name);
        }

        return true;
    }

    /**
        Allocate a buffer resource to use as an unordered access view.

        \param buffer_size The size of the buffer (in bytes).
        \param resource The created UAV buffer resource.
        \param initial_resource_state The initial resource state.
        \param resource_name An optional name for the resource.
        \return true if the resource was allocated successfully.
    */
    bool ContextD3D12::AllocateUAVBuffer(std::size_t buffer_size, ID3D12Resource** resource, D3D12_RESOURCE_STATES initial_resource_state, wchar_t const* resource_name) const
    {
        FFX_SSSR_ASSERT(resource != nullptr);

        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CreationNodeMask = 1u;
        heap_properties.VisibleNodeMask = 1u;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Width = static_cast<UINT64>(buffer_size);
        resource_desc.Height = 1u;
        resource_desc.DepthOrArraySize = 1u;
        resource_desc.MipLevels = 1u;
        resource_desc.SampleDesc.Count = 1u;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        if (!SUCCEEDED(device_->CreateCommittedResource(&heap_properties,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &resource_desc,
                                                        initial_resource_state,
                                                        nullptr,
                                                        IID_PPV_ARGS(resource))))
        {
            return false;   // failed to create committed resource
        }

        if (resource_name)
        {
            (*resource)->SetName(resource_name);
        }

        return true;
    }

    /**
        Allocate a buffer resource to use as a readback resource.

        \param buffer_size The size of the buffer (in bytes).
        \param resource The created readback buffer resource.
        \param initial_resource_state The initial resource state.
        \param resource_name An optional name for the resource.
        \return true if the resource was allocated successfully.
    */
    bool ContextD3D12::AllocateReadbackBuffer(std::size_t buffer_size, ID3D12Resource** resource, D3D12_RESOURCE_STATES initial_resource_state, wchar_t const* resource_name) const
    {
        FFX_SSSR_ASSERT(resource != nullptr);

        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
        heap_properties.CreationNodeMask = 1u;
        heap_properties.VisibleNodeMask = 1u;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Width = static_cast<UINT64>(buffer_size);
        resource_desc.Height = 1u;
        resource_desc.DepthOrArraySize = 1u;
        resource_desc.MipLevels = 1u;
        resource_desc.SampleDesc.Count = 1u;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (!SUCCEEDED(device_->CreateCommittedResource(&heap_properties,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &resource_desc,
                                                        initial_resource_state,
                                                        nullptr,
                                                        IID_PPV_ARGS(resource))))
        {
            return false;   // failed to create committed resource
        }

        if (resource_name)
        {
            (*resource)->SetName(resource_name);
        }

        return true;
    }
}
