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
#include "reflection_view_d3d12.h"

#include <string>
#include <array>

#include "context.h"
#include "reflection_error.h"
#include "reflection_view.h"
#include "context_d3d12.h"
#include "ffx_sssr_d3d12.h"
#include "descriptor_heap_d3d12.h"

namespace ffx_sssr
{
    /**
        The constructor for the ReflectionViewD3D12 class.
    */
    ReflectionViewD3D12::ReflectionViewD3D12()
        : width_(0)
        , height_(0)
        , flags_(0)
        , descriptor_heap_cbv_srv_uav_(nullptr)
        , descriptor_heap_samplers_(nullptr)
        , resource_heap_(nullptr)
        , tile_list_(nullptr)
        , tile_counter_(nullptr)
        , ray_list_(nullptr)
        , ray_counter_(nullptr)
        , intersection_pass_indirect_args_(nullptr)
        , denoiser_pass_indirect_args_(nullptr)
        , temporal_denoiser_result_()
        , ray_lengths_(nullptr)
        , temporal_variance_(nullptr)
        , tile_classification_elapsed_time_(0)
        , intersection_elapsed_time_(0)
        , denoising_elapsed_time_(0)
        , timestamp_query_heap_(nullptr)
        , timestamp_query_buffer_(nullptr)
        , timestamp_queries_()
        , timestamp_queries_index_(0)
        , scene_format_(DXGI_FORMAT_UNKNOWN)
        , tile_classification_descriptor_table_()
        , indirect_args_descriptor_table_()
        , intersection_descriptor_table_()
        , spatial_denoising_descriptor_table_()
        , temporal_denoising_descriptor_table_()
        , eaw_denoising_descriptor_table_()
        , sampler_descriptor_table_()
        , prev_view_projection_()
    {
    }

    /**
        The constructor for the ReflectionViewD3D12 class.

        \param other The reflection view to be moved.
    */
    ReflectionViewD3D12::ReflectionViewD3D12(ReflectionViewD3D12&& other) noexcept
        : width_(other.width_)
        , height_(other.height_)
        , flags_(other.flags_)
        , descriptor_heap_cbv_srv_uav_(other.descriptor_heap_cbv_srv_uav_)
        , descriptor_heap_samplers_(other.descriptor_heap_samplers_)
        , tile_classification_elapsed_time_(other.tile_classification_elapsed_time_)
        , intersection_elapsed_time_(other.intersection_elapsed_time_)
        , denoising_elapsed_time_(other.denoising_elapsed_time_)
        , timestamp_query_heap_(other.timestamp_query_heap_)
        , timestamp_query_buffer_(other.timestamp_query_buffer_)
        , timestamp_queries_(std::move(other.timestamp_queries_))
        , timestamp_queries_index_(other.timestamp_queries_index_)
        , resource_heap_(other.resource_heap_)
        , tile_list_(other.tile_list_)
        , tile_counter_(other.tile_counter_)
        , ray_list_(other.ray_list_)
        , ray_counter_(other.ray_counter_)
        , intersection_pass_indirect_args_(other.intersection_pass_indirect_args_)
        , denoiser_pass_indirect_args_(other.denoiser_pass_indirect_args_)
        , ray_lengths_(other.ray_lengths_)
        , temporal_variance_(other.temporal_variance_)
        , scene_format_(other.scene_format_)
        , prev_view_projection_(other.prev_view_projection_)
    {
        other.timestamp_query_heap_ = nullptr;
        other.timestamp_query_buffer_ = nullptr;
        other.descriptor_heap_cbv_srv_uav_ = nullptr;
        other.descriptor_heap_samplers_ = nullptr;

        for (int i = 0; i < 2; ++i)
        {
            temporal_denoiser_result_[i] = other.temporal_denoiser_result_[i];
            tile_classification_descriptor_table_[i] = other.tile_classification_descriptor_table_[i];
            indirect_args_descriptor_table_[i] = other.indirect_args_descriptor_table_[i];
            intersection_descriptor_table_[i] = other.intersection_descriptor_table_[i];
            spatial_denoising_descriptor_table_[i] = other.spatial_denoising_descriptor_table_[i];
            temporal_denoising_descriptor_table_[i] = other.temporal_denoising_descriptor_table_[i];
            eaw_denoising_descriptor_table_[i] = other.eaw_denoising_descriptor_table_[i];
            other.temporal_denoiser_result_[i] = nullptr;
        }
        sampler_descriptor_table_ = other.sampler_descriptor_table_;

        other.resource_heap_ = nullptr;
        other.tile_list_ = nullptr;
        other.tile_counter_ = nullptr;
        other.ray_list_ = nullptr;
        other.ray_counter_ = nullptr;
        other.intersection_pass_indirect_args_ = nullptr;
        other.denoiser_pass_indirect_args_ = nullptr;
        other.ray_lengths_ = nullptr;
        other.temporal_variance_ = nullptr;
        other.timestamp_query_buffer_ = nullptr;
        other.timestamp_query_heap_ = nullptr;
    }

    /**
        The destructor for the ReflectionViewD3D12 class.
    */
    ReflectionViewD3D12::~ReflectionViewD3D12()
    {
        Destroy();
    }

    /**
        Assigns the reflection view.

        \param other The reflection view to be moved.
        \return The assigned reflection view.
    */
    ReflectionViewD3D12& ReflectionViewD3D12::operator =(ReflectionViewD3D12&& other) noexcept
    {
        if (this != &other)
        {
            width_ = other.width_;
            height_ = other.height_;
            flags_ = other.flags_;

            descriptor_heap_cbv_srv_uav_ = other.descriptor_heap_cbv_srv_uav_;
            descriptor_heap_samplers_ = other.descriptor_heap_samplers_;
            tile_classification_elapsed_time_ = other.tile_classification_elapsed_time_;
            intersection_elapsed_time_ = other.intersection_elapsed_time_;
            denoising_elapsed_time_ = other.denoising_elapsed_time_;
            timestamp_query_heap_ = other.timestamp_query_heap_;
            timestamp_query_buffer_ = other.timestamp_query_buffer_;
            timestamp_queries_ = other.timestamp_queries_;;
            timestamp_queries_index_ = other.timestamp_queries_index_;
            resource_heap_ = other.resource_heap_;
            tile_list_ = other.tile_list_;
            tile_counter_ = other.tile_counter_;
            ray_list_ = other.ray_list_;
            ray_counter_ = other.ray_counter_;
            intersection_pass_indirect_args_ = other.intersection_pass_indirect_args_;
            denoiser_pass_indirect_args_ = other.denoiser_pass_indirect_args_;
            ray_lengths_ = other.ray_lengths_;
            temporal_variance_ = other.temporal_variance_;
            scene_format_ = other.scene_format_;
            prev_view_projection_ = other.prev_view_projection_;

            other.timestamp_query_heap_ = nullptr;
            other.timestamp_query_buffer_ = nullptr;
            other.descriptor_heap_cbv_srv_uav_ = nullptr;
            other.descriptor_heap_samplers_ = nullptr;

            for (int i = 0; i < 2; ++i)
            {
                temporal_denoiser_result_[i] = other.temporal_denoiser_result_[i];
                tile_classification_descriptor_table_[i] = other.tile_classification_descriptor_table_[i];
                indirect_args_descriptor_table_[i] = other.indirect_args_descriptor_table_[i];
                intersection_descriptor_table_[i] = other.intersection_descriptor_table_[i];
                spatial_denoising_descriptor_table_[i] = other.spatial_denoising_descriptor_table_[i];
                temporal_denoising_descriptor_table_[i] = other.temporal_denoising_descriptor_table_[i];
                eaw_denoising_descriptor_table_[i] = other.eaw_denoising_descriptor_table_[i];

                other.temporal_denoiser_result_[i] = nullptr;
            }
            sampler_descriptor_table_ = other.sampler_descriptor_table_;

            other.resource_heap_ = nullptr;
            other.tile_list_ = nullptr;
            other.tile_counter_ = nullptr;
            other.ray_list_ = nullptr;
            other.ray_counter_ = nullptr;
            other.intersection_pass_indirect_args_ = nullptr;
            other.denoiser_pass_indirect_args_ = nullptr;
            other.ray_lengths_ = nullptr;
            other.temporal_variance_ = nullptr;
            other.timestamp_query_buffer_ = nullptr;
            other.timestamp_query_heap_ = nullptr;
        }

        return *this;
    }

    /**
        Creates the reflection view.

        \param context The context to be used.
        \param create_reflection_view_info The reflection view creation information.
    */
    void ReflectionViewD3D12::Create(Context& context, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info)
    {
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo != nullptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->sceneFormat != DXGI_FORMAT_UNKNOWN);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->depthBufferHierarchySRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->motionBufferSRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->normalBufferSRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->roughnessBufferSRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->normalHistoryBufferSRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->roughnessHistoryBufferSRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->environmentMapSRV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->pEnvironmentMapSamplerDesc);
        FFX_SSSR_ASSERT(create_reflection_view_info.pD3D12CreateReflectionViewInfo->reflectionViewUAV.ptr);
        FFX_SSSR_ASSERT(create_reflection_view_info.outputWidth && create_reflection_view_info.outputHeight);

        // Populate the reflection view properties
        width_ = create_reflection_view_info.outputWidth;
        height_ = create_reflection_view_info.outputHeight;
        flags_ = create_reflection_view_info.flags;
        scene_format_ = create_reflection_view_info.pD3D12CreateReflectionViewInfo->sceneFormat;

        // Create reflection view resources
        CreateDescriptorHeaps(context);

        // Create tile classification-related buffers
        {
            ID3D12Device * device = context.GetContextD3D12()->GetDevice();

            uint32_t num_tiles = RoundedDivide(width_, 8u) * RoundedDivide(height_, 8u);
            uint32_t num_pixels = width_ * height_;

            uint32_t tile_list_element_count = num_tiles;
            uint32_t tile_counter_element_count = 1;
            uint32_t ray_list_element_count = num_pixels;
            uint32_t ray_counter_element_count = 1;
            uint32_t intersection_pass_indirect_args_element_count = 3;
            uint32_t denoiser_pass_indirect_args_element_count = 3;

            // Helper function to create resource descriptions for 1D Buffers
            auto BufferDesc = [](uint32_t num_elements) {
                D3D12_RESOURCE_DESC desc = {};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Alignment = 0;
                desc.Width = num_elements * 4;
                desc.Height = 1;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_UNKNOWN;
                desc.SampleDesc.Count = 1;
                desc.SampleDesc.Quality = 0;
                desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                return desc;
            };

            D3D12_RESOURCE_DESC tile_list_desc = BufferDesc(num_tiles);
            D3D12_RESOURCE_DESC tile_counter_desc = BufferDesc(1);
            D3D12_RESOURCE_DESC ray_list_desc = BufferDesc(num_pixels);
            D3D12_RESOURCE_DESC ray_counter_desc = BufferDesc(1);
            constexpr uint32_t indirect_arguments_member_count = 3;
            static_assert(sizeof(D3D12_DISPATCH_ARGUMENTS) == indirect_arguments_member_count * 4, "Size of indirect arguments buffer does not match D3D12_DISPATCH_ARGUMENTS.");
            D3D12_RESOURCE_DESC intersection_pass_indirect_args_desc = BufferDesc(3);
            D3D12_RESOURCE_DESC denoiser_pass_indirect_args_desc = BufferDesc(3);

            D3D12_RESOURCE_DESC resource_descs[] = {
                tile_list_desc, tile_counter_desc, ray_list_desc, ray_counter_desc, intersection_pass_indirect_args_desc, denoiser_pass_indirect_args_desc
            };

            D3D12_RESOURCE_ALLOCATION_INFO allocation_info = device->GetResourceAllocationInfo(0, FFX_SSSR_ARRAY_SIZE(resource_descs), resource_descs);
            D3D12_HEAP_DESC heap_desc = {};
            heap_desc.Alignment = allocation_info.Alignment;
            heap_desc.SizeInBytes = allocation_info.SizeInBytes;
            heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            heap_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heap_desc.Properties.CreationNodeMask = 0;
            heap_desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            heap_desc.Properties.VisibleNodeMask = 0;

            HRESULT hr = device->CreateHeap(&heap_desc, IID_PPV_ARGS(&resource_heap_));
            if (!SUCCEEDED(hr))
            {
                throw reflection_error(context, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to create resource heap.");
            }

            UINT64 heap_offset = 0;
            auto CreatePlacedResource = [this, &context, &heap_offset, &allocation_info](
                D3D12_RESOURCE_DESC * desc
                , D3D12_RESOURCE_STATES initial_state
                , REFIID riidResource
                , _COM_Outptr_opt_  void **ppvResource)
            {
                ID3D12Device * device = context.GetContextD3D12()->GetDevice();
                HRESULT hr = device->CreatePlacedResource(resource_heap_, heap_offset, desc, initial_state, nullptr, riidResource, ppvResource);
                if (!SUCCEEDED(hr))
                {
                    throw reflection_error(context, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Failed to create placed resource.");
                }

                heap_offset += desc->Width;
                heap_offset = RoundedDivide(heap_offset, allocation_info.Alignment) * allocation_info.Alignment;
            };

            CreatePlacedResource(&tile_list_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, IID_PPV_ARGS(&tile_list_));
            CreatePlacedResource(&tile_counter_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, IID_PPV_ARGS(&tile_counter_));
            CreatePlacedResource(&ray_list_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, IID_PPV_ARGS(&ray_list_));
            CreatePlacedResource(&ray_counter_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, IID_PPV_ARGS(&ray_counter_));
            CreatePlacedResource(&intersection_pass_indirect_args_desc, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, IID_PPV_ARGS(&intersection_pass_indirect_args_));
            CreatePlacedResource(&denoiser_pass_indirect_args_desc, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, IID_PPV_ARGS(&denoiser_pass_indirect_args_));

            tile_list_->SetName(L"SSSR Tile List");
            tile_counter_->SetName(L"SSSR Tile Counter");
            ray_list_->SetName(L"SSSR Ray List");
            ray_counter_->SetName(L"SSSR Ray Counter");
            intersection_pass_indirect_args_->SetName(L"SSSR Intersect Indirect Args");
            denoiser_pass_indirect_args_->SetName(L"SSSR Denoiser Indirect Args");
        }
        
        // Create denoising-related resources
        {
            auto CreateCommittedResource = [this, &context](
                DXGI_FORMAT format
                , REFIID riidResource
                , _COM_Outptr_opt_  void **ppvResource) {
                HRESULT hr;
                ID3D12Device * device = context.GetContextD3D12()->GetDevice();
                D3D12_HEAP_PROPERTIES default_heap = {};
                default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                default_heap.CreationNodeMask = 1;
                default_heap.VisibleNodeMask = 1;

                D3D12_RESOURCE_DESC desc = {};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Alignment = 0;
                desc.Width = width_;
                desc.Height = height_;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 0;
                desc.Format = format;
                desc.SampleDesc.Count = 1;
                desc.SampleDesc.Quality = 0;
                desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                hr = device->CreateCommittedResource(
                    &default_heap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    NULL,
                    riidResource, ppvResource);

                if (!SUCCEEDED(hr))
                {
                    throw reflection_error(context, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to create intermediate target.");
                }
            };

            CreateCommittedResource(scene_format_, IID_PPV_ARGS(&temporal_denoiser_result_[0]));
            CreateCommittedResource(scene_format_, IID_PPV_ARGS(&temporal_denoiser_result_[1]));
            CreateCommittedResource(DXGI_FORMAT_R16_FLOAT, IID_PPV_ARGS(&ray_lengths_));
            CreateCommittedResource(DXGI_FORMAT_R8_UNORM, IID_PPV_ARGS(&temporal_variance_));

            temporal_denoiser_result_[0]->SetName(L"SSSR Temporal Denoised Result 0");
            temporal_denoiser_result_[1]->SetName(L"SSSR Temporal Denoised Result 1");
            ray_lengths_->SetName(L"SSSR Ray Lengths");
            temporal_variance_->SetName(L"SSSR Temporal Variance");
        }

        ContextD3D12* d3d12_context = context.GetContextD3D12();

        // Setup the descriptor tables
        {
            descriptor_heap_samplers_->AllocateStaticDescriptor(sampler_descriptor_table_, 1);

            // Suballocate descriptor heap for descriptor tables
            for (int i = 0; i < 2; ++i)
            {
                DescriptorD3D12 table;
                descriptor_heap_cbv_srv_uav_->AllocateStaticDescriptor(table, d3d12_context->GetTileClassificationPass().descriptor_count_);
                tile_classification_descriptor_table_[i] = table;

                descriptor_heap_cbv_srv_uav_->AllocateStaticDescriptor(table, d3d12_context->GetIndirectArgsPass().descriptor_count_);
                indirect_args_descriptor_table_[i] = table;

                descriptor_heap_cbv_srv_uav_->AllocateStaticDescriptor(table, d3d12_context->GetIntersectionPass().descriptor_count_);
                intersection_descriptor_table_[i] = table;

                descriptor_heap_cbv_srv_uav_->AllocateStaticDescriptor(table, d3d12_context->GetSpatialDenoisingPass().descriptor_count_);
                spatial_denoising_descriptor_table_[i] = table;

                descriptor_heap_cbv_srv_uav_->AllocateStaticDescriptor(table, d3d12_context->GetTemporalDenoisingPass().descriptor_count_);
                temporal_denoising_descriptor_table_[i] = table;

                descriptor_heap_cbv_srv_uav_->AllocateStaticDescriptor(table, d3d12_context->GetEawDenoisingPass().descriptor_count_);
                eaw_denoising_descriptor_table_[i] = table;
            }

            ID3D12Device * device = context.GetContextD3D12()->GetDevice();
            UINT descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            D3D12_CPU_DESCRIPTOR_HANDLE scene_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->sceneSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE depth_hierarchy_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->depthBufferHierarchySRV;
            D3D12_CPU_DESCRIPTOR_HANDLE motion_buffer_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->motionBufferSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE normal_buffer_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->normalBufferSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE roughness_buffer_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->roughnessBufferSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE normal_history_buffer_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->normalHistoryBufferSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE roughness_history_buffer_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->roughnessHistoryBufferSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE environment_map_srv = create_reflection_view_info.pD3D12CreateReflectionViewInfo->environmentMapSRV;
            D3D12_CPU_DESCRIPTOR_HANDLE output_buffer_uav = create_reflection_view_info.pD3D12CreateReflectionViewInfo->reflectionViewUAV;
            const D3D12_SAMPLER_DESC* environment_map_sampler_desc = create_reflection_view_info.pD3D12CreateReflectionViewInfo->pEnvironmentMapSamplerDesc;

            D3D12_CPU_DESCRIPTOR_HANDLE normal_buffers[] = { normal_buffer_srv, normal_history_buffer_srv };
            D3D12_CPU_DESCRIPTOR_HANDLE roughness_buffers[] = { roughness_buffer_srv, roughness_history_buffer_srv };

            bool ping_pong_normal = (create_reflection_view_info.flags & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_NORMAL_BUFFERS) != 0;
            bool ping_pong_roughness = (create_reflection_view_info.flags & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_ROUGHNESS_BUFFERS) != 0;

            // Helper function to create a default shader resource view for a Texture2D
            auto SRV_Tex2D = [](DXGI_FORMAT format) {
                D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = {};
                shader_resource_view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                shader_resource_view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                shader_resource_view_desc.Texture2D.MipLevels = -1;
                shader_resource_view_desc.Texture2D.MostDetailedMip = 0;
                shader_resource_view_desc.Texture2D.PlaneSlice = 0;
                shader_resource_view_desc.Texture2D.ResourceMinLODClamp = 0;
                shader_resource_view_desc.Format = format;
                return shader_resource_view_desc;
            };

            // Helper function to create a default unordered access view for a Texture2D
            auto UAV_Tex2D = [](DXGI_FORMAT format) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC unordered_access_view_desc = {};
                unordered_access_view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                unordered_access_view_desc.Texture2D.MipSlice = 0;
                unordered_access_view_desc.Texture2D.PlaneSlice = 0;
                unordered_access_view_desc.Format = format;
                return unordered_access_view_desc;
            };

            // Helper function to create a default unordered access view for a Buffer
            auto UAV_Buffer = [](uint32_t num_elements) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC unordered_access_view_desc = {};
                unordered_access_view_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                unordered_access_view_desc.Buffer.CounterOffsetInBytes = 0;
                unordered_access_view_desc.Buffer.FirstElement = 0;
                unordered_access_view_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
                unordered_access_view_desc.Buffer.NumElements = num_elements;
                unordered_access_view_desc.Buffer.StructureByteStride = 4;
                unordered_access_view_desc.Format = DXGI_FORMAT_UNKNOWN;
                return unordered_access_view_desc;
            };

            auto SRV_Buffer = [](uint32_t num_elements) {
                D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = {};
                shader_resource_view_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                shader_resource_view_desc.Buffer.FirstElement = 0;
                shader_resource_view_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                shader_resource_view_desc.Buffer.NumElements = num_elements;
                shader_resource_view_desc.Buffer.StructureByteStride = 4;
                shader_resource_view_desc.Format = DXGI_FORMAT_UNKNOWN;
                shader_resource_view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                return shader_resource_view_desc;
            };

            // Place the descriptors
            device->CreateSampler(environment_map_sampler_desc, sampler_descriptor_table_.GetCPUDescriptor(0)); // g_environment_map_sampler
            for (int i = 0; i < 2; ++i)
            {
                uint32_t num_tiles = RoundedDivide(width_, 8u) * RoundedDivide(height_, 8u);
                uint32_t num_pixels = width_ * height_;

                // Tile Classifier pass
                {
                    DescriptorD3D12 table = tile_classification_descriptor_table_[i];
                    uint32_t offset = 0;
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_roughness
                    device->CreateUnorderedAccessView(tile_list_, nullptr, &UAV_Buffer(num_tiles), table.GetCPUDescriptor(offset++)); // g_tile_list
                    device->CreateUnorderedAccessView(ray_list_, nullptr, &UAV_Buffer(num_pixels), table.GetCPUDescriptor(offset++)); // g_ray_list
                    device->CreateUnorderedAccessView(tile_counter_, nullptr, &UAV_Buffer(1), table.GetCPUDescriptor(offset++)); // g_tile_counter
                    device->CreateUnorderedAccessView(ray_counter_, nullptr, &UAV_Buffer(1), table.GetCPUDescriptor(offset++)); // g_ray_counter
                    device->CreateUnorderedAccessView(temporal_denoiser_result_[i], nullptr, &UAV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_temporally_denoised_reflections
                    device->CreateUnorderedAccessView(temporal_denoiser_result_[1 - i], nullptr, &UAV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_temporally_denoised_reflections_history
                    device->CreateUnorderedAccessView(ray_lengths_, nullptr, &UAV_Tex2D(DXGI_FORMAT_R16_FLOAT), table.GetCPUDescriptor(offset++)); // g_ray_lengths
                    device->CreateUnorderedAccessView(temporal_variance_, nullptr, &UAV_Tex2D(DXGI_FORMAT_R8_UNORM), table.GetCPUDescriptor(offset++)); // g_temporal_variance
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), output_buffer_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_denoised_reflections
                }

                // Indirect args pass
                {
                    DescriptorD3D12 table = indirect_args_descriptor_table_[i];
                    uint32_t offset = 0;
                    device->CreateUnorderedAccessView(tile_counter_, nullptr, &UAV_Buffer(1), table.GetCPUDescriptor(offset++)); // g_tile_counter
                    device->CreateUnorderedAccessView(ray_counter_, nullptr, &UAV_Buffer(1), table.GetCPUDescriptor(offset++)); // g_ray_counter

                    constexpr uint32_t indirect_arguments_member_count = 3;
                    static_assert(sizeof(D3D12_DISPATCH_ARGUMENTS) == indirect_arguments_member_count * 4, "Size of indirect arguments buffer does not match D3D12_DISPATCH_ARGUMENTS.");
                    device->CreateUnorderedAccessView(intersection_pass_indirect_args_, nullptr, &UAV_Buffer(indirect_arguments_member_count), table.GetCPUDescriptor(offset++)); // g_intersect_args
                    device->CreateUnorderedAccessView(denoiser_pass_indirect_args_, nullptr, &UAV_Buffer(indirect_arguments_member_count), table.GetCPUDescriptor(offset++)); // g_denoiser_args
                }

                // Intersection pass
                {
                    DescriptorD3D12 table = intersection_descriptor_table_[i];
                    uint32_t offset = 0;
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), scene_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_lit_scene
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), depth_hierarchy_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_depth_buffer_hierarchy
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_normal
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_roughness
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), environment_map_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_environment_map

                    // Blue noise sampler
                    D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = {};
                    shader_resource_view_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    shader_resource_view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    auto const& sampler = context.GetContextD3D12()->GetSampler2SPP();
                    shader_resource_view_desc.Buffer.NumElements = static_cast<UINT>(sampler.sobol_buffer_->GetDesc().Width / sizeof(std::int32_t));
                    shader_resource_view_desc.Buffer.StructureByteStride = static_cast<UINT>(sizeof(std::int32_t));
                    device->CreateShaderResourceView(sampler.sobol_buffer_, &shader_resource_view_desc, table.GetCPUDescriptor(offset++)); // g_sobol_buffer
                    shader_resource_view_desc.Buffer.NumElements = static_cast<UINT>(sampler.ranking_tile_buffer_->GetDesc().Width / sizeof(std::int32_t));
                    shader_resource_view_desc.Buffer.StructureByteStride = static_cast<UINT>(sizeof(std::int32_t));
                    device->CreateShaderResourceView(sampler.ranking_tile_buffer_, &shader_resource_view_desc, table.GetCPUDescriptor(offset++)); // g_ranking_tile_buffer
                    shader_resource_view_desc.Buffer.NumElements = static_cast<UINT>(sampler.scrambling_tile_buffer_->GetDesc().Width / sizeof(std::int32_t));
                    shader_resource_view_desc.Buffer.StructureByteStride = static_cast<UINT>(sizeof(std::int32_t));
                    device->CreateShaderResourceView(sampler.scrambling_tile_buffer_, &shader_resource_view_desc, table.GetCPUDescriptor(offset++)); // g_scrambling_tile_buffer

                    device->CreateShaderResourceView(ray_list_, &SRV_Buffer(num_pixels), table.GetCPUDescriptor(offset++)); // g_ray_list
                    device->CreateUnorderedAccessView(temporal_denoiser_result_[i], nullptr, &UAV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_intersection_result
                    device->CreateUnorderedAccessView(ray_lengths_, nullptr, &UAV_Tex2D(DXGI_FORMAT_R16_FLOAT), table.GetCPUDescriptor(offset++)); // g_ray_lengths
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), output_buffer_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_denoised_reflections
                }

                // Spatial denoising pass
                {
                    DescriptorD3D12 table = spatial_denoising_descriptor_table_[i];
                    uint32_t offset = 0;
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), depth_hierarchy_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_depth_buffer
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_normal
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_roughness
                    device->CreateShaderResourceView(temporal_denoiser_result_[i], &SRV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_intersection_result
                    device->CreateShaderResourceView(temporal_variance_, &SRV_Tex2D(DXGI_FORMAT_R8_UNORM), table.GetCPUDescriptor(offset++)); // g_has_ray
                    device->CreateShaderResourceView(tile_list_, &SRV_Buffer(num_tiles), table.GetCPUDescriptor(offset++)); // g_tile_list
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), output_buffer_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_spatially_denoised_reflections
                    device->CreateUnorderedAccessView(ray_lengths_, nullptr, &UAV_Tex2D(DXGI_FORMAT_R16_FLOAT), table.GetCPUDescriptor(offset++)); // g_ray_lengths
                }

                // Temporal denoising pass
                {
                    DescriptorD3D12 table = temporal_denoising_descriptor_table_[i];
                    uint32_t offset = 0;
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_normal
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_roughness
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_normal ? normal_buffers[1 - i] : normal_history_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_normal_history
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_roughness ? roughness_buffers[1 - i] : roughness_history_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_roughness_history
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), depth_hierarchy_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_depth_buffer
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), motion_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_motion_vectors
                    device->CreateShaderResourceView(temporal_denoiser_result_[1 - i], &SRV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_temporally_denoised_reflections_history
                    device->CreateShaderResourceView(ray_lengths_, &SRV_Tex2D(DXGI_FORMAT_R16_FLOAT), table.GetCPUDescriptor(offset++)); // g_ray_lengths
                    device->CreateShaderResourceView(tile_list_, &SRV_Buffer(num_tiles), table.GetCPUDescriptor(offset++)); // g_tile_list
                    device->CreateUnorderedAccessView(temporal_denoiser_result_[i], nullptr, &UAV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_temporally_denoised_reflections
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), output_buffer_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_spatially_denoised_reflections
                    device->CreateUnorderedAccessView(temporal_variance_, nullptr, &UAV_Tex2D(DXGI_FORMAT_R8_UNORM), table.GetCPUDescriptor(offset++)); // g_temporal_variance
                }

                // EAW denoising pass
                {
                    DescriptorD3D12 table = eaw_denoising_descriptor_table_[i];
                    uint32_t offset = 0;
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_normal ? normal_buffers[i] : normal_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_normal
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), ping_pong_roughness ? roughness_buffers[i] : roughness_buffer_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_roughness
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), depth_hierarchy_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_depth_buffer
                    device->CreateShaderResourceView(tile_list_, &SRV_Buffer(num_tiles), table.GetCPUDescriptor(offset++)); // g_tile_list
                    device->CreateUnorderedAccessView(temporal_denoiser_result_[i], nullptr, &UAV_Tex2D(scene_format_), table.GetCPUDescriptor(offset++)); // g_temporally_denoised_reflections
                    device->CopyDescriptorsSimple(1, table.GetCPUDescriptor(offset++), output_buffer_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_denoised_reflections
                }
            }
        }

        // Create timestamp querying resources if enabled
        if ((create_reflection_view_info.flags & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto const query_heap_size = kTimestampQuery_Count * context.GetFrameCountBeforeReuse() * sizeof(std::uint64_t);

            D3D12_QUERY_HEAP_DESC query_heap_desc = {};
            query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            query_heap_desc.Count = static_cast<UINT>(query_heap_size);

            if (!SUCCEEDED(context.GetContextD3D12()->GetDevice()->CreateQueryHeap(&query_heap_desc,
                IID_PPV_ARGS(&timestamp_query_heap_))))
            {
                throw reflection_error(context, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Unable to create timestamp query heap");
            }

            if (!context.GetContextD3D12()->AllocateReadbackBuffer(query_heap_size,
                &timestamp_query_buffer_,
                D3D12_RESOURCE_STATE_COPY_DEST,
                L"TimestampQueryBuffer"))
            {
                throw reflection_error(context, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Unable to allocate readback buffer");
            }

            timestamp_queries_.resize(context.GetFrameCountBeforeReuse());

            for (auto& timestamp_queries : timestamp_queries_)
            {
                timestamp_queries.reserve(kTimestampQuery_Count);
            }
        }
    }

    /**
        Destroys the reflection view.
    */
    void ReflectionViewD3D12::Destroy()
    {
        if (descriptor_heap_cbv_srv_uav_)
            delete descriptor_heap_cbv_srv_uav_;
        descriptor_heap_cbv_srv_uav_ = nullptr;

        if (descriptor_heap_samplers_)
            delete descriptor_heap_samplers_;
        descriptor_heap_samplers_ = nullptr;

#define FFX_SSSR_SAFE_RELEASE(x)\
        if(x) { x->Release(); }\
        x = nullptr;

        FFX_SSSR_SAFE_RELEASE(timestamp_query_heap_);
        FFX_SSSR_SAFE_RELEASE(timestamp_query_buffer_);
        FFX_SSSR_SAFE_RELEASE(temporal_denoiser_result_[0]);
        FFX_SSSR_SAFE_RELEASE(temporal_denoiser_result_[1]);
        FFX_SSSR_SAFE_RELEASE(ray_lengths_);
        FFX_SSSR_SAFE_RELEASE(temporal_variance_);
        FFX_SSSR_SAFE_RELEASE(tile_list_);
        FFX_SSSR_SAFE_RELEASE(tile_counter_);
        FFX_SSSR_SAFE_RELEASE(ray_list_);
        FFX_SSSR_SAFE_RELEASE(ray_counter_);
        FFX_SSSR_SAFE_RELEASE(intersection_pass_indirect_args_);
        FFX_SSSR_SAFE_RELEASE(denoiser_pass_indirect_args_);
        FFX_SSSR_SAFE_RELEASE(resource_heap_);

#undef FFX_SSSR_SAFE_RELEASE

        timestamp_queries_.resize(0u);
    }

    /**
        Creates the descriptor heaps.

        \param context The context to be used.
    */
    void ReflectionViewD3D12::CreateDescriptorHeaps(Context& context)
    {
        ContextD3D12* d3d12_context = context.GetContextD3D12();

        FFX_SSSR_ASSERT(!descriptor_heap_cbv_srv_uav_);
        FFX_SSSR_ASSERT(!descriptor_heap_samplers_);

        descriptor_heap_cbv_srv_uav_ = new DescriptorHeapD3D12(context);
        FFX_SSSR_ASSERT(descriptor_heap_cbv_srv_uav_ != nullptr);
        std::uint32_t descriptor_count
            = d3d12_context->GetTileClassificationPass().descriptor_count_
            + d3d12_context->GetIndirectArgsPass().descriptor_count_
            + d3d12_context->GetIntersectionPass().descriptor_count_
            + d3d12_context->GetSpatialDenoisingPass().descriptor_count_
            + d3d12_context->GetTemporalDenoisingPass().descriptor_count_
            + d3d12_context->GetEawDenoisingPass().descriptor_count_;
        descriptor_heap_cbv_srv_uav_->Create(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 * descriptor_count, 0u);

        descriptor_heap_samplers_ = new DescriptorHeapD3D12(context);
        FFX_SSSR_ASSERT(descriptor_heap_samplers_ != nullptr);
        descriptor_heap_samplers_->Create(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, 0u); // g_environment_map_sampler
    }

    /**
        Gets the index of the current timestamp query.

        \return The index of the current timestamp query.
    */
    std::uint32_t ReflectionViewD3D12::GetTimestampQueryIndex() const
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
        Resolves the Direct3D12 reflection view.

        \param context The context to be used.
        \param reflection_view The reflection view to be resolved.
        \param resolve_reflection_view_info The reflection view resolve information.
    */
    void ReflectionViewD3D12::Resolve(Context& context, ReflectionView const& reflection_view, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info)
    {
        // Get hold of the command list for recording
        FFX_SSSR_ASSERT(resolve_reflection_view_info.pD3D12CommandEncodeInfo);
        auto const command_list = ContextD3D12::GetCommandList(context, resolve_reflection_view_info.pD3D12CommandEncodeInfo->pCommandList);
        FFX_SSSR_ASSERT(descriptor_heap_cbv_srv_uav_ && descriptor_heap_samplers_ && command_list);
        FFX_SSSR_ASSERT(resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_1 || resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_2 || resolve_reflection_view_info.samplesPerQuad == FFX_SSSR_RAY_SAMPLES_PER_QUAD_4);

        // Query timestamp value prior to resolving the reflection view
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

            if (!timestamp_queries.empty())
            {
                std::uint64_t* data;

                // Reset performance counters
                tile_classification_elapsed_time_ = 0ull;
                denoising_elapsed_time_ = 0ull;
                intersection_elapsed_time_ = 0ull;

                auto const start_index = timestamp_queries_index_ * kTimestampQuery_Count;

                D3D12_RANGE read_range = {};
                read_range.Begin = start_index * sizeof(std::uint64_t);
                read_range.End = (start_index + timestamp_queries.size()) * sizeof(std::uint64_t);

                timestamp_query_buffer_->Map(0u,
                                             &read_range,
                                             reinterpret_cast<void**>(&data));

                for (auto i = 0u, j = 1u; j < timestamp_queries.size(); ++i, ++j)
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

                timestamp_query_buffer_->Unmap(0u, nullptr);
            }

            timestamp_queries.clear();

            command_list->EndQuery(timestamp_query_heap_,
                                   D3D12_QUERY_TYPE_TIMESTAMP,
                                   GetTimestampQueryIndex());

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
        auto& upload_buffer = context.GetContextD3D12()->GetUploadBuffer();
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
        
        std::uint32_t current_frame = context.GetFrameIndex() & 1u;
        ID3D12DescriptorHeap *descriptor_heaps[] = { descriptor_heap_cbv_srv_uav_->GetDescriptorHeap(), descriptor_heap_samplers_->GetDescriptorHeap() };
        command_list->SetDescriptorHeaps(FFX_SSSR_ARRAY_SIZE(descriptor_heaps), descriptor_heaps);

        ID3D12Resource * cb_resource = upload_buffer.GetResource();
        size_t offset = upload_buffer.GetOffset(pass_data);

        auto UAVBarrier = [](ID3D12Resource * resource) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.UAV.pResource = resource;
            return barrier;
        };

        auto Transition = [](ID3D12Resource * resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = from;
            barrier.Transition.StateAfter = to;
            barrier.Transition.Subresource = 0;
            return barrier;
        };

        ContextD3D12* d3d12_context = context.GetContextD3D12();

        // Tile Classification pass
        {
            command_list->SetComputeRootSignature(d3d12_context->GetTileClassificationPass().root_signature_);
            command_list->SetComputeRootDescriptorTable(0, tile_classification_descriptor_table_[current_frame].GetGPUDescriptor());
            command_list->SetComputeRootConstantBufferView(1, cb_resource->GetGPUVirtualAddress() + offset);
            command_list->SetPipelineState(d3d12_context->GetTileClassificationPass().pipeline_state_);
            uint32_t dim_x = RoundedDivide(width_, 8u);
            uint32_t dim_y = RoundedDivide(height_, 8u);
            command_list->Dispatch(dim_x, dim_y, 1);
        }

        // Ensure that the tile classification pass finished
        D3D12_RESOURCE_BARRIER classification_results_barriers[] = { 
            UAVBarrier(ray_list_), 
            UAVBarrier(tile_list_),
            Transition(intersection_pass_indirect_args_, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            Transition(denoiser_pass_indirect_args_, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        command_list->ResourceBarrier(FFX_SSSR_ARRAY_SIZE(classification_results_barriers), classification_results_barriers);

        // Indirect Arguments pass
        {
            command_list->SetComputeRootSignature(d3d12_context->GetIndirectArgsPass().root_signature_);
            command_list->SetComputeRootDescriptorTable(0, indirect_args_descriptor_table_[current_frame].GetGPUDescriptor());
            command_list->SetComputeRootConstantBufferView(1, cb_resource->GetGPUVirtualAddress() + offset);
            command_list->SetPipelineState(d3d12_context->GetIndirectArgsPass().pipeline_state_);
            command_list->Dispatch(1, 1, 1);
        }

        // Query the amount of time spent in the intersection pass
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

            FFX_SSSR_ASSERT(timestamp_queries.size() == 1ull && timestamp_queries[0] == kTimestampQuery_Init);

            command_list->EndQuery(timestamp_query_heap_,
                D3D12_QUERY_TYPE_TIMESTAMP,
                GetTimestampQueryIndex());

            timestamp_queries.push_back(kTimestampQuery_TileClassification);
        }

        // Ensure that the arguments are written
        D3D12_RESOURCE_BARRIER indirect_arguments_barriers[] = { 
            UAVBarrier(intersection_pass_indirect_args_), 
            UAVBarrier(denoiser_pass_indirect_args_),
            Transition(intersection_pass_indirect_args_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            Transition(denoiser_pass_indirect_args_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
        };
        command_list->ResourceBarrier(FFX_SSSR_ARRAY_SIZE(indirect_arguments_barriers), indirect_arguments_barriers);

        // Intersection pass
        {
            command_list->SetComputeRootSignature(d3d12_context->GetIntersectionPass().root_signature_);
            command_list->SetComputeRootDescriptorTable(0, intersection_descriptor_table_[current_frame].GetGPUDescriptor());
            command_list->SetComputeRootConstantBufferView(1, cb_resource->GetGPUVirtualAddress() + offset);
            command_list->SetComputeRootDescriptorTable(2, sampler_descriptor_table_.GetGPUDescriptor());
            command_list->SetPipelineState(d3d12_context->GetIntersectionPass().pipeline_state_);
            command_list->ExecuteIndirect(d3d12_context->GetIndirectDispatchCommandSignature(), 1, intersection_pass_indirect_args_, 0, nullptr, 0);
        }

        // Query the amount of time spent in the intersection pass
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

            FFX_SSSR_ASSERT(timestamp_queries.size() == 2ull && timestamp_queries[1] == kTimestampQuery_TileClassification);

            command_list->EndQuery(timestamp_query_heap_,
                                   D3D12_QUERY_TYPE_TIMESTAMP,
                                   GetTimestampQueryIndex());

            timestamp_queries.push_back(kTimestampQuery_Intersection);
        }

        if (resolve_reflection_view_info.flags & FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE)
        {
            // Ensure that the intersection pass finished
            command_list->ResourceBarrier(1, &UAVBarrier(temporal_denoiser_result_[current_frame]));

            // Spatial denoiser passes
            {
                command_list->SetComputeRootSignature(d3d12_context->GetSpatialDenoisingPass().root_signature_);
                command_list->SetComputeRootDescriptorTable(0, spatial_denoising_descriptor_table_[current_frame].GetGPUDescriptor());
                command_list->SetComputeRootConstantBufferView(1, cb_resource->GetGPUVirtualAddress() + offset);
                command_list->SetPipelineState(d3d12_context->GetSpatialDenoisingPass().pipeline_state_);
                command_list->ExecuteIndirect(d3d12_context->GetIndirectDispatchCommandSignature(), 1, denoiser_pass_indirect_args_, 0, nullptr, 0);
            }

            // Ensure that the spatial denoising pass finished. We don't have the resource for the final result available, thus we have to wait for any UAV access to finish.
            command_list->ResourceBarrier(1, &UAVBarrier(nullptr));

            // Temporal denoiser passes
            {
                command_list->SetComputeRootSignature(d3d12_context->GetTemporalDenoisingPass().root_signature_);
                command_list->SetComputeRootDescriptorTable(0, temporal_denoising_descriptor_table_[current_frame].GetGPUDescriptor());
                command_list->SetComputeRootConstantBufferView(1, cb_resource->GetGPUVirtualAddress() + offset);
                command_list->SetPipelineState(d3d12_context->GetTemporalDenoisingPass().pipeline_state_);
                command_list->ExecuteIndirect(d3d12_context->GetIndirectDispatchCommandSignature(), 1, denoiser_pass_indirect_args_, 0, nullptr, 0);
            }

            // Ensure that the temporal denoising pass finished
            command_list->ResourceBarrier(1, &UAVBarrier(temporal_denoiser_result_[current_frame]));

            // EAW denoiser passes
            {
                command_list->SetComputeRootSignature(d3d12_context->GetEawDenoisingPass().root_signature_);
                command_list->SetComputeRootDescriptorTable(0, eaw_denoising_descriptor_table_[current_frame].GetGPUDescriptor());
                command_list->SetComputeRootConstantBufferView(1, cb_resource->GetGPUVirtualAddress() + offset);
                command_list->SetPipelineState(d3d12_context->GetEawDenoisingPass().pipeline_state_);
                command_list->ExecuteIndirect(d3d12_context->GetIndirectDispatchCommandSignature(), 1, denoiser_pass_indirect_args_, 0, nullptr, 0);
            }

            // Query the amount of time spent in the denoiser passes
            if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
            {
                auto& timestamp_queries = timestamp_queries_[timestamp_queries_index_];

                FFX_SSSR_ASSERT(timestamp_queries.size() == 3ull && timestamp_queries[2] == kTimestampQuery_Intersection);

                command_list->EndQuery(timestamp_query_heap_,
                    D3D12_QUERY_TYPE_TIMESTAMP,
                    GetTimestampQueryIndex());

                timestamp_queries.push_back(kTimestampQuery_Denoising);
            }
        }

        // Resolve the timestamp query data
        if ((flags_ & FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS) != 0)
        {
            auto const start_index = timestamp_queries_index_ * kTimestampQuery_Count;

            command_list->ResolveQueryData(timestamp_query_heap_,
                D3D12_QUERY_TYPE_TIMESTAMP,
                start_index,
                static_cast<UINT>(timestamp_queries_[timestamp_queries_index_].size()),
                timestamp_query_buffer_,
                start_index * sizeof(std::uint64_t));

            timestamp_queries_index_ = (timestamp_queries_index_ + 1u) % context.GetFrameCountBeforeReuse();
        }
    }
}
