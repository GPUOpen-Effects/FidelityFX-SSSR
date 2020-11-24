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
#include "stdafx.h"
#include "BufferVK.h"
#include "memory.h"

namespace SSSR_SAMPLE_VK
{
	/**
		The constructor for the BufferVK class.
	*/
	BufferVK::BufferVK()
		: buffer_(VK_NULL_HANDLE)
		, device_(VK_NULL_HANDLE)
		, memory_(VK_NULL_HANDLE)
		, buffer_view_(VK_NULL_HANDLE)
		, mappable_(false)
		, mapped_(false)
	{
	}

	/**
		The destructor for the BufferVK class.
	*/
	BufferVK::~BufferVK()
	{

	}

	void BufferVK::OnDestroy()
	{
		if (mapped_)
		{
			Unmap();
		}

		if (buffer_)
		{
			vkDestroyBuffer(device_, buffer_, nullptr);
			buffer_ = VK_NULL_HANDLE;
		}

		if (memory_)
		{
			vkFreeMemory(device_, memory_, nullptr);
			memory_ = VK_NULL_HANDLE;
		}

		if (buffer_view_)
		{
			vkDestroyBufferView(device_, buffer_view_, nullptr);
			buffer_view_ = VK_NULL_HANDLE;
		}

		device_ = VK_NULL_HANDLE;
	}

	/**
		The constructor for the BufferVK class.

		\param device The VkDevice that creates the buffer view.
		\param physical_device The VkPhysicalDevice to determine the right memory heap.
		\param create_info The CreateInfo struct.
	*/
	BufferVK::BufferVK(VkDevice device, VkPhysicalDevice physical_device, const CreateInfo& create_info)
		: device_(device)
		, buffer_(VK_NULL_HANDLE)
		, memory_(VK_NULL_HANDLE)
		, buffer_view_(VK_NULL_HANDLE)
		, mappable_(false)
		, mapped_(false)
	{
		VkBufferCreateInfo buffer_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		buffer_create_info.pNext = nullptr;
		buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		buffer_create_info.size = create_info.size_in_bytes_;
		buffer_create_info.usage = create_info.buffer_usage_;
		VkResult vkResult = vkCreateBuffer(device_, &buffer_create_info, nullptr, &buffer_);
		assert(VK_SUCCESS == vkResult);

		VkMemoryRequirements memory_requirements = {};
		vkGetBufferMemoryRequirements(device_, buffer_, &memory_requirements);

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
		assert(memory_type_index != -1);

		if (create_info.memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			mappable_ = true;
			mapped_ = false;
		}

		VkMemoryAllocateInfo memory_allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		memory_allocate_info.pNext = nullptr;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = memory_type_index;

		vkResult = vkAllocateMemory(device_, &memory_allocate_info, nullptr, &memory_);
		assert(VK_SUCCESS == vkResult);
		vkResult = vkBindBufferMemory(device_, buffer_, memory_, 0);
		assert(VK_SUCCESS == vkResult);


		PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
		if (vkSetDebugUtilsObjectName)
		{
			VkDebugUtilsObjectNameInfoEXT object_name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
			object_name_info.pNext = nullptr;
			object_name_info.objectType = VK_OBJECT_TYPE_BUFFER;
			object_name_info.objectHandle = reinterpret_cast<uint64_t>(buffer_);
			object_name_info.pObjectName = create_info.name_;

			vkResult = vkSetDebugUtilsObjectName(device, &object_name_info);
			assert(VK_SUCCESS == vkResult);
		}

		if (create_info.format_ == VK_FORMAT_UNDEFINED)
		{
			return;
		}

		VkBufferViewCreateInfo buffer_view_create_info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		buffer_view_create_info.pNext = nullptr;
		buffer_view_create_info.flags = 0;
		buffer_view_create_info.buffer = buffer_;
		buffer_view_create_info.format = create_info.format_;
		buffer_view_create_info.offset = 0;
		buffer_view_create_info.range = VK_WHOLE_SIZE;

		vkResult = vkCreateBufferView(device_, &buffer_view_create_info, nullptr, &buffer_view_);
		assert(VK_SUCCESS == vkResult);
	}

	/**
		The constructor for the BufferVK class.

		\param other The buffer to be moved.
	*/
	BufferVK::BufferVK(BufferVK&& other) noexcept
		: buffer_(other.buffer_)
		, memory_(other.memory_)
		, device_(other.device_)
		, buffer_view_(other.buffer_view_)
		, mappable_(other.mappable_)
		, mapped_(other.mapped_)
	{
		other.buffer_ = VK_NULL_HANDLE;
		other.memory_ = VK_NULL_HANDLE;
		other.device_ = VK_NULL_HANDLE;
		other.buffer_view_ = VK_NULL_HANDLE;
		other.mappable_ = false;
		other.mapped_ = false;
	}

	/**
		Assigns the buffer.

		\param other The buffer to be moved.
		\return The assigned buffer.
	*/
	BufferVK& BufferVK::operator=(BufferVK&& other) noexcept
	{
		if (this != &other)
		{
			buffer_ = other.buffer_;
			memory_ = other.memory_;
			device_ = other.device_;
			buffer_view_ = other.buffer_view_;
			mappable_ = other.mappable_;
			mapped_ = other.mapped_;

			other.buffer_ = VK_NULL_HANDLE;
			other.memory_ = VK_NULL_HANDLE;
			other.device_ = VK_NULL_HANDLE;
			other.buffer_view_ = VK_NULL_HANDLE;
			other.mappable_ = false;
			other.mapped_ = false;
		}

		return *this;
	}

	void BufferVK::Map(void** data)
	{
		assert(mappable_ == true);
		assert(mapped_ == false);

		VkResult vkResult = vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, data);
		assert(VK_SUCCESS == vkResult);

		mapped_ = true;
	}

	void BufferVK::Unmap()
	{
		assert(mappable_ == true);
		assert(mapped_ == true);
		vkUnmapMemory(device_, memory_);
		mapped_ = false;
	}
}
