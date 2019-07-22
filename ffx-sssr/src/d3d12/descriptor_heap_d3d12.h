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
#include <d3d12.h>

#include "context.h"

namespace sssr
{
    class DescriptorHeapD3D12;

    /**
        The DescriptorD3D12 class represents an individual Direct3D12 descriptor handle.
    */
    class DescriptorD3D12
    {
    public:
        inline DescriptorD3D12();

        inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptor(std::uint32_t descriptor_index = 0u) const;
        inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptor(std::uint32_t descriptor_index = 0u) const;

    protected:
        friend class DescriptorHeapD3D12;

        // The number of descriptors available.
        std::uint32_t descriptor_count_;
        // The size of an individual descriptor handle.
        std::uint32_t descriptor_handle_size_;
        // The CPU-side descriptor handle.
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle_;
        // The GPU-side descriptor handle.
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle_;
    };

    /**
        The DescriptorHeapD3D12 class represents a Direct3D12 heap for allocating descriptors of a given type.
    */
    class DescriptorHeapD3D12
    {
        SSSR_NON_COPYABLE(DescriptorHeapD3D12);

    public:
        DescriptorHeapD3D12(Context& context);
        ~DescriptorHeapD3D12();

        inline ID3D12DescriptorHeap* const& GetDescriptorHeap() const;

        inline bool AllocateStaticDescriptor(DescriptorD3D12& descriptor, std::uint32_t descriptor_count = 1u);
        inline bool AllocateDynamicDescriptor(DescriptorD3D12& descriptor, std::uint32_t descriptor_count = 1u);

        void Create(D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type, std::uint32_t static_descriptor_count, std::uint32_t dynamic_descriptor_count);
        void Destroy();

    protected:
        /**
            The Range class describes an allocated range within a descriptor heap.
        */
        class Range
        {
        public:
            inline Range();
            inline Range(std::uint32_t range_start, std::uint32_t range_size);

            inline bool Overlap(Range const& other) const;

            // The index of the allocation frame for this range.
            std::uint32_t frame_index_;
            // The start of the range in the heap.
            std::uint32_t range_start_;
            // The size of the allocation range.
            std::uint32_t range_size_;
        };

        // The context to be used.
        Context& context_;
        // The Direct3D12 descriptor heap.
        ID3D12DescriptorHeap* descriptor_heap_;
        // The size of an individual descriptor handle.
        std::uint32_t descriptor_handle_size_;
        // The size of the heap for allocating static descriptors.
        std::uint32_t static_descriptor_heap_size_;
        // The cursor of the heap for allocating static descriptors.
        std::uint32_t static_descriptor_heap_cursor_;
        // The size of the heap for allocating dynamic descriptors.
        std::uint32_t dynamic_descriptor_heap_size_;
        // The cursor of the heap for allocating dynamic descriptors.
        std::uint32_t dynamic_descriptor_heap_cursor_;
        // The allocated ranges with the dynamic descriptor heap.
        std::deque<Range> dynamic_descriptor_heap_ranges_;
    };
}

#include "descriptor_heap_d3d12.inl"
