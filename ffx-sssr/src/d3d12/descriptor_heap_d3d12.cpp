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
#include "descriptor_heap_d3d12.h"

#include "reflection_error.h"
#include "context_d3d12.h"

namespace sssr
{
    /**
        The constructor for the DescriptorHeapD3D12 class.

        \param context The context to be used.
    */
    DescriptorHeapD3D12::DescriptorHeapD3D12(Context& context)
        : context_(context)
        , descriptor_heap_(nullptr)
        , descriptor_handle_size_(0u)
        , static_descriptor_heap_size_(0u)
        , static_descriptor_heap_cursor_(0u)
        , dynamic_descriptor_heap_size_(0u)
        , dynamic_descriptor_heap_cursor_(0u)
    {
    }

    /**
        The destructor for the DescriptorHeapD3D12 class.
    */
    DescriptorHeapD3D12::~DescriptorHeapD3D12()
    {
        Destroy();
    }

    /**
        Creates the Direct3D12 descriptor heap.

        \param descriptor_heap_type The type of descriptor heap to be created.
        \param static_descriptor_count The number of static descriptors to be allocated.
        \param dynamic_descriptor_count The number of dynamic descriptors to be allocated.
    */
    void DescriptorHeapD3D12::Create(D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, std::uint32_t static_descriptor_count, std::uint32_t dynamic_descriptor_count)
    {
        HRESULT result;

        // Populate the allocation ranges
        auto const static_descriptor_heap_size = static_descriptor_count;
        auto const dynamic_descriptor_heap_size = dynamic_descriptor_count * context_.GetFrameCountBeforeReuse();

        // Create the descriptor heap
        auto const descriptor_count = static_descriptor_heap_size + dynamic_descriptor_heap_size;
        auto const descriptor_handle_size = context_.GetContextD3D12()->GetDevice()->GetDescriptorHandleIncrementSize(descriptor_heap_type);

        D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
        descriptor_heap_desc.Type = descriptor_heap_type;
        descriptor_heap_desc.NumDescriptors = descriptor_count;
        descriptor_heap_desc.Flags = (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV ? D3D12_DESCRIPTOR_HEAP_FLAG_NONE : D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

        ID3D12DescriptorHeap* descriptor_heap;
        result = context_.GetContextD3D12()->GetDevice()->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap));
        if (!SUCCEEDED(result))
        {
            throw reflection_error(context_, SSSR_STATUS_INTERNAL_ERROR, "Unable to create descriptor heap");
        }
        descriptor_heap->SetName(L"SSSR Descriptor Heap");

        // Assign the base members
        if (descriptor_heap_)
            descriptor_heap_->Release();
        descriptor_heap_ = descriptor_heap;
        descriptor_handle_size_ = descriptor_handle_size;
        static_descriptor_heap_size_ = static_descriptor_heap_size;
        static_descriptor_heap_cursor_ = 0u;
        dynamic_descriptor_heap_size_ = dynamic_descriptor_heap_size;
        dynamic_descriptor_heap_cursor_ = 0u;
    }

    /**
        Destroys the Direct3D12 descriptor heap.
    */
    void DescriptorHeapD3D12::Destroy()
    {
        if (descriptor_heap_)
            descriptor_heap_->Release();
        descriptor_heap_ = nullptr;
    }
}
