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
#include "ImageVK.h"
#include "memory.h"

namespace SSSR_SAMPLE_VK
{
	/**
		The constructor for the ImageVK class.
	*/
	ImageVK::ImageVK()
		: m_device(VK_NULL_HANDLE)
		, m_texture()
		, m_view(VK_NULL_HANDLE)
		, m_currentLayout(VK_IMAGE_LAYOUT_UNDEFINED)
	{
	}

	/**
		The destructor for the ImageVK class.
	*/
	ImageVK::~ImageVK()
	{

	}

	void ImageVK::OnDestroy()
	{
		if (m_view)
		{
			vkDestroyImageView(m_device, m_view, nullptr);
		}

		m_texture.OnDestroy();
		m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		m_device = VK_NULL_HANDLE;
	}

	/**
		The constructor for the ImageVK class.

		\param device The VkDevice that creates the image resources.
		\param createInfo The CreateInfo struct.
	*/
	ImageVK::ImageVK(Device* pDevice, const CreateInfo& createInfo, const char* name)
		: m_device(pDevice->GetDevice())
	{
		VkImageCreateInfo imgCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imgCreateInfo.pNext = nullptr;
		imgCreateInfo.flags = 0;
		imgCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imgCreateInfo.arrayLayers = 1;
		imgCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imgCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imgCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imgCreateInfo.queueFamilyIndexCount = 0;
		imgCreateInfo.pQueueFamilyIndices = nullptr;
		imgCreateInfo.mipLevels = 1;
		imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imgCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		
		imgCreateInfo.extent = { createInfo.width, createInfo.height, 1 };
		imgCreateInfo.format = createInfo.format;
		m_texture.Init(pDevice, &imgCreateInfo, name);
		m_texture.CreateSRV(&m_view);
		m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	/**
		The constructor for the ImageVK class.

		\param other The buffer to be moved.
	*/
	ImageVK::ImageVK(ImageVK&& other) noexcept
		: m_device(other.m_device)
		, m_texture(other.m_texture)
		, m_view(other.m_view)
		, m_currentLayout(other.m_currentLayout)
	{
		other.m_device = VK_NULL_HANDLE;
		other.m_texture = {};
		other.m_view = VK_NULL_HANDLE;
		other.m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	/**
		Assigns the image.

		\param other The image to be moved.
		\return The assigned buffer.
	*/
	ImageVK& ImageVK::operator=(ImageVK&& other) noexcept
	{
		if (this != &other)
		{
			m_device = other.m_device;
			m_texture = other.m_texture;
			m_view = other.m_view;
			m_currentLayout = other.m_currentLayout;

			other.m_device = VK_NULL_HANDLE;
			other.m_texture = {};
			other.m_view = VK_NULL_HANDLE;
			other.m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
		return *this;
	}

	const char* ToString(VkImageLayout layout)
	{
		switch(layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			return "VK_IMAGE_LAYOUT_UNDEFINED";
		case VK_IMAGE_LAYOUT_GENERAL:
			return "VK_IMAGE_LAYOUT_GENERAL";
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return "VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL";
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return "VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return "VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL";
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL";
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL";
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL";
		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			return "VK_IMAGE_LAYOUT_PREINITIALIZED";
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
			return "VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL";
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
			return "VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL";
		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
			return "VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL";
		case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
			return "VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL";
		case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
			return "VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL";
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			return "VK_IMAGE_LAYOUT_PRESENT_SRC_KHR";
		case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
			return "VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR";
		case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR:
			return "VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR";
		case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR:
			return "VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR";
		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR:
			return "VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR";
		case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV:
			return "VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV";
		default: return "LAYOUT_NOT_IMPLEMENTED";
		}
	}

	VkImageMemoryBarrier ImageVK::Transition(VkImageLayout after)
	{
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;

		barrier.image = m_texture.Resource();
		barrier.oldLayout = m_currentLayout;
		barrier.newLayout = after;

		m_currentLayout = after;
		return barrier;
	}

	VkImage ImageVK::Resource() const
	{
		return m_texture.Resource();
	}

	VkImageView ImageVK::View() const
	{
		return m_view;
	}
}
