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
#include "upload_buffer_d3d12.h"

#include "utils.h"
#include "context.h"
#include "context_d3d12.h"

namespace sssr
{
    /**
        The constructor for the UploadBufferD3D12 class.

        \param context The Direct3D12 context to be used.
        \param buffer_size The size of the upload buffer (in bytes).
    */
    UploadBufferD3D12::UploadBufferD3D12(ContextD3D12& context, std::size_t buffer_size)
        : data_(nullptr)
        , context_(context.GetContext())
        , buffer_(nullptr)
        , blocks_(buffer_size)
    {
        SSSR_ASSERT(context.GetDevice());

        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
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

        if (!SUCCEEDED(context.GetDevice()->CreateCommittedResource(&heap_properties,
                                                                    D3D12_HEAP_FLAG_NONE,
                                                                    &resource_desc,
                                                                    D3D12_RESOURCE_STATE_GENERIC_READ,
                                                                    nullptr,
                                                                    IID_PPV_ARGS(&buffer_))))
        {
            throw reflection_error(context_, SSSR_STATUS_OUT_OF_MEMORY, "Failed to allocate %uMiB for the upload buffer", RoundedDivide(buffer_size, 1024ull * 1024ull));
        }

        D3D12_RANGE range = {};
        range.Begin = 0u;
        range.End = static_cast<SIZE_T>(buffer_size);

        if (!SUCCEEDED(buffer_->Map(0u,
                                    &range,
                                    reinterpret_cast<void**>(&data_))))
        {
            throw reflection_error(context_, SSSR_STATUS_INTERNAL_ERROR, "Cannot map the Direct3D12 upload buffer");
        }

        buffer_->SetName(L"UploadBufferRing");
    }

    /**
        The destructor for the UploadBufferD3D12 class.
    */
    UploadBufferD3D12::~UploadBufferD3D12()
    {
        if (buffer_)
        {
            if (data_)
            {
                D3D12_RANGE range = {};
                range.Begin = 0u;
                range.End = static_cast<SIZE_T>(buffer_->GetDesc().Width);

                buffer_->Unmap(0u, &range);
            }

            buffer_->Release();
        }
    }

    /**
        Allocates a buffer.

        \param size The size of the buffer (in bytes).
        \param gpu_address The GPU virtual address.
        \param data The pointer to the pinned memory.
        \return true if the buffer was allocated successfully, false otherwise.
    */
    bool UploadBufferD3D12::AllocateBuffer(std::size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpu_address, void*& data)
    {
        std::size_t start;

        auto const memory_block = blocks_.AcquireBlock(start, size, 256u);

        if (!memory_block)
        {
            return false;
        }

        data = static_cast<char*>(data_) + start;
        gpu_address = buffer_->GetGPUVirtualAddress() + start;

        memory_block->block_index_ = context_.GetFrameIndex();
        memory_block->frame_index_ = &context_.GetFrameIndex();
        memory_block->frame_count_before_reuse_ = context_.GetFrameCountBeforeReuse();

        return true;
    }

    /**
        Creates a constant buffer view for the allocated range.

        \param data The pointer to the allocated memory.
        \param size The size of the allocated range (in bytes).
        \param cpu_descriptor The CPU descriptor to be used.
    */
    void UploadBufferD3D12::CreateConstantBufferView(void const* data, std::size_t size, D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor) const
    {
        auto const offset = static_cast<char const*>(data) - static_cast<char const*>(data_);
        SSSR_ASSERT(buffer_ && data >= data_ && offset + size <= buffer_->GetDesc().Width);   // buffer overflow!

        D3D12_CONSTANT_BUFFER_VIEW_DESC constant_buffer_view_desc = {};
        constant_buffer_view_desc.BufferLocation = buffer_->GetGPUVirtualAddress() + offset;
        constant_buffer_view_desc.SizeInBytes = static_cast<UINT>(Align(size, 256ull));

        context_.GetContextD3D12()->GetDevice()->CreateConstantBufferView(&constant_buffer_view_desc,
                                                                          cpu_descriptor);
    }

    /**
        Creates a shader resource view for the allocated range.

        \param data The pointer to the allocated memory.
        \param size The size of the allocated range (in bytes).
        \param stride The size of an individual element (in bytes).
        \param cpu_descriptor The CPU descriptor to be used.
    */
    void UploadBufferD3D12::CreateShaderResourceView(void const* data, std::size_t size, std::size_t stride, D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor) const
    {
        auto const offset = static_cast<char const*>(data) - static_cast<char const*>(data_);
        SSSR_ASSERT(buffer_ && data >= data_ && offset + size <= buffer_->GetDesc().Width);   // buffer overflow!

        D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = {};
        shader_resource_view_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        shader_resource_view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shader_resource_view_desc.Buffer.FirstElement = static_cast<UINT64>(offset / stride);
        shader_resource_view_desc.Buffer.NumElements = static_cast<UINT>(size / stride);
        shader_resource_view_desc.Buffer.StructureByteStride = static_cast<UINT>(stride);

        context_.GetContextD3D12()->GetDevice()->CreateShaderResourceView(buffer_,
                                                                          &shader_resource_view_desc,
                                                                          cpu_descriptor);
    }
}
