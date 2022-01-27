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

#include "Base/Texture.h"

#include <vulkan/vulkan.h>
using namespace CAULDRON_VK;
namespace SSSR_SAMPLE_VK
{
	/**
		The ImageVK class is a helper class to track image layouts on Vulkan.
	*/
	class ImageVK
	{
	public:
		struct CreateInfo
		{
			VkFormat format;
			uint32_t width;
			uint32_t height;
		};

		ImageVK();
		~ImageVK();

		ImageVK(Device* pDevice, const CreateInfo& createInfo, const char* name);
		void OnDestroy();

		ImageVK(ImageVK&& other) noexcept;
		ImageVK& ImageVK::operator =(ImageVK&& other) noexcept;

		VkImageMemoryBarrier Transition(VkImageLayout after);
		VkImage Resource() const;
		VkImageView View() const;

	private:
		VkDevice m_device;
		Texture m_texture;
		VkImageView m_view;
		VkImageLayout m_currentLayout;
	};
}
