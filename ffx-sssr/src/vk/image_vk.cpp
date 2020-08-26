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
#include "image_vk.h"
#include "memory.h"

namespace ffx_sssr
{
    /**
        The constructor for the ImageVK class.
    */
    ImageVK::ImageVK()
        : image_(VK_NULL_HANDLE)
        , device_(VK_NULL_HANDLE)
        , memory_(VK_NULL_HANDLE)
        , image_view_(VK_NULL_HANDLE)
    {
    }

    /**
        The destructor for the ImageVK class.
    */
    ImageVK::~ImageVK()
    {
        if (image_)
        {
            vkDestroyImage(device_, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }

        if (memory_)
        {
            vkFreeMemory(device_, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }

        if (image_view_)
        {
            vkDestroyImageView(device_, image_view_, nullptr);
            image_view_ = VK_NULL_HANDLE;
        }

        device_ = VK_NULL_HANDLE;
    }

    /**
        The constructor for the ImageVK class.

        \param device The VkDevice that creates the image view.
        \param physical_device The VkPhysicalDevice to determine the right memory heap.
        \param create_info The create info.
    */
    ImageVK::ImageVK(VkDevice device, VkPhysicalDevice physical_device, const CreateInfo & create_info)
        : device_(device)
    {
        VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_create_info.pNext = nullptr;
        image_create_info.flags = 0;
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = create_info.format_;
        image_create_info.extent = { create_info.width_, create_info.height_, 1 };
        image_create_info.mipLevels = create_info.mip_levels_;
        image_create_info.arrayLayers = 1;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = create_info.image_usage_;
        image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.queueFamilyIndexCount = 0;
        image_create_info.pQueueFamilyIndices = nullptr;
        image_create_info.initialLayout = create_info.initial_layout_;
        if (VK_SUCCESS != vkCreateImage(device, &image_create_info, nullptr, &image_))
        {
            throw reflection_error(FFX_SSSR_STATUS_INTERNAL_ERROR);
        }

        VkMemoryRequirements memory_requirements = {};
        vkGetImageMemoryRequirements(device, image_, &memory_requirements);

        VkPhysicalDeviceMemoryProperties memory_properties = {};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

        // find the right memory type for this image
        int memory_type_index = -1;
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
        {
            const VkMemoryType& memory_type = memory_properties.memoryTypes[i];
            bool has_required_properties = memory_type.propertyFlags & create_info.memory_property_flags;
            bool is_required_memory_type = memory_requirements.memoryTypeBits & (1 << i);
            if (has_required_properties && is_required_memory_type)
            {
                memory_type_index = i;
                break;
            }
        }

        // abort if we couldn't find the right memory type
        if (memory_type_index == -1)
        {
            throw reflection_error(FFX_SSSR_STATUS_INTERNAL_ERROR);
        }

        VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memory_allocate_info.pNext = nullptr;
        memory_allocate_info.allocationSize = memory_requirements.size;
        memory_allocate_info.memoryTypeIndex = memory_type_index;
        if (VK_SUCCESS != vkAllocateMemory(device, &memory_allocate_info, nullptr, &memory_))
        {
            throw reflection_error(FFX_SSSR_STATUS_OUT_OF_MEMORY);
        }

        if (VK_SUCCESS != vkBindImageMemory(device_, image_, memory_, 0))
        {
            throw reflection_error(FFX_SSSR_STATUS_INTERNAL_ERROR);
        }

        VkImageSubresourceRange subresource_range = {};
        subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_range.baseMipLevel = 0;
        subresource_range.levelCount = create_info.mip_levels_;
        subresource_range.baseArrayLayer = 0;
        subresource_range.layerCount = 1;

        VkImageViewCreateInfo image_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        image_view_create_info.pNext = VK_NULL_HANDLE;
        image_view_create_info.flags = 0;
        image_view_create_info.image = image_;
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = create_info.format_;
        image_view_create_info.subresourceRange = subresource_range;
        if (VK_SUCCESS != vkCreateImageView(device_, &image_view_create_info, nullptr, &image_view_))
        {
            throw reflection_error(FFX_SSSR_STATUS_INTERNAL_ERROR);
        }

        FFX_SSSR_ASSERT(create_info.name_); // require all library objects to be named.
        PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
        if (vkSetDebugUtilsObjectName)
        {
            VkDebugUtilsObjectNameInfoEXT object_name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            object_name_info.pNext = nullptr;
            object_name_info.objectType = VK_OBJECT_TYPE_IMAGE;
            object_name_info.objectHandle = reinterpret_cast<uint64_t>(image_);
            object_name_info.pObjectName = create_info.name_;

            VkResult result = vkSetDebugUtilsObjectName(device, &object_name_info);
            FFX_SSSR_ASSERT(result == VK_SUCCESS);
        }
    }

    /**
        The constructor for the ImageVK class.

        \param other The image to be moved.
    */
    ImageVK::ImageVK(ImageVK && other) noexcept
        : image_(other.image_)
        , device_(other.device_)
        , image_view_(other.image_view_)
        , memory_(other.memory_)
    {
        other.image_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.image_view_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }

    /**
        Assigns the image.

        \param other The image to be moved.
        \return The assigned image.
    */
    ImageVK & ImageVK::operator=(ImageVK && other) noexcept
    {
        if (this != &other)
        {
            image_ = other.image_;
            device_ = other.device_;
            image_view_ = other.image_view_;
            memory_ = other.memory_;

            other.image_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
            other.image_view_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
        }

        return *this;
    }
}
