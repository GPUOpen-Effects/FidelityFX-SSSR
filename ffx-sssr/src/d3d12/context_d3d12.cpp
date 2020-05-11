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
#include "shader_resolve_eaw_stride.h"
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
        , reflection_views_(create_context_info.maxReflectionViewCount)
    {
        FFX_SSSR_ASSERT(device_ != nullptr);

        struct
        {
            char const* shader_name_ = nullptr;
            char const* content_ = nullptr;
            char const* profile_ = nullptr;
            DxcDefine additional_define_ = {};
        }
        const shader_source[] =
        {
            { "prepare_indirect_args",   prepare_indirect_args,    "cs_6_0"},
            { "classify_tiles",          classify_tiles,           "cs_6_0"},
            { "intersect",               intersect,                "cs_6_0"},
            { "resolve_spatial",         resolve_spatial,          "cs_6_0"},
            { "resolve_temporal",        resolve_temporal,         "cs_6_0"},
            { "resolve_eaw",             resolve_eaw,              "cs_6_0"},
            { "resolve_eaw_stride",      resolve_eaw_stride,       "cs_6_0", {L"FFX_SSSR_EAW_STRIDE", L"2"}},
            { "resolve_eaw_stride",      resolve_eaw_stride,       "cs_6_0", {L"FFX_SSSR_EAW_STRIDE", L"4"}},
        };

        auto const common_include = std::string(common);

        DxcDefine defines[11];
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
            auto const shader = static_cast<Shader>(i);
            defines[10] = shader_source[i].additional_define_;
            ShaderKey const shader_key(shader, 0ull);
            if (shaders_.find(shader_key) == shaders_.end())
            {
                // Append common includes
                shader_content.str(std::string());
                shader_content.clear();
                shader_content << common << std::endl << shader_source[i].content_;
                shaders_[shader_key] = shader_compiler_.CompileShaderString(
                    shader_content.str().c_str(), 
                    static_cast<uint32_t>(shader_content.str().size()), 
                    shader_source[i].shader_name_, 
                    shader_source[i].profile_, 
                    nullptr, 0, 
                    defines, FFX_SSSR_ARRAY_SIZE(defines));
            }
            FFX_SSSR_ASSERT(shaders_[shader_key]); // should never happen as compile throws in case of failure
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
