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
#include "upload_buffer_vk.h"

#include "utils.h"
#include "context.h"
#include "context_vk.h"

namespace ffx_sssr
{
    /**
        The constructor for the UploadBufferVK class.

        \param context The Vulkan context to be used.
        \param buffer_size The size of the upload buffer (in bytes).
    */
    UploadBufferVK::UploadBufferVK(ContextVK& context, std::size_t buffer_size)
        : data_(nullptr)
        , context_(context)
        , buffer_()
        , buffer_size_(buffer_size)
        , blocks_(buffer_size)
    {
        FFX_SSSR_ASSERT(context.GetDevice());
        FFX_SSSR_ASSERT(context.GetPhysicalDevice());
        FFX_SSSR_ASSERT(buffer_size_ > 0);
    }

    /**
        The destructor for the UploadBufferVK class.
    */
    UploadBufferVK::~UploadBufferVK()
    {
        if (buffer_.mapped_)
        {
            buffer_.Unmap();
        }
    }

    /**
        Allocates a buffer.

        \param size The size of the buffer (in bytes).
        \param data The pointer to the pinned memory.
        \return true if the buffer was allocated successfully, false otherwise.
    */
    bool UploadBufferVK::AllocateBufferInternal(std::size_t size, void*& data)
    {
        std::size_t start;

        auto const memory_block = blocks_.AcquireBlock(start, size, 256u);

        if (!memory_block)
        {
            return false;
        }

        data = static_cast<char*>(data_) + start;

        memory_block->block_index_ = context_.GetContext().GetFrameIndex();
        memory_block->frame_index_ = &context_.GetContext().GetFrameIndex();
        memory_block->frame_count_before_reuse_ = context_.GetContext().GetFrameCountBeforeReuse();

        return true;
    }

    /**
        Initialize and map the upload buffer. Has to be deferred as we can't access the allocator in the constructor yet.
    */
    void UploadBufferVK::Initialize()
    {
        BufferVK::CreateInfo create_info = {};
        create_info.memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // TODO: VMA_MEMORY_USAGE_CPU_TO_GPU
        create_info.buffer_usage_ = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        create_info.format_ = VK_FORMAT_UNDEFINED;
        create_info.size_in_bytes_ = buffer_size_;

        buffer_ = BufferVK(context_.GetDevice(), context_.GetPhysicalDevice(), create_info);
        buffer_.Map(&data_);
    }
}
