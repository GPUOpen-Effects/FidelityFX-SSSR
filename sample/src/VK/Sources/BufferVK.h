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
#pragma once

#include <vulkan/vulkan.h>
namespace SSSR_SAMPLE_VK
{
	/**
		The BufferVK class is a helper class to create and destroy buffers on Vulkan.
	*/
	class BufferVK
	{
	public:
		struct CreateInfo
		{
			VkDeviceSize sizeInBytes;
			VkMemoryPropertyFlags memoryPropertyFlags;
			VkBufferUsageFlags bufferUsage;
			VkFormat format;
		};

		BufferVK();
		~BufferVK();
		void OnDestroy();

		BufferVK(VkDevice device, VkPhysicalDevice physicalDevice, const CreateInfo& createInfo, const char* name);

		BufferVK(BufferVK&& other) noexcept;
		BufferVK& BufferVK::operator =(BufferVK&& other) noexcept;

		void Map(void** data);
		void Unmap();

		VkDevice m_device;
		VkBuffer m_buffer;
		VkBufferView m_bufferView;
		VkDeviceMemory m_memory; // We're creating a low number of allocations for this library, so we just allocate a dedicated memory object per buffer. Normally you'd want to do sub-allocations of a larger allocation.
		bool m_mappable;
		bool m_mapped;
	};
}
