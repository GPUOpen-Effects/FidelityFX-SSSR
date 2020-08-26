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

#include <vulkan/vulkan.h>

#include "macros.h"
#include "ffx_sssr.h"

namespace ffx_sssr
{
    /**
        The ImageVK class is a helper class to create and destroy image resources on Vulkan.
    */
    class ImageVK
    {
        FFX_SSSR_NON_COPYABLE(ImageVK);

    public:

        class CreateInfo
        {
        public:
            uint32_t width_;
            uint32_t height_;
            VkFormat format_;
            uint32_t mip_levels_;
            VkImageLayout initial_layout_;
            VkMemoryPropertyFlags memory_property_flags;
            VkImageUsageFlags image_usage_;
            const char* name_;
        };

        ImageVK();
        ~ImageVK();
        
        ImageVK(VkDevice device, VkPhysicalDevice physical_device, const CreateInfo& create_info);

        ImageVK(ImageVK&& other) noexcept;
        ImageVK& ImageVK::operator =(ImageVK&& other) noexcept;

        VkDevice device_;
        VkImage image_;
        VkImageView image_view_;
        VkDeviceMemory memory_; // We're creating a low number of allocations for this library, so we just allocate a dedicated memory object per buffer. Normally you'd want to do sub-allocations of a larger allocation.
    };
}
