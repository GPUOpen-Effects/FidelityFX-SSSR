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

namespace ffx_sssr
{
    /**
        The constructor for the DescriptorD3D12 class.
    */
    DescriptorD3D12::DescriptorD3D12()
        : descriptor_count_(0u)
        , descriptor_handle_size_(0u)
        , cpu_descriptor_handle_{0ull}
        , gpu_descriptor_handle_{0ull}
    {
    }

    /**
        Gets the CPU descriptor.

        \param descriptor_index The index of the descriptor.
        \return The CPU descriptor handle.
    */
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorD3D12::GetCPUDescriptor(std::uint32_t descriptor_index) const
    {
        FFX_SSSR_ASSERT(descriptor_index < descriptor_count_);
        auto cpu_descriptor_handle = cpu_descriptor_handle_;
        cpu_descriptor_handle.ptr += static_cast<SIZE_T>(descriptor_index) * static_cast<SIZE_T>(descriptor_handle_size_);
        return cpu_descriptor_handle;
    }

    /**
        Gets the GPU descriptor.

        \param descriptor_index The index of the descriptor.
        \return The GPU descriptor handle.
    */
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorD3D12::GetGPUDescriptor(std::uint32_t descriptor_index) const
    {
        FFX_SSSR_ASSERT(descriptor_index < descriptor_count_);
        auto gpu_descriptor_handle = gpu_descriptor_handle_;
        gpu_descriptor_handle.ptr += static_cast<UINT64>(descriptor_index) * static_cast<UINT64>(descriptor_handle_size_);
        return gpu_descriptor_handle;
    }

    /**
        The constructor for the Range class.
    */
    DescriptorHeapD3D12::Range::Range()
        : frame_index_(0u)
        , range_start_(0u)
        , range_size_(0u)
    {
    }

    /**
        The constructor for the Range class.

        \param range_start The start of the range in the heap.
        \param range_size The size of the allocation range.
    */
    DescriptorHeapD3D12::Range::Range(std::uint32_t range_start, std::uint32_t range_size)
        : frame_index_(0u)
        , range_start_(range_start)
        , range_size_(range_size)
    {
    }

    /**
        Checks whether the ranges overlap.

        \param other The range to be checked for overlap.
        \return true if the ranges overlap, false otherwise.
    */
    bool DescriptorHeapD3D12::Range::Overlap(Range const& other) const
    {
        return (range_start_ < other.range_start_ + other.range_size_ && other.range_start_ < range_start_ + range_size_);
    }

    /**
        Gets the Direct3D12 descriptor heap.

        \return The Direct3D12 descriptor heap.
    */
    ID3D12DescriptorHeap* const& DescriptorHeapD3D12::GetDescriptorHeap() const
    {
        return descriptor_heap_;
    }

    /**
        Allocates a static descriptor.

        \param descriptor The allocated descriptor.
        \param descriptor_count The number of descriptors to be allocated.
        \return true if the descriptor was allocated successfully, false otherwise.
    */
    bool DescriptorHeapD3D12::AllocateStaticDescriptor(DescriptorD3D12& descriptor, std::uint32_t descriptor_count)
    {
        // Calculate the new cursor position
        auto const static_descriptor_heap_cursor = static_descriptor_heap_cursor_ + descriptor_count;

        if (static_descriptor_heap_cursor > static_descriptor_heap_size_)
        {
            return false;   // out of memory
        }

        // Populate the descriptor handles
        descriptor.descriptor_count_ = descriptor_count;
        descriptor.descriptor_handle_size_ = descriptor_handle_size_;
        descriptor.cpu_descriptor_handle_ = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
        descriptor.cpu_descriptor_handle_.ptr += static_cast<SIZE_T>(static_descriptor_heap_cursor_) * static_cast<SIZE_T>(descriptor_handle_size_);
        descriptor.gpu_descriptor_handle_ = descriptor_heap_->GetGPUDescriptorHandleForHeapStart();
        descriptor.gpu_descriptor_handle_.ptr += static_cast<UINT64>(static_descriptor_heap_cursor_) * static_cast<UINT64>(descriptor_handle_size_);

        // Advance the allocation cursor
        static_descriptor_heap_cursor_ = static_descriptor_heap_cursor;

        return true;
    }

    /**
        Allocates a dynamic descriptor.

        \param descriptor The allocated descriptor.
        \param descriptor_count The number of descriptors to be allocated.
        \return true if the descriptor was allocated successfully, false otherwise.
    */
    bool DescriptorHeapD3D12::AllocateDynamicDescriptor(DescriptorD3D12& descriptor, std::uint32_t descriptor_count)
    {
        // Calculate the new cursor position
        auto dynamic_descriptor_heap_cursor = dynamic_descriptor_heap_cursor_ + descriptor_count;

        if (dynamic_descriptor_heap_cursor > dynamic_descriptor_heap_size_)
        {
            dynamic_descriptor_heap_cursor_ = 0u;   // loop back
            dynamic_descriptor_heap_cursor = descriptor_count;
        }
        if (dynamic_descriptor_heap_cursor > dynamic_descriptor_heap_size_)
        {
            return false;   // not enough memory available
        }

        // Check whether we can safely reuse the allocation range
        Range dynamic_descriptor_heap_range(dynamic_descriptor_heap_cursor_, descriptor_count);

        while (!dynamic_descriptor_heap_ranges_.empty() && dynamic_descriptor_heap_ranges_.front().Overlap(dynamic_descriptor_heap_range))
        {
            FFX_SSSR_ASSERT(context_.GetFrameIndex() >= dynamic_descriptor_heap_ranges_.front().frame_index_);

            if (context_.GetFrameIndex() - dynamic_descriptor_heap_ranges_.front().frame_index_ < context_.GetFrameCountBeforeReuse())
            {
                return false;   // next available range is still in flight!
            }

            dynamic_descriptor_heap_ranges_.pop_front();
        }

        // Populate the descriptor handles
        descriptor.descriptor_count_ = descriptor_count;
        descriptor.descriptor_handle_size_ = descriptor_handle_size_;
        descriptor.cpu_descriptor_handle_ = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
        descriptor.cpu_descriptor_handle_.ptr += (static_cast<SIZE_T>(static_descriptor_heap_size_) + static_cast<SIZE_T>(dynamic_descriptor_heap_cursor_)) * static_cast<SIZE_T>(descriptor_handle_size_);
        descriptor.gpu_descriptor_handle_ = descriptor_heap_->GetGPUDescriptorHandleForHeapStart();
        descriptor.gpu_descriptor_handle_.ptr += (static_cast<UINT64>(static_descriptor_heap_size_) + static_cast<UINT64>(dynamic_descriptor_heap_cursor_)) * static_cast<UINT64>(descriptor_handle_size_);

        // Advance the allocation cursor
        dynamic_descriptor_heap_range.frame_index_ = context_.GetFrameIndex();
        dynamic_descriptor_heap_ranges_.push_back(dynamic_descriptor_heap_range);
        dynamic_descriptor_heap_cursor_ = dynamic_descriptor_heap_cursor;

        return true;
    }
}
