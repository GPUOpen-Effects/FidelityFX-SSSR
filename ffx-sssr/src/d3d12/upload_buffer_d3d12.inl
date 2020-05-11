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
        The constructor for the Block class.
    */
    UploadBufferD3D12::Block::Block()
        : frame_index_(nullptr)
        , block_index_(0u)
        , frame_count_before_reuse_(0u)
    {
    }

    /**
        Checks whether the memory block can now be re-used.

        \return true if the memory block can be re-used, false otherwise.
    */
    bool UploadBufferD3D12::Block::CanBeReused() const
    {
        FFX_SSSR_ASSERT(frame_index_ && *frame_index_ >= block_index_);

        return (*frame_index_ - block_index_ >= frame_count_before_reuse_);
    }

    /**
        Gets the size of the upload buffer.

        \return The size of the upload buffer (in bytes).
    */
    std::size_t UploadBufferD3D12::GetSize() const
    {
        return static_cast<std::size_t>(buffer_ ? buffer_->GetDesc().Width : 0ull);
    }

    /**
        Gets the resource for the upload buffer.

        \return The resource for the upload buffer.
    */
    ID3D12Resource* UploadBufferD3D12::GetResource() const
    {
        return buffer_;
    }

    /**
        Gets the offset for the allocate range of memory.

        \param data The allocated range of memory.
        \return The offset within the upload buffer (in bytes).
    */
    std::size_t UploadBufferD3D12::GetOffset(void* data) const
    {
        if (!data)
            return 0ull;
        auto const offset = static_cast<char const*>(data) - static_cast<char const*>(data_);
        FFX_SSSR_ASSERT(buffer_ && data >= data_ && static_cast<UINT64>(offset) < buffer_->GetDesc().Width);  // buffer overflow!
        return static_cast<std::size_t>(offset);
    }

    /**
        Allocates a buffer.

        \param size The size of the buffer (in bytes).
        \param data The pointer to the pinned memory.
        \return true if the buffer was allocated successfully, false otherwise.
    */
    template<typename TYPE>
    bool UploadBufferD3D12::AllocateBuffer(std::size_t size, TYPE*& data)
    {
        void* data_internal;
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address_unused;

        if (!AllocateBuffer(Align(size, 256ull), gpu_address_unused, data_internal))
        {
            return false;
        }

        data = static_cast<TYPE*>(data_internal);

        return true;
    }

    /**
        Allocates a buffer.

        \param size The size of the buffer (in bytes).
        \param data The pointer to the pinned memory.
        \param gpu_address The GPU virtual address.
        \return true if the buffer was allocated successfully, false otherwise.
    */
    template<typename TYPE>
    bool UploadBufferD3D12::AllocateBuffer(std::size_t size, TYPE*& data, D3D12_GPU_VIRTUAL_ADDRESS& gpu_address)
    {
        void* data_internal;

        if (!AllocateBuffer(size, gpu_address, data_internal))
        {
            return false;
        }

        data = static_cast<TYPE*>(data_internal);

        return true;
    }
}
