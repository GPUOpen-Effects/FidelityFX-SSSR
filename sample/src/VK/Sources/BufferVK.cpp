/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

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
		: m_buffer(VK_NULL_HANDLE)
		, m_device(VK_NULL_HANDLE)
		, m_memory(VK_NULL_HANDLE)
		, m_bufferView(VK_NULL_HANDLE)
		, m_mappable(false)
		, m_mapped(false)
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
		if (m_mapped)
		{
			Unmap();
		}

		if (m_buffer)
		{
			vkDestroyBuffer(m_device, m_buffer, nullptr);
			m_buffer = VK_NULL_HANDLE;
		}

		if (m_memory)
		{
			vkFreeMemory(m_device, m_memory, nullptr);
			m_memory = VK_NULL_HANDLE;
		}

		if (m_bufferView)
		{
			vkDestroyBufferView(m_device, m_bufferView, nullptr);
			m_bufferView = VK_NULL_HANDLE;
		}

		m_device = VK_NULL_HANDLE;
	}

	/**
		The constructor for the BufferVK class.

		\param device The VkDevice that creates the buffer view.
		\param physicalDevice The VkPhysicalDevice to determine the right memory heap.
		\param createInfo The CreateInfo struct.
	*/
	BufferVK::BufferVK(VkDevice device, VkPhysicalDevice physicalDevice, const CreateInfo& createInfo, const char* name)
		: m_device(device)
		, m_buffer(VK_NULL_HANDLE)
		, m_memory(VK_NULL_HANDLE)
		, m_bufferView(VK_NULL_HANDLE)
		, m_mappable(false)
		, m_mapped(false)
	{
		VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferCreateInfo.pNext = nullptr;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.size = createInfo.sizeInBytes;
		bufferCreateInfo.usage = createInfo.bufferUsage;
		VkResult vkResult = vkCreateBuffer(m_device, &bufferCreateInfo, nullptr, &m_buffer);
		assert(VK_SUCCESS == vkResult);

		VkMemoryRequirements memoryRequirements = {};
		vkGetBufferMemoryRequirements(m_device, m_buffer, &memoryRequirements);

		VkPhysicalDeviceMemoryProperties memoryProperties = {};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

		// find the right memory type for this image
		int memoryTypeIndex = -1;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			const VkMemoryType& memoryType = memoryProperties.memoryTypes[i];
			bool hasRequiredProperties = memoryType.propertyFlags & createInfo.memoryPropertyFlags;
			bool isRequiredMemoryType = memoryRequirements.memoryTypeBits & (1 << i);
			if (hasRequiredProperties && isRequiredMemoryType)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		// abort if we couldn't find the right memory type
		assert(memoryTypeIndex != -1);

		if (createInfo.memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			m_mappable = true;
			m_mapped = false;
		}

		VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		memoryAllocateInfo.pNext = nullptr;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;

		vkResult = vkAllocateMemory(m_device, &memoryAllocateInfo, nullptr, &m_memory);
		assert(VK_SUCCESS == vkResult);
		vkResult = vkBindBufferMemory(m_device, m_buffer, m_memory, 0);
		assert(VK_SUCCESS == vkResult);


		PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
		if (vkSetDebugUtilsObjectName)
		{
			VkDebugUtilsObjectNameInfoEXT objectNameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
			objectNameInfo.pNext = nullptr;
			objectNameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
			objectNameInfo.objectHandle = reinterpret_cast<uint64_t>(m_buffer);
			objectNameInfo.pObjectName = name;

			vkResult = vkSetDebugUtilsObjectName(device, &objectNameInfo);
			assert(VK_SUCCESS == vkResult);
		}

		if (createInfo.format == VK_FORMAT_UNDEFINED)
		{
			return;
		}

		VkBufferViewCreateInfo bufferViewCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		bufferViewCreateInfo.pNext = nullptr;
		bufferViewCreateInfo.flags = 0;
		bufferViewCreateInfo.buffer = m_buffer;
		bufferViewCreateInfo.format = createInfo.format;
		bufferViewCreateInfo.offset = 0;
		bufferViewCreateInfo.range = VK_WHOLE_SIZE;

		vkResult = vkCreateBufferView(m_device, &bufferViewCreateInfo, nullptr, &m_bufferView);
		assert(VK_SUCCESS == vkResult);
	}

	/**
		The constructor for the BufferVK class.

		\param other The buffer to be moved.
	*/
	BufferVK::BufferVK(BufferVK&& other) noexcept
		: m_buffer(other.m_buffer)
		, m_memory(other.m_memory)
		, m_device(other.m_device)
		, m_bufferView(other.m_bufferView)
		, m_mappable(other.m_mappable)
		, m_mapped(other.m_mapped)
	{
		other.m_buffer = VK_NULL_HANDLE;
		other.m_memory = VK_NULL_HANDLE;
		other.m_device = VK_NULL_HANDLE;
		other.m_bufferView = VK_NULL_HANDLE;
		other.m_mappable = false;
		other.m_mapped = false;
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
			m_buffer = other.m_buffer;
			m_memory = other.m_memory;
			m_device = other.m_device;
			m_bufferView = other.m_bufferView;
			m_mappable = other.m_mappable;
			m_mapped = other.m_mapped;

			other.m_buffer = VK_NULL_HANDLE;
			other.m_memory = VK_NULL_HANDLE;
			other.m_device = VK_NULL_HANDLE;
			other.m_bufferView = VK_NULL_HANDLE;
			other.m_mappable = false;
			other.m_mapped = false;
		}

		return *this;
	}

	void BufferVK::Map(void** data)
	{
		assert(m_mappable == true);
		assert(m_mapped == false);

		VkResult vkResult = vkMapMemory(m_device, m_memory, 0, VK_WHOLE_SIZE, 0, data);
		assert(VK_SUCCESS == vkResult);

		m_mapped = true;
	}

	void BufferVK::Unmap()
	{
		assert(m_mappable == true);
		assert(m_mapped == true);
		vkUnmapMemory(m_device, m_memory);
		m_mapped = false;
	}
}
