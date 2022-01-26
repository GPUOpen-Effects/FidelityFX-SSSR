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

#include "SampleRenderer.h"
#include <deque>

#undef max
#undef min

namespace SSSR_SAMPLE_VK
{
	//--------------------------------------------------------------------------------------
	//
	// OnCreate
	//
	//--------------------------------------------------------------------------------------
	void SampleRenderer::OnCreate(Device* pDevice, SwapChain* pSwapChain)
	{
		m_pDevice = pDevice;
		m_CurrentBackbufferIndex = 0;

		// Initialize helpers

		// Create all the heaps for the resources views
		const uint32_t cbvDescriptorCount = 2000;
		const uint32_t srvDescriptorCount = 2000;
		const uint32_t uavDescriptorCount = 10;
		const uint32_t samplerDescriptorCount = 20;
		m_ResourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, samplerDescriptorCount);

		// Create a commandlist ring for the Direct queue
		uint32_t commandListsPerBackBuffer = 8;
		m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer);

		// Create a 'dynamic' constant buffer
		const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
		m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, "Uniforms");

		// Create a 'static' pool for vertices and indices 
		const uint32_t staticGeometryMemSize = 128 * 1024 * 1024;
		const uint32_t systemGeometryMemSize = 32 * 1024;
		m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, USE_VID_MEM, "StaticGeom");
		m_SysMemBufferPool.OnCreate(pDevice, systemGeometryMemSize, false, "PostProcGeom");

		// initialize the GPU time stamps module
		m_GPUTimer.OnCreate(pDevice, backBufferCount);

		// Quick helper to upload resources, it has it's own commandList and uses suballocation.
		const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
		m_UploadHeap.OnCreate(pDevice, staticGeometryMemSize);    // initialize an upload heap (uses suballocation for faster results)

		CreateApplyReflectionsPipeline();
		CreateDepthDownsamplePipeline();

		// Create a command buffer for upload
		m_CommandListRing.OnBeginFrame();

		VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerCreateInfo.pNext = nullptr;
		samplerCreateInfo.flags = 0;
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.mipLodBias = 0;
		samplerCreateInfo.anisotropyEnable = false;
		samplerCreateInfo.maxAnisotropy = 0;
		samplerCreateInfo.compareEnable = false;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0;
		samplerCreateInfo.maxLod = 16;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		samplerCreateInfo.unnormalizedCoordinates = false;
		if (VK_SUCCESS != vkCreateSampler(m_pDevice->GetDevice(), &samplerCreateInfo, nullptr, &m_LinearSampler))
		{
			Trace("Failed to create linear sampler.");
		}

		// Create a 2Kx2K Shadowmap atlas to hold 4 cascades/spotlights
		m_ShadowMap.InitDepthStencil(m_pDevice, 2 * 1024, 2 * 1024, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, "ShadowMap");
		m_ShadowMap.CreateSRV(&m_ShadowMapSRV);
		m_ShadowMap.CreateDSV(&m_ShadowMapDSV);

		// Create render pass shadow
		//
		{
			VkAttachmentDescription depthAttachments;
			AttachClearBeforeUse(m_ShadowMap.GetFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachments);
			m_RenderPassShadow = CreateRenderPassOptimal(m_pDevice->GetDevice(), 0, NULL, &depthAttachments);

			// Create frame buffer
			//
			VkImageView attachmentViews[1] = { m_ShadowMapDSV };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.pNext = NULL;
			framebufferInfo.renderPass = m_RenderPassShadow;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachmentViews;
			framebufferInfo.width = m_ShadowMap.GetWidth();
			framebufferInfo.height = m_ShadowMap.GetHeight();
			framebufferInfo.layers = 1;
			VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &framebufferInfo, NULL, &m_FramebufferShadows);
			assert(res == VK_SUCCESS);
		}

		// Create motion vector render pass
		//
		{
			VkAttachmentDescription colorAttachments[2], depthAttachment;
			// motion vector RT
			AttachClearBeforeUse(VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &colorAttachments[0]);
			// normals RT
			AttachClearBeforeUse(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &colorAttachments[1]);
			// depth RT
			AttachClearBeforeUse(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, &depthAttachment);
			m_RenderPassMV = CreateRenderPassOptimal(m_pDevice->GetDevice(), _countof(colorAttachments), colorAttachments, &depthAttachment);
		}

		// Create HDR render pass color with color clear
		//
		{
			VkAttachmentDescription colorAttachments[1], depthAttachment;
			// color RT
			AttachClearBeforeUse(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &colorAttachments[0]);
			// depth RT
			AttachBlending(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, &depthAttachment);
			m_RenderPassClearHDR = CreateRenderPassOptimal(m_pDevice->GetDevice(), _countof(colorAttachments), colorAttachments, &depthAttachment);
		}

		// Create PBR render pass
		//
		{
			VkAttachmentDescription colorAttachments[2], depthAttachment;
			// color RT
			AttachBlending(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &colorAttachments[0]);
			// specular roughness RT
			AttachClearBeforeUse(VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &colorAttachments[1]);
			// depth RT
			AttachBlending(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, &depthAttachment);
			m_RenderPassPBR = CreateRenderPassOptimal(m_pDevice->GetDevice(), _countof(colorAttachments), colorAttachments, &depthAttachment);
		}

		// Create HDR render pass color without clear
		//
		{
			VkAttachmentDescription colorAttachments[1], depthAttachment;
			// color RT
			AttachBlending(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &colorAttachments[0]);
			// depth RT
			AttachBlending(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachment);
			m_RenderPassHDR = CreateRenderPassOptimal(m_pDevice->GetDevice(), _countof(colorAttachments), colorAttachments, &depthAttachment);
		}

		m_SkyDome.OnCreate(pDevice, m_RenderPassHDR, &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\papermill\\diffuse.dds", "..\\media\\envmaps\\papermill\\specular.dds", VK_SAMPLE_COUNT_1_BIT);
		m_AmbientLight.OnCreate(pDevice, m_RenderPassHDR, &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\white\\diffuse.dds", "..\\media\\envmaps\\white\\specular.dds", VK_SAMPLE_COUNT_1_BIT);
		m_SkyDomeProc.OnCreate(pDevice, m_RenderPassHDR, &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_1_BIT);
		m_Wireframe.OnCreate(pDevice, m_RenderPassHDR, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_1_BIT);
		m_WireframeBox.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
		m_DownSample.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);
		m_Bloom.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);

		VkCommandBuffer cb1 = BeginNewCommandBuffer();
		m_Sssr.OnCreate(pDevice, cb1, &m_ResourceViewHeaps, &m_ConstantBufferRing, backBufferCount, true);
		// Wait for the upload to finish;
		SubmitCommandBuffer(cb1);
		m_pDevice->GPUFlush();

		// Create tonemapping pass
		m_ToneMapping.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_ResourceViewHeaps, &m_SysMemBufferPool, &m_ConstantBufferRing);

		// Initialize UI rendering resources
		m_ImGUI.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_UploadHeap, &m_ConstantBufferRing);

		m_BrdfLut.InitFromFile(pDevice, &m_UploadHeap, "BrdfLut.dds", false); // LUT images are stored as linear
		m_BrdfLut.CreateSRV(&m_BrdfLutSRV);

		// Make sure upload heap has finished uploading before continuing
#if (USE_VID_MEM==true)
		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
		m_UploadHeap.FlushAndFinish();
#endif
	}

	//--------------------------------------------------------------------------------------
	//
	// OnDestroy 
	//
	//--------------------------------------------------------------------------------------
	void SampleRenderer::OnDestroy()
	{
		m_ImGUI.OnDestroy();
		m_ToneMapping.OnDestroy();
		m_Bloom.OnDestroy();
		m_DownSample.OnDestroy();
		m_WireframeBox.OnDestroy();
		m_Wireframe.OnDestroy();
		m_SkyDomeProc.OnDestroy();
		m_SkyDome.OnDestroy();
		m_AmbientLight.OnDestroy();
		m_ShadowMap.OnDestroy();
		m_BrdfLut.OnDestroy();
		m_Sssr.OnDestroy();

		VkDevice device = m_pDevice->GetDevice();

		vkDestroySampler(device, m_LinearSampler, nullptr);
		vkDestroyImageView(device, m_BrdfLutSRV, nullptr);
		vkDestroyImageView(device, m_ShadowMapDSV, nullptr);
		vkDestroyImageView(device, m_ShadowMapSRV, nullptr);

		vkDestroyPipeline(device, m_DepthDownsamplePipeline, nullptr);
		vkDestroyPipelineLayout(device, m_DepthDownsamplePipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, m_DepthDownsampleDescriptorSetLayout, nullptr);
		m_ResourceViewHeaps.FreeDescriptor(m_DepthDownsampleDescriptorSet);

		vkDestroyPipeline(device, m_ApplyPipeline, nullptr);
		vkDestroyPipelineLayout(device, m_ApplyPipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, m_ApplyPipelineDescriptorSetLayout, nullptr);

		for (int i = 0; i < backBufferCount; ++i)
		{
			m_ResourceViewHeaps.FreeDescriptor(m_ApplyPipelineDescriptorSet[i]);
		}

		vkDestroyRenderPass(device, m_RenderPassShadow, nullptr);
		vkDestroyRenderPass(device, m_RenderPassClearHDR, nullptr);
		vkDestroyRenderPass(device, m_RenderPassHDR, nullptr);
		vkDestroyRenderPass(device, m_RenderPassPBR, nullptr);
		vkDestroyRenderPass(device, m_RenderPassMV, nullptr);
		vkDestroyRenderPass(device, m_RenderPassApply, nullptr);

		vkDestroyFramebuffer(device, m_FramebufferShadows, nullptr);

		m_UploadHeap.OnDestroy();
		m_GPUTimer.OnDestroy();
		m_VidMemBufferPool.OnDestroy();
		m_SysMemBufferPool.OnDestroy();
		m_ConstantBufferRing.OnDestroy();
		m_ResourceViewHeaps.OnDestroy();
		m_CommandListRing.OnDestroy();
	}

	//--------------------------------------------------------------------------------------
	//
	// OnCreateWindowSizeDependentResources
	//
	//--------------------------------------------------------------------------------------
	void SampleRenderer::OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height)
	{
		m_Width = Width;
		m_Height = Height;

		// Set the viewport
		m_Viewport.x = 0;
		m_Viewport.y = (float)m_Height;
		m_Viewport.width = (float)m_Width;
		m_Viewport.height = -(float)(m_Height);
		m_Viewport.minDepth = (float)0.0f;
		m_Viewport.maxDepth = (float)1.0f;

		// Create scissor rectangle
		m_Scissor.extent.width = m_Width;
		m_Scissor.extent.height = m_Height;
		m_Scissor.offset.x = 0;
		m_Scissor.offset.y = 0;

		// Create depth buffer
		m_DepthBuffer.InitDepthStencil(m_pDevice, m_Width, m_Height, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, "DepthBuffer");
		m_DepthBuffer.CreateSRV(&m_DepthBufferSRV);
		m_DepthBuffer.CreateDSV(&m_DepthBufferDSV);

		// Create Texture + RTV
		m_HDR.InitRenderTarget(m_pDevice, m_Width, m_Height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT), false, "HDR");
		m_HDR.CreateSRV(&m_HDRSRV);

		VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageCreateInfo.pNext = nullptr;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.extent = { m_Width, m_Height, 1 };
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		imageCreateInfo.flags = 0;

		m_NormalBuffer.InitRenderTarget(m_pDevice, m_Width, m_Height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT), false, "m_NormalBuffer");
		m_NormalBuffer.CreateSRV(&m_NormalBufferSRV);

		imageCreateInfo.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		m_NormalHistoryBuffer.Init(m_pDevice, &imageCreateInfo, "m_NormalHistoryBuffer");
		m_NormalHistoryBuffer.CreateSRV(&m_NormalHistoryBufferSRV);

		m_SpecularRoughness.InitRenderTarget(m_pDevice, m_Width, m_Height, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT), false, "m_SpecularRoughness");
		m_SpecularRoughness.CreateSRV(&m_SpecularRoughnessSRV);

		m_MotionVectors.InitRenderTarget(m_pDevice, m_Width, m_Height, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT), false, "m_MotionVector");
		m_MotionVectors.CreateSRV(&m_MotionVectorsSRV);

		// Create framebuffer for the RT
		{
			VkImageView hdrAttachments[2] = { m_HDRSRV, m_DepthBufferDSV };

			VkFramebufferCreateInfo hdrFramebufferInfo = {};
			hdrFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			hdrFramebufferInfo.pNext = NULL;
			hdrFramebufferInfo.renderPass = m_RenderPassHDR;
			hdrFramebufferInfo.attachmentCount = _countof(hdrAttachments);
			hdrFramebufferInfo.pAttachments = hdrAttachments;
			hdrFramebufferInfo.width = m_Width;
			hdrFramebufferInfo.height = m_Height;
			hdrFramebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &hdrFramebufferInfo, NULL, &m_FramebufferHDR);
			assert(res == VK_SUCCESS);
		}

		{
			VkImageView pbrAttachments[3] = { m_HDRSRV, m_SpecularRoughnessSRV, m_DepthBufferDSV };

			VkFramebufferCreateInfo pbrFramebufferInfo = {};
			pbrFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			pbrFramebufferInfo.pNext = NULL;
			pbrFramebufferInfo.renderPass = m_RenderPassPBR;
			pbrFramebufferInfo.attachmentCount = _countof(pbrAttachments);
			pbrFramebufferInfo.pAttachments = pbrAttachments;
			pbrFramebufferInfo.width = m_Width;
			pbrFramebufferInfo.height = m_Height;
			pbrFramebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &pbrFramebufferInfo, NULL, &m_FramebufferPBR);
			assert(res == VK_SUCCESS);
		}

		{
			VkImageView mvAttachments[3] = { m_MotionVectorsSRV, m_NormalBufferSRV, m_DepthBufferDSV };

			VkFramebufferCreateInfo mvFramebufferInfo = {};
			mvFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			mvFramebufferInfo.pNext = NULL;
			mvFramebufferInfo.renderPass = m_RenderPassMV;
			mvFramebufferInfo.attachmentCount = _countof(mvAttachments);
			mvFramebufferInfo.pAttachments = mvAttachments;
			mvFramebufferInfo.width = m_Width;
			mvFramebufferInfo.height = m_Height;
			mvFramebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &mvFramebufferInfo, NULL, &m_FramebufferMV);
			assert(res == VK_SUCCESS);
		}

		{
			m_HDR.CreateRTV(&m_ApplyPipelineRTV);
			VkImageView attachmentViews[1] = { m_ApplyPipelineRTV };

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.pNext = NULL;
			framebufferInfo.renderPass = m_RenderPassApply;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachmentViews;
			framebufferInfo.width = m_Width;
			framebufferInfo.height = m_Height;
			framebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &framebufferInfo, NULL, &m_FramebufferApply);
			assert(res == VK_SUCCESS);
		}

		// update bloom and downscaling effect
		m_DownSample.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_HDR, 6);
		m_Bloom.OnCreateWindowSizeDependentResources(m_Width / 2, m_Height / 2, m_DownSample.GetTexture(), 6, &m_HDR);

		// update the pipelines if the swapchain render pass has changed (for example when the format of the swapchain changes)   
		m_ToneMapping.UpdatePipelines(pSwapChain->GetRenderPass());
		m_ImGUI.UpdatePipeline(pSwapChain->GetRenderPass());

		// Depth downsampling pass with single CS
		{
			m_DepthMipLevelCount = static_cast<uint32_t>(std::log2(std::max(m_Width, m_Height))) + 1;

			// Downsampled depth buffer
			imageCreateInfo.format = VK_FORMAT_R32_SFLOAT;
			imageCreateInfo.mipLevels = m_DepthMipLevelCount;
			m_DepthHierarchy.Init(m_pDevice, &imageCreateInfo, "m_DepthHierarchy");
			for (UINT i = 0; i < std::min(13u, m_DepthMipLevelCount); ++i)
			{
				m_DepthHierarchy.CreateSRV(&m_DepthHierarchyDescriptors[i], i);
			}
			m_DepthHierarchy.CreateSRV(&m_DepthHierarchySRV);

			// Atomic counter

			VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			bufferCreateInfo.pNext = nullptr;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			bufferCreateInfo.size = 4;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.memoryTypeBits = 0;
			allocCreateInfo.pool = VK_NULL_HANDLE;
			allocCreateInfo.preferredFlags = 0;
			allocCreateInfo.pUserData = "m_AtomicCounter";
			allocCreateInfo.requiredFlags = 0;
			allocCreateInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
			if (VK_SUCCESS != vmaCreateBuffer(m_pDevice->GetAllocator(), &bufferCreateInfo, &allocCreateInfo, &m_AtomicCounter, &m_AtomicCounterAllocation, nullptr))
			{
				Trace("Failed to create buffer for atomic counter");
			}

			VkBufferViewCreateInfo bufferViewCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
			bufferViewCreateInfo.buffer = m_AtomicCounter;
			bufferViewCreateInfo.format = VK_FORMAT_R32_UINT;
			bufferViewCreateInfo.range = VK_WHOLE_SIZE;
			bufferViewCreateInfo.flags = 0;
			if (VK_SUCCESS != vkCreateBufferView(m_pDevice->GetDevice(), &bufferViewCreateInfo, nullptr, &m_AtomicCounterUAV))
			{
				Trace("Failed to create buffer view for atomic counter");
			}
		}


		m_CommandListRing.OnBeginFrame();
		VkCommandBuffer cb = BeginNewCommandBuffer();

		//==============Setup SSSR==============
		SSSRCreationInfo sssrInput;
		sssrInput.HDRView = m_HDRSRV;
		sssrInput.DepthHierarchyView = m_DepthHierarchySRV;
		sssrInput.MotionVectorsView = m_MotionVectorsSRV;
		sssrInput.NormalBufferView = m_NormalBufferSRV;
		sssrInput.NormalHistoryBufferView = m_NormalHistoryBufferSRV;
		sssrInput.SpecularRoughnessView = m_SpecularRoughnessSRV;
		sssrInput.EnvironmentMapView = m_SkyDome.GetCubeSpecularTextureView();
		sssrInput.EnvironmentMapSampler = m_SkyDome.GetCubeSpecularTextureSampler();
		sssrInput.pingPongNormal = false;
		sssrInput.pingPongRoughness = false;
		sssrInput.outputWidth = m_Width;
		sssrInput.outputHeight = m_Height;
		m_Sssr.OnCreateWindowSizeDependentResources(cb, sssrInput);

		// Fill apply reflections descriptor set
		VkDescriptorImageInfo applyReflectionsImageInfos[5];
		applyReflectionsImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[0].imageView = m_Sssr.GetOutputTextureView();
		applyReflectionsImageInfos[0].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[1].imageView = m_NormalBufferSRV;
		applyReflectionsImageInfos[1].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[2].imageView = m_SpecularRoughnessSRV;
		applyReflectionsImageInfos[2].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[3].imageView = m_BrdfLutSRV;
		applyReflectionsImageInfos[3].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[4].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		applyReflectionsImageInfos[4].imageView = VK_NULL_HANDLE;
		applyReflectionsImageInfos[4].sampler = m_LinearSampler;

		for (int i = 0; i < backBufferCount; ++i)
		{
			VkWriteDescriptorSet applyReflectionsWriteDescSets[5];
			applyReflectionsWriteDescSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			applyReflectionsWriteDescSets[0].pNext = nullptr;
			applyReflectionsWriteDescSets[0].descriptorCount = 1;
			applyReflectionsWriteDescSets[0].dstArrayElement = 0;
			applyReflectionsWriteDescSets[0].dstSet = m_ApplyPipelineDescriptorSet[i];
			applyReflectionsWriteDescSets[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			applyReflectionsWriteDescSets[0].dstBinding = 0;
			applyReflectionsWriteDescSets[0].pImageInfo = &applyReflectionsImageInfos[0];

			applyReflectionsWriteDescSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			applyReflectionsWriteDescSets[1].pNext = nullptr;
			applyReflectionsWriteDescSets[1].descriptorCount = 1;
			applyReflectionsWriteDescSets[1].dstArrayElement = 0;
			applyReflectionsWriteDescSets[1].dstSet = m_ApplyPipelineDescriptorSet[i];
			applyReflectionsWriteDescSets[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			applyReflectionsWriteDescSets[1].dstBinding = 1;
			applyReflectionsWriteDescSets[1].pImageInfo = &applyReflectionsImageInfos[1];

			applyReflectionsWriteDescSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			applyReflectionsWriteDescSets[2].pNext = nullptr;
			applyReflectionsWriteDescSets[2].descriptorCount = 1;
			applyReflectionsWriteDescSets[2].dstArrayElement = 0;
			applyReflectionsWriteDescSets[2].dstSet = m_ApplyPipelineDescriptorSet[i];
			applyReflectionsWriteDescSets[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			applyReflectionsWriteDescSets[2].dstBinding = 2;
			applyReflectionsWriteDescSets[2].pImageInfo = &applyReflectionsImageInfos[2];

			applyReflectionsWriteDescSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			applyReflectionsWriteDescSets[3].pNext = nullptr;
			applyReflectionsWriteDescSets[3].descriptorCount = 1;
			applyReflectionsWriteDescSets[3].dstArrayElement = 0;
			applyReflectionsWriteDescSets[3].dstSet = m_ApplyPipelineDescriptorSet[i];
			applyReflectionsWriteDescSets[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			applyReflectionsWriteDescSets[3].dstBinding = 3;
			applyReflectionsWriteDescSets[3].pImageInfo = &applyReflectionsImageInfos[3];

			applyReflectionsWriteDescSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			applyReflectionsWriteDescSets[4].pNext = nullptr;
			applyReflectionsWriteDescSets[4].descriptorCount = 1;
			applyReflectionsWriteDescSets[4].dstArrayElement = 0;
			applyReflectionsWriteDescSets[4].dstSet = m_ApplyPipelineDescriptorSet[i];
			applyReflectionsWriteDescSets[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			applyReflectionsWriteDescSets[4].dstBinding = 4;
			applyReflectionsWriteDescSets[4].pImageInfo = &applyReflectionsImageInfos[4];

			vkUpdateDescriptorSets(m_pDevice->GetDevice(), _countof(applyReflectionsWriteDescSets), applyReflectionsWriteDescSets, 0, nullptr);
		}

		// Fill depth downsample descriptor set
		VkDescriptorImageInfo downsampleImageInfos[15];
		downsampleImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		downsampleImageInfos[0].imageView = m_DepthBufferDSV;
		downsampleImageInfos[0].sampler = VK_NULL_HANDLE;

		uint32_t i = 0;
		for (; i < m_DepthMipLevelCount; ++i)
		{
			uint32_t idx = i + 1;
			downsampleImageInfos[idx].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			downsampleImageInfos[idx].imageView = m_DepthHierarchyDescriptors[i];
			downsampleImageInfos[idx].sampler = VK_NULL_HANDLE;
		}

		VkWriteDescriptorSet depthDownsampleWriteDescSets[15];
		depthDownsampleWriteDescSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		depthDownsampleWriteDescSets[0].pNext = nullptr;
		depthDownsampleWriteDescSets[0].descriptorCount = 1;
		depthDownsampleWriteDescSets[0].dstArrayElement = 0;
		depthDownsampleWriteDescSets[0].dstSet = m_DepthDownsampleDescriptorSet;
		depthDownsampleWriteDescSets[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		depthDownsampleWriteDescSets[0].dstBinding = 0;
		depthDownsampleWriteDescSets[0].pImageInfo = &downsampleImageInfos[0];

		i = 0;
		for (; i < m_DepthMipLevelCount; ++i)
		{
			uint32_t idx = i + 1;
			depthDownsampleWriteDescSets[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			depthDownsampleWriteDescSets[idx].pNext = nullptr;
			depthDownsampleWriteDescSets[idx].descriptorCount = 1;
			depthDownsampleWriteDescSets[idx].dstArrayElement = i;
			depthDownsampleWriteDescSets[idx].dstSet = m_DepthDownsampleDescriptorSet;
			depthDownsampleWriteDescSets[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			depthDownsampleWriteDescSets[idx].dstBinding = 1;
			depthDownsampleWriteDescSets[idx].pImageInfo = &downsampleImageInfos[idx];
		}

		// Map the remaining mip levels to the lowest mip
		for (; i < 13; ++i)
		{
			uint32_t idx = i + 1;
			depthDownsampleWriteDescSets[idx].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			depthDownsampleWriteDescSets[idx].pNext = nullptr;
			depthDownsampleWriteDescSets[idx].descriptorCount = 1;
			depthDownsampleWriteDescSets[idx].dstArrayElement = i;
			depthDownsampleWriteDescSets[idx].dstSet = m_DepthDownsampleDescriptorSet;
			depthDownsampleWriteDescSets[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			depthDownsampleWriteDescSets[idx].dstBinding = 1;
			depthDownsampleWriteDescSets[idx].pImageInfo = &downsampleImageInfos[m_DepthMipLevelCount];
		}

		depthDownsampleWriteDescSets[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		depthDownsampleWriteDescSets[14].pNext = nullptr;
		depthDownsampleWriteDescSets[14].descriptorCount = 1;
		depthDownsampleWriteDescSets[14].dstArrayElement = 0;
		depthDownsampleWriteDescSets[14].dstSet = m_DepthDownsampleDescriptorSet;
		depthDownsampleWriteDescSets[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		depthDownsampleWriteDescSets[14].dstBinding = 2;
		depthDownsampleWriteDescSets[14].pTexelBufferView = &m_AtomicCounterUAV;

		vkUpdateDescriptorSets(m_pDevice->GetDevice(), _countof(depthDownsampleWriteDescSets), depthDownsampleWriteDescSets, 0, nullptr);

		// Initial layout transitions
		Barriers(cb, {
			Transition(m_NormalHistoryBuffer.Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			Transition(m_DepthHierarchy.Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, m_DepthMipLevelCount),
			Transition(m_DownSample.GetTexture()->Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 6),
			Transition(m_Sssr.GetOutputTexture()->Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			});

		SubmitCommandBuffer(cb);
	}

	//--------------------------------------------------------------------------------------
	//
	// OnDestroyWindowSizeDependentResources
	//
	//--------------------------------------------------------------------------------------
	void SampleRenderer::OnDestroyWindowSizeDependentResources()
	{
		m_Bloom.OnDestroyWindowSizeDependentResources();
		m_DownSample.OnDestroyWindowSizeDependentResources();
		m_Sssr.OnDestroyWindowSizeDependentResources();

		m_MotionVectors.OnDestroy();
		m_SpecularRoughness.OnDestroy();
		m_NormalBuffer.OnDestroy();
		m_NormalHistoryBuffer.OnDestroy();

		VkDevice device = m_pDevice->GetDevice();

		vkDestroyImageView(device, m_ApplyPipelineRTV, nullptr);
		vkDestroyImageView(device, m_DepthBufferSRV, nullptr);

		for (int i = 0; i < 13; ++i)
		{
			if (m_DepthHierarchyDescriptors[i] != VK_NULL_HANDLE)
			{
				vkDestroyImageView(device, m_DepthHierarchyDescriptors[i], nullptr);
			}
			m_DepthHierarchyDescriptors[i] = VK_NULL_HANDLE;
		}
		vkDestroyImageView(device, m_HDRSRV, nullptr);
		vkDestroyImageView(device, m_DepthHierarchySRV, nullptr);
		vkDestroyImageView(device, m_SpecularRoughnessSRV, nullptr);
		vkDestroyImageView(device, m_NormalBufferSRV, nullptr);
		vkDestroyImageView(device, m_NormalHistoryBufferSRV, nullptr);
		vkDestroyImageView(device, m_MotionVectorsSRV, nullptr);
		vkDestroyImageView(device, m_DepthBufferDSV, nullptr);
		vkDestroyBufferView(device, m_AtomicCounterUAV, nullptr);

		m_HDR.OnDestroy();
		m_DepthBuffer.OnDestroy();
		m_DepthHierarchy.OnDestroy();

		vkDestroyFramebuffer(device, m_FramebufferHDR, nullptr);
		vkDestroyFramebuffer(device, m_FramebufferPBR, nullptr);
		vkDestroyFramebuffer(device, m_FramebufferMV, nullptr);
		vkDestroyFramebuffer(device, m_FramebufferApply, nullptr);

		vmaDestroyBuffer(m_pDevice->GetAllocator(), m_AtomicCounter, m_AtomicCounterAllocation);
	}

	//--------------------------------------------------------------------------------------
	//
	// LoadScene
	//
	//--------------------------------------------------------------------------------------
	int SampleRenderer::LoadScene(GLTFCommon* pGLTFCommon, int stage)
	{
		// show loading progress
		//
		ImGui::OpenPopup("Loading");
		if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			float progress = (float)stage / 13.0f;
			ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
			ImGui::EndPopup();
		}

		AsyncPool* pAsyncPool = &m_AsyncPool;

		// Loading stages
		//
		if (stage == 0)
		{
		}
		else if (stage == 5)
		{
			Profile p("m_pGltfLoader->Load");

			m_pGLTFTexturesAndBuffers = new GLTFTexturesAndBuffers();
			m_pGLTFTexturesAndBuffers->OnCreate(m_pDevice, pGLTFCommon, &m_UploadHeap, &m_VidMemBufferPool, &m_ConstantBufferRing);
		}
		else if (stage == 6)
		{
			Profile p("LoadTextures");

			// here we are loading onto the GPU all the textures and the inverse matrices
			// this data will be used to create the PBR and Depth passes       
			m_pGLTFTexturesAndBuffers->LoadTextures(pAsyncPool);
		}
		else if (stage == 7)
		{
			Profile p("m_gltfDepth->OnCreate");

			//create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
			m_gltfDepth = new GltfDepthPass();
			m_gltfDepth->OnCreate(
				m_pDevice,
				m_RenderPassShadow,
				&m_UploadHeap,
				&m_ResourceViewHeaps,
				&m_ConstantBufferRing,
				&m_VidMemBufferPool,
				m_pGLTFTexturesAndBuffers,
				pAsyncPool
			);
		}
		else if (stage == 8)
		{
			Profile p("m_gltfMotionVectors->OnCreate");

			m_gltfMotionVectors = new GltfMotionVectorsPass();
			m_gltfMotionVectors->OnCreate(
				m_pDevice,
				m_RenderPassMV,
				&m_UploadHeap,
				&m_ResourceViewHeaps,
				&m_ConstantBufferRing,
				&m_VidMemBufferPool,
				m_pGLTFTexturesAndBuffers,
				m_MotionVectors.GetFormat(),
				m_NormalBuffer.GetFormat(),
				pAsyncPool
			);
		}
		else if (stage == 9)
		{
			Profile p("m_gltfPBR->OnCreate");

			// same thing as above but for the PBR pass
			m_gltfPBR = new GltfPbrPass();
			m_gltfPBR->OnCreate(
				m_pDevice,
				m_RenderPassPBR,
				&m_UploadHeap,
				&m_ResourceViewHeaps,
				&m_ConstantBufferRing,
				&m_VidMemBufferPool,
				m_pGLTFTexturesAndBuffers,
				&m_AmbientLight,
				false,
				m_ShadowMapSRV,
				true, true, false, false,
				VK_SAMPLE_COUNT_1_BIT,
				pAsyncPool
			);


#if (USE_VID_MEM==true)
			// we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
			m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
			m_UploadHeap.FlushAndFinish();
#endif    
		}
		else if (stage == 10)
		{
			Profile p("m_gltfBBox->OnCreate");

			// just a bounding box pass that will draw boundingboxes instead of the geometry itself
			m_gltfBBox = new GltfBBoxPass();
			m_gltfBBox->OnCreate(
				m_pDevice,
				m_RenderPassHDR,
				&m_ResourceViewHeaps,
				&m_ConstantBufferRing,
				&m_VidMemBufferPool,
				m_pGLTFTexturesAndBuffers,
				&m_Wireframe
			);
#if (USE_VID_MEM==true)
			// we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
			m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
			m_UploadHeap.FlushAndFinish();
#endif    
		}
		else if (stage == 11)
		{
			Profile p("Flush");

			m_UploadHeap.FlushAndFinish();

#if (USE_VID_MEM==true)
			//once everything is uploaded we dont need he upload heaps anymore
			m_VidMemBufferPool.FreeUploadHeap();
#endif    

			// tell caller that we are done loading the map
			return 0;
		}

		stage++;
		return stage;
	}

	//--------------------------------------------------------------------------------------
	//
	// UnloadScene
	//
	//--------------------------------------------------------------------------------------
	void SampleRenderer::UnloadScene()
	{
		if (m_gltfPBR)
		{
			m_gltfPBR->OnDestroy();
			delete m_gltfPBR;
			m_gltfPBR = NULL;
		}

		if (m_gltfMotionVectors)
		{
			m_gltfMotionVectors->OnDestroy();
			delete m_gltfMotionVectors;
			m_gltfMotionVectors = NULL;
		}

		if (m_gltfDepth)
		{
			m_gltfDepth->OnDestroy();
			delete m_gltfDepth;
			m_gltfDepth = NULL;
		}

		if (m_gltfBBox)
		{
			m_gltfBBox->OnDestroy();
			delete m_gltfBBox;
			m_gltfBBox = NULL;
		}

		if (m_pGLTFTexturesAndBuffers)
		{
			m_pGLTFTexturesAndBuffers->OnDestroy();
			delete m_pGLTFTexturesAndBuffers;
			m_pGLTFTexturesAndBuffers = NULL;
		}
	}

	void SampleRenderer::CreateApplyReflectionsPipeline()
	{
		VkDevice device = m_pDevice->GetDevice();

		VkDescriptorSetLayoutBinding bindings[6];
		bindings[0].binding = 0;
		bindings[0].descriptorCount = 1;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		bindings[1].binding = 1;
		bindings[1].descriptorCount = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		bindings[1].pImmutableSamplers = nullptr;

		bindings[2].binding = 2;
		bindings[2].descriptorCount = 1;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		bindings[2].pImmutableSamplers = nullptr;

		bindings[3].binding = 3;
		bindings[3].descriptorCount = 1;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		bindings[3].pImmutableSamplers = nullptr;

		bindings[4].binding = 4;
		bindings[4].descriptorCount = 1;
		bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		bindings[4].pImmutableSamplers = nullptr;

		bindings[5].binding = 5;
		bindings[5].descriptorCount = 1;
		bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		bindings[5].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo descSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		descSetLayoutCreateInfo.pNext = nullptr;
		descSetLayoutCreateInfo.bindingCount = _countof(bindings);
		descSetLayoutCreateInfo.pBindings = bindings;
		descSetLayoutCreateInfo.flags = 0;

		if (VK_SUCCESS != vkCreateDescriptorSetLayout(device, &descSetLayoutCreateInfo, nullptr, &m_ApplyPipelineDescriptorSetLayout))
		{
			Trace("Failed to create set layout for apply reflections pipeline.");
		}

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutCreateInfo.flags = 0;
		pipelineLayoutCreateInfo.pNext = nullptr;
		pipelineLayoutCreateInfo.setLayoutCount = 1;
		pipelineLayoutCreateInfo.pSetLayouts = &m_ApplyPipelineDescriptorSetLayout;
		pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
		pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

		if (VK_SUCCESS != vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &m_ApplyPipelineLayout))
		{
			Trace("Failed to create pipeline layout for apply reflections pipeline.");
		}

		DefineList defines;
		VkPipelineShaderStageCreateInfo vs, fs;
		VKCompileFromFile(device, VK_SHADER_STAGE_VERTEX_BIT, "ApplyReflections.hlsl", "vs_main", "-T vs_6_0", &defines, &vs);
		VKCompileFromFile(device, VK_SHADER_STAGE_FRAGMENT_BIT, "ApplyReflections.hlsl", "ps_main", "-T ps_6_0", &defines, &fs);

		VkPipelineVertexInputStateCreateInfo vertexInputStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInputStateInfo.pNext = nullptr;
		vertexInputStateInfo.flags = 0;
		vertexInputStateInfo.vertexBindingDescriptionCount = 0;
		vertexInputStateInfo.pVertexBindingDescriptions = nullptr;
		vertexInputStateInfo.vertexAttributeDescriptionCount = 0;
		vertexInputStateInfo.pVertexAttributeDescriptions = nullptr;

		VkPipelineColorBlendAttachmentState pipelineColorBlendAttachmentState = {};
		pipelineColorBlendAttachmentState.blendEnable = VK_TRUE;
		pipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		pipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		pipelineColorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		pipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		pipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		pipelineColorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		pipelineColorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlendStateCreateInfo.pNext = nullptr;
		colorBlendStateCreateInfo.flags = 0;
		colorBlendStateCreateInfo.logicOpEnable = false;
		colorBlendStateCreateInfo.attachmentCount = 1;
		colorBlendStateCreateInfo.pAttachments = &pipelineColorBlendAttachmentState;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo pipelineDynamicStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		pipelineDynamicStateInfo.pNext = nullptr;
		pipelineDynamicStateInfo.flags = 0;
		pipelineDynamicStateInfo.dynamicStateCount = _countof(dynamicStates);
		pipelineDynamicStateInfo.pDynamicStates = dynamicStates;

		VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		pipelineRasterizationStateCreateInfo.pNext = nullptr;
		pipelineRasterizationStateCreateInfo.flags = 0;
		pipelineRasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
		pipelineRasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
		pipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
		pipelineRasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
		pipelineRasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
		pipelineRasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		pipelineRasterizationStateCreateInfo.depthBiasConstantFactor = 0;
		pipelineRasterizationStateCreateInfo.depthBiasClamp = 0;
		pipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = 0;
		pipelineRasterizationStateCreateInfo.lineWidth = 0;

		VkPipelineMultisampleStateCreateInfo multisampleStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisampleStateInfo.pNext = nullptr;
		multisampleStateInfo.flags = 0;
		multisampleStateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampleStateInfo.sampleShadingEnable = VK_FALSE;
		multisampleStateInfo.minSampleShading = 0;
		multisampleStateInfo.pSampleMask = nullptr;
		multisampleStateInfo.alphaToCoverageEnable = VK_FALSE;
		multisampleStateInfo.alphaToOneEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewportStateInfo = {};
		viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateInfo.pNext = nullptr;
		viewportStateInfo.flags = 0;
		viewportStateInfo.viewportCount = 1;
		viewportStateInfo.scissorCount = 1;
		viewportStateInfo.pScissors = nullptr;
		viewportStateInfo.pViewports = nullptr;

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssemblyState.pNext = nullptr;
		inputAssemblyState.flags = 0;
		inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssemblyState.primitiveRestartEnable = VK_FALSE;

		VkAttachmentDescription colorAttachments[1];
		AttachBlending(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &colorAttachments[0]);
		m_RenderPassApply = CreateRenderPassOptimal(m_pDevice->GetDevice(), _countof(colorAttachments), colorAttachments, nullptr);

		VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineCreateInfo.pNext = nullptr;
		pipelineCreateInfo.flags = 0;
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineCreateInfo.basePipelineIndex = 0;
		pipelineCreateInfo.layout = m_ApplyPipelineLayout;
		pipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
		pipelineCreateInfo.pDepthStencilState = nullptr;
		pipelineCreateInfo.pDynamicState = &pipelineDynamicStateInfo;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pMultisampleState = &multisampleStateInfo;
		pipelineCreateInfo.pRasterizationState = &pipelineRasterizationStateCreateInfo;
		pipelineCreateInfo.stageCount = _countof(stages);
		pipelineCreateInfo.pStages = stages;
		pipelineCreateInfo.pTessellationState = nullptr;
		pipelineCreateInfo.pVertexInputState = &vertexInputStateInfo;
		pipelineCreateInfo.pViewportState = &viewportStateInfo;
		pipelineCreateInfo.renderPass = m_RenderPassApply;
		pipelineCreateInfo.subpass = 0;

		if (VK_SUCCESS != vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_ApplyPipeline))
		{
			Trace("Failed to create pipeline for the apply reflection target pass.");
		}

		for (int i = 0; i < backBufferCount; ++i)
		{
			m_ResourceViewHeaps.AllocDescriptor(m_ApplyPipelineDescriptorSetLayout, &m_ApplyPipelineDescriptorSet[i]);
		}
	}

	void SampleRenderer::CreateDepthDownsamplePipeline()
	{
		VkDevice device = m_pDevice->GetDevice();

		VkDescriptorSetLayoutBinding bindings[3];
		bindings[0].binding = 0;
		bindings[0].descriptorCount = 1;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		bindings[1].binding = 1;
		bindings[1].descriptorCount = 13;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].pImmutableSamplers = nullptr;

		bindings[2].binding = 2;
		bindings[2].descriptorCount = 1;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[2].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo descSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		descSetLayoutCreateInfo.pNext = nullptr;
		descSetLayoutCreateInfo.bindingCount = _countof(bindings);
		descSetLayoutCreateInfo.pBindings = bindings;
		descSetLayoutCreateInfo.flags = 0;

		if (VK_SUCCESS != vkCreateDescriptorSetLayout(device, &descSetLayoutCreateInfo, nullptr, &m_DepthDownsampleDescriptorSetLayout))
		{
			Trace("Failed to create descriptor set layout for depth downsampling pipeline.");
		}

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutCreateInfo.flags = 0;
		pipelineLayoutCreateInfo.pNext = nullptr;
		pipelineLayoutCreateInfo.setLayoutCount = 1;
		pipelineLayoutCreateInfo.pSetLayouts = &m_DepthDownsampleDescriptorSetLayout;
		pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
		pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

		if (VK_SUCCESS != vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &m_DepthDownsamplePipelineLayout))
		{
			Trace("Failed to create pipeline layout for depth downsampling pipeline.");
		}

		DefineList defines;
		VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo;
		VKCompileFromFile(device, VK_SHADER_STAGE_COMPUTE_BIT, "DepthDownsample.hlsl", "main", "-T cs_6_0", &defines, &pipelineShaderStageCreateInfo);

		VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		pipelineCreateInfo.pNext = nullptr;
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineCreateInfo.basePipelineIndex = 0;
		pipelineCreateInfo.flags = 0;
		pipelineCreateInfo.layout = m_DepthDownsamplePipelineLayout;
		pipelineCreateInfo.stage = pipelineShaderStageCreateInfo;

		if (VK_SUCCESS != vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_DepthDownsamplePipeline))
		{
			Trace("Failed to create pipeline for depth downsampling pipeline.");
		}

		m_ResourceViewHeaps.AllocDescriptor(m_DepthDownsampleDescriptorSetLayout, &m_DepthDownsampleDescriptorSet);
	}

	void SampleRenderer::StallFrame(float targetFrametime)
	{
		// Simulate lower frame rates
		static std::chrono::system_clock::time_point last = std::chrono::system_clock::now();
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		std::chrono::duration<double> diff = now - last;
		last = now;
		float deltaTime = 1000 * static_cast<float>(diff.count());
		if (deltaTime < targetFrametime)
		{
			int deltaCount = static_cast<int>(targetFrametime - deltaTime);
			std::this_thread::sleep_for(std::chrono::milliseconds(deltaCount));
		}
	}

	void SampleRenderer::BeginFrame(VkCommandBuffer cb)
	{
		m_CurrentBackbufferIndex = (m_CurrentBackbufferIndex + 1) % backBufferCount;

		// Timing values
		double nanosecondsBetweenGPUTicks = m_pDevice->GetPhysicalDeviceProperries().limits.timestampPeriod;
		m_MillisecondsBetweenGpuTicks = 1e-6 * nanosecondsBetweenGPUTicks;

		// Let our resource managers do some house keeping 
		m_ConstantBufferRing.OnBeginFrame();
		m_GPUTimer.OnBeginFrame(cb, &m_TimeStamps);
	}

	VkBufferMemoryBarrier SampleRenderer::BufferBarrier(VkBuffer buffer)
	{
		VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = buffer;
		barrier.offset = 0;
		barrier.size = VK_WHOLE_SIZE;
		return barrier;
	}

	VkImageMemoryBarrier SampleRenderer::Transition(VkImage image, VkImageLayout before, VkImageLayout after, VkImageAspectFlags aspectMask, int mipCount)
	{
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
		barrier.oldLayout = before;
		barrier.newLayout = after;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = aspectMask; // VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipCount;

		barrier.subresourceRange = subresourceRange;
		return barrier;
	}

	void SampleRenderer::Barriers(VkCommandBuffer cb, const std::vector<VkImageMemoryBarrier>& imageBarriers)
	{
		vkCmdPipelineBarrier(cb,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
	}

	VkCommandBuffer SampleRenderer::BeginNewCommandBuffer()
	{
		VkCommandBuffer cb = m_CommandListRing.GetNewCommandList();
		VkCommandBufferBeginInfo commandBufferBeginInfo = {};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBufferBeginInfo.pNext = NULL;
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		commandBufferBeginInfo.pInheritanceInfo = NULL;
		VkResult res = vkBeginCommandBuffer(cb, &commandBufferBeginInfo);
		assert(res == VK_SUCCESS);
		return cb;
	}

	void SampleRenderer::SubmitCommandBuffer(VkCommandBuffer cb, VkSemaphore* waitSemaphore, VkSemaphore* signalSemaphores, VkFence fence)
	{
		VkResult res = vkEndCommandBuffer(cb);
		assert(res == VK_SUCCESS);

		VkPipelineStageFlags submitWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.pNext = NULL;
		submitInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
		submitInfo.pWaitSemaphores = waitSemaphore;
		submitInfo.pWaitDstStageMask = waitSemaphore ? &submitWaitStage : NULL;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cb;
		submitInfo.signalSemaphoreCount = signalSemaphores ? 1 : 0;
		submitInfo.pSignalSemaphores = signalSemaphores;
		res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submitInfo, fence);
		assert(res == VK_SUCCESS);
	}

	per_frame* SampleRenderer::FillFrameConstants(State* pState)
	{
		// Sets the perFrame data (Camera and lights data), override as necessary and set them as constant buffers --------------
		//
		per_frame* pPerFrame = NULL;
		if (m_pGLTFTexturesAndBuffers)
		{
			pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(pState->camera);

			//override gltf camera with ours
			pPerFrame->mCameraViewProj = pState->camera.GetView() * pState->camera.GetProjection();
			pPerFrame->cameraPos = pState->camera.GetPosition();
			pPerFrame->emmisiveFactor = pState->emmisiveFactor;
			pPerFrame->iblFactor = pState->iblFactor;

			//if the gltf doesn't have any lights set a directional light
			if (pPerFrame->lightCount == 0)
			{
				pPerFrame->lightCount = 1;
				pPerFrame->lights[0].color[0] = pState->lightColor.x;
				pPerFrame->lights[0].color[1] = pState->lightColor.y;
				pPerFrame->lights[0].color[2] = pState->lightColor.z;
				GetXYZ(pPerFrame->lights[0].position, pState->lightCamera.GetPosition());
				GetXYZ(pPerFrame->lights[0].direction, pState->lightCamera.GetDirection());

				pPerFrame->lights[0].range = 30.0f; // in meters
				pPerFrame->lights[0].type = LightType_Spot;
				pPerFrame->lights[0].intensity = pState->lightIntensity;
				pPerFrame->lights[0].innerConeCos = cosf(pState->lightCamera.GetFovV() * 0.9f / 2.0f);
				pPerFrame->lights[0].outerConeCos = cosf(pState->lightCamera.GetFovV() / 2.0f);
				pPerFrame->lights[0].mLightViewProj = pState->lightCamera.GetView() * pState->lightCamera.GetProjection();
			}

			// Up to 4 spotlights can have shadowmaps. Each spot the light has a shadowMap index which is used to find the shadowmap in the atlas
			uint32_t shadowMapIndex = 0;
			for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
			{
				if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Spot))
				{
					pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index so the color pass knows which shadow map to use
					pPerFrame->lights[i].depthBias = 20.0f / 100000.0f;
				}
				else if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Directional))
				{
					pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // same as above
					pPerFrame->lights[i].depthBias = 100.0f / 100000.0f;
				}
				else
				{
					pPerFrame->lights[i].shadowMapIndex = -1;   // no shadow for this light
				}
			}

			m_pGLTFTexturesAndBuffers->SetPerFrameConstants();

			m_pGLTFTexturesAndBuffers->SetSkinningMatricesForSkeletons();
		}

		return pPerFrame;
	}

	void SampleRenderer::RenderSpotLights(VkCommandBuffer cb, per_frame* pPerFrame)
	{
		VkClearValue clearValue = {};
		clearValue.depthStencil.depth = 1;
		clearValue.depthStencil.stencil = 0;

		VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.pNext = nullptr;
		beginInfo.clearValueCount = 1;
		beginInfo.pClearValues = &clearValue;
		beginInfo.renderArea = { 0, 0, m_ShadowMap.GetWidth(), m_ShadowMap.GetHeight() };
		beginInfo.renderPass = m_RenderPassShadow;
		beginInfo.framebuffer = m_FramebufferShadows;
		vkCmdBeginRenderPass(cb, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
		{
			if (!(pPerFrame->lights[i].type == LightType_Spot || pPerFrame->lights[i].type == LightType_Directional))
				continue;

			// Set the RT's quadrant where to render the shadomap (these viewport offsets need to match the ones in shadowFiltering.h)
			uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
			uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
			uint32_t viewportWidth = m_ShadowMap.GetWidth() / 2;
			uint32_t viewportHeight = m_ShadowMap.GetHeight() / 2;
			SetViewportAndScissor(cb, viewportOffsetsX[i] * viewportWidth, viewportOffsetsY[i] * viewportHeight, viewportWidth, viewportHeight);

			GltfDepthPass::per_frame* cbDepthPerFrame = m_gltfDepth->SetPerFrameConstants();
			cbDepthPerFrame->mViewProj = pPerFrame->lights[i].mLightViewProj;

			m_gltfDepth->Draw(cb);

			m_GPUTimer.GetTimeStamp(cb, "Shadow map");
		}

		vkCmdEndRenderPass(cb);
	}

	void SampleRenderer::RenderMotionVectors(VkCommandBuffer cb, per_frame* pPerFrame, State* pState)
	{
		vkCmdSetViewport(cb, 0, 1, &m_Viewport);
		vkCmdSetScissor(cb, 0, 1, &m_Scissor);

		GltfMotionVectorsPass::per_frame* cbDepthPerFrame = m_gltfMotionVectors->SetPerFrameConstants();
		cbDepthPerFrame->mCurrViewProj = pPerFrame->mCameraViewProj;
		cbDepthPerFrame->mPrevViewProj = pState->camera.GetPrevView() * pState->camera.GetProjection();

		m_gltfMotionVectors->Draw(cb);
		m_GPUTimer.GetTimeStamp(cb, "Motion vectors");
	}


	void SampleRenderer::RenderSkydome(VkCommandBuffer cb, per_frame* pPerFrame, State* pState)
	{
		VkClearValue clearValues[1];
		clearValues[0].color.float32[0] = 0;
		clearValues[0].color.float32[1] = 0;
		clearValues[0].color.float32[2] = 0;
		clearValues[0].color.float32[3] = 0;

		VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.pNext = nullptr;
		beginInfo.clearValueCount = _countof(clearValues);
		beginInfo.pClearValues = clearValues;
		beginInfo.renderArea = { 0, 0, m_Width, m_Height };
		beginInfo.renderPass = m_RenderPassClearHDR;
		beginInfo.framebuffer = m_FramebufferHDR;
		vkCmdBeginRenderPass(cb, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdSetViewport(cb, 0, 1, &m_Viewport);
		vkCmdSetScissor(cb, 0, 1, &m_Scissor);

		if (pState->skyDomeType == 1)
		{
			XMMATRIX clipToView = XMMatrixInverse(NULL, pPerFrame->mCameraViewProj);
			m_SkyDome.Draw(cb, clipToView);
			m_GPUTimer.GetTimeStamp(cb, "Skydome");
		}
		else if (pState->skyDomeType == 0)
		{
			SkyDomeProc::Constants skyDomeConstants;
			skyDomeConstants.invViewProj = XMMatrixInverse(NULL, pPerFrame->mCameraViewProj);
			skyDomeConstants.vSunDirection = XMVectorSet(1.0f, 0.05f, 0.0f, 0.0f);
			skyDomeConstants.turbidity = 10.0f;
			skyDomeConstants.rayleigh = 2.0f;
			skyDomeConstants.mieCoefficient = 0.005f;
			skyDomeConstants.mieDirectionalG = 0.8f;
			skyDomeConstants.luminance = 1.0f;
			skyDomeConstants.sun = false;
			m_SkyDomeProc.Draw(cb, skyDomeConstants);
			m_GPUTimer.GetTimeStamp(cb, "Skydome proc");
		}

		vkCmdEndRenderPass(cb);
	}

	void SampleRenderer::RenderScene(VkCommandBuffer cb)
	{
		VkClearValue clearValues[2];
		clearValues[0].color.float32[0] = 0;
		clearValues[0].color.float32[1] = 0;
		clearValues[0].color.float32[2] = 0;
		clearValues[0].color.float32[3] = 0;
		clearValues[1].color.float32[0] = 1;
		clearValues[1].color.float32[1] = 1;
		clearValues[1].color.float32[2] = 1;
		clearValues[1].color.float32[3] = 1;

		VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.pNext = nullptr;
		beginInfo.clearValueCount = _countof(clearValues);
		beginInfo.pClearValues = clearValues;
		beginInfo.renderArea = { 0, 0, m_Width, m_Height };
		beginInfo.renderPass = m_RenderPassPBR;
		beginInfo.framebuffer = m_FramebufferPBR;
		vkCmdBeginRenderPass(cb, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		//set per frame constant buffer values
		m_gltfPBR->Draw(cb);

		vkCmdEndRenderPass(cb);
	}

	void SampleRenderer::RenderBoundingBoxes(VkCommandBuffer cb, per_frame* pPerFrame)
	{
		m_gltfBBox->Draw(cb, pPerFrame->mCameraViewProj);
		m_GPUTimer.GetTimeStamp(cb, "Bounding Box");
	}


	void SampleRenderer::RenderLightFrustums(VkCommandBuffer cb, per_frame* pPerFrame, State* pState)
	{
		SetPerfMarkerBegin(cb, "Light frustrums");

		XMVECTOR vCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR vRadius = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
		XMVECTOR vColor = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
		for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
		{
			XMMATRIX spotlightMatrix = XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
			XMMATRIX worldMatrix = spotlightMatrix * pPerFrame->mCameraViewProj;
			m_WireframeBox.Draw(cb, &m_Wireframe, worldMatrix, vCenter, vRadius, vColor);
		}

		m_GPUTimer.GetTimeStamp(cb, "Light frustums");
		SetPerfMarkerEnd(cb);
	}


	void SampleRenderer::DownsampleDepthBuffer(VkCommandBuffer cb)
	{
		// Clear m_AtomicCounter to 0
		vkCmdFillBuffer(cb, m_AtomicCounter, 0, VK_WHOLE_SIZE, 0);

		SetPerfMarkerBegin(cb, "Downsample Depth");

		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_DepthDownsamplePipeline);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_DepthDownsamplePipelineLayout, 0, 1, &m_DepthDownsampleDescriptorSet, 0, nullptr);

		// Each threadgroup works on 64x64 texels
		uint32_t dimX = (m_Width + 63) / 64;
		uint32_t dimY = (m_Height + 63) / 64;
		vkCmdDispatch(cb, dimX, dimY, 1);

		m_GPUTimer.GetTimeStamp(cb, "Downsample Depth");
		SetPerfMarkerEnd(cb);
	}


	void SampleRenderer::RenderScreenSpaceReflections(VkCommandBuffer cb, per_frame* pPerFrame, State* pState)
	{
		Barriers(cb, {
			Transition(m_Sssr.GetOutputTexture()->Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT),
			Transition(m_DepthHierarchy.Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, m_DepthMipLevelCount),
			});

		SSSRConstants sssrConstants = {};
		const Camera* camera = &pState->camera;
		XMMATRIX view = camera->GetView();
		XMMATRIX proj = camera->GetProjection();

		XMStoreFloat4x4(&sssrConstants.view, XMMatrixTranspose(view));
		XMStoreFloat4x4(&sssrConstants.projection, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&sssrConstants.invProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, proj)));
		XMStoreFloat4x4(&sssrConstants.invView, XMMatrixTranspose(XMMatrixInverse(nullptr, view)));
		XMStoreFloat4x4(&sssrConstants.invViewProjection, XMMatrixTranspose(pPerFrame->mInverseCameraViewProj));
		XMStoreFloat4x4(&sssrConstants.prevViewProjection, XMMatrixTranspose(m_prev_view_projection));

		sssrConstants.frameIndex = m_CurrentFrameIndex;
		sssrConstants.maxTraversalIntersections = pState->maxTraversalIterations;
		sssrConstants.minTraversalOccupancy = pState->minTraversalOccupancy;
		sssrConstants.mostDetailedMip = pState->mostDetailedDepthHierarchyMipLevel;
		sssrConstants.temporalStabilityFactor = pState->temporalStability;
		sssrConstants.temporalVarianceThreshold = pState->temporalVarianceThreshold;
		sssrConstants.depthBufferThickness = pState->depthBufferThickness;
		sssrConstants.samplesPerQuad = pState->samplesPerQuad;
		sssrConstants.temporalVarianceGuidedTracingEnabled = pState->bEnableVarianceGuidedTracing ? 1 : 0;
		sssrConstants.roughnessThreshold = pState->roughnessThreshold;

		m_Sssr.Draw(cb, sssrConstants, pState->bShowIntersectionResults);
		m_GPUTimer.GetTimeStamp(cb, "FidelityFX SSSR");

		Barriers(cb, {
			Transition(m_Sssr.GetOutputTexture()->Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			Transition(m_DepthHierarchy.Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, m_DepthMipLevelCount),
			});


		//Extract SSSR Timestamps and calculate averages
		uint64_t tileClassificationTime = m_Sssr.GetTileClassificationElapsedGpuTicks();
		static std::deque<float> tileClassificationTimes(100);
		tileClassificationTimes.pop_front();
		tileClassificationTimes.push_back(static_cast<float>(tileClassificationTime * m_MillisecondsBetweenGpuTicks));
		pState->tileClassificationTime = 0;
		for (auto& time : tileClassificationTimes)
		{
			pState->tileClassificationTime += time;
		}
		pState->tileClassificationTime /= tileClassificationTimes.size();

		uint64_t intersectionTime = m_Sssr.GetIntersectElapsedGpuTicks();
		static std::deque<float> intersectionTimes(100);
		intersectionTimes.pop_front();
		intersectionTimes.push_back(static_cast<float>(intersectionTime * m_MillisecondsBetweenGpuTicks));
		pState->intersectionTime = 0;
		for (auto& time : intersectionTimes)
		{
			pState->intersectionTime += time;
		}
		pState->intersectionTime /= intersectionTimes.size();

		uint64_t denoisingTime = m_Sssr.GetDenoiserElapsedGpuTicks();
		static std::deque<float> denoisingTimes(100);
		denoisingTimes.pop_front();
		denoisingTimes.push_back(static_cast<float>(denoisingTime * m_MillisecondsBetweenGpuTicks));
		pState->denoisingTime = 0;
		for (auto& time : denoisingTimes)
		{
			pState->denoisingTime += time;
		}
		pState->denoisingTime /= denoisingTimes.size();
	}

	void SampleRenderer::CopyHistorySurfaces(VkCommandBuffer cb)
	{
		Barriers(cb, {
			Transition(m_NormalBuffer.Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			Transition(m_NormalHistoryBuffer.Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			});

		SetPerfMarkerBegin(cb, "Copy History Normals and Roughness");
		// Keep copy of normal roughness buffer for next frame
		CopyToTexture(cb, &m_NormalBuffer, &m_NormalHistoryBuffer);
		SetPerfMarkerEnd(cb);

		Barriers(cb, {
			Transition(m_NormalBuffer.Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			Transition(m_NormalHistoryBuffer.Resource(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			});
	}

	void SampleRenderer::ApplyReflectionTarget(VkCommandBuffer cb, State* pState)
	{
		VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.pNext = nullptr;
		beginInfo.clearValueCount = 0;
		beginInfo.pClearValues = nullptr;
		beginInfo.renderArea = { 0, 0, m_Width, m_Height };
		beginInfo.renderPass = m_RenderPassApply;
		beginInfo.framebuffer = m_FramebufferApply;
		vkCmdBeginRenderPass(cb, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		SetPerfMarkerBegin(cb, "Apply Reflection View");

		struct PassConstants
		{
			XMFLOAT4 viewDir;
			UINT showReflectionTarget;
			UINT drawReflections;
		} constants;

		XMVECTOR view = pState->camera.GetDirection();
		XMStoreFloat4(&constants.viewDir, view);
		constants.showReflectionTarget = pState->showReflectionTarget ? 1 : 0;
		constants.drawReflections = pState->bDrawScreenSpaceReflections ? 1 : 0;

		VkDescriptorBufferInfo uniformBufferInfo = m_ConstantBufferRing.AllocConstantBuffer(sizeof(PassConstants), &constants);
		VkWriteDescriptorSet uniformBufferWriteDescSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		uniformBufferWriteDescSet.pNext = nullptr;
		uniformBufferWriteDescSet.descriptorCount = 1;
		uniformBufferWriteDescSet.dstArrayElement = 0;
		uniformBufferWriteDescSet.dstSet = m_ApplyPipelineDescriptorSet[m_CurrentBackbufferIndex];
		uniformBufferWriteDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uniformBufferWriteDescSet.dstBinding = 5;
		uniformBufferWriteDescSet.pBufferInfo = &uniformBufferInfo;

		vkUpdateDescriptorSets(m_pDevice->GetDevice(), 1, &uniformBufferWriteDescSet, 0, nullptr);

		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ApplyPipeline);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ApplyPipelineLayout, 0, 1, &m_ApplyPipelineDescriptorSet[m_CurrentBackbufferIndex], 0, nullptr);
		vkCmdSetViewport(cb, 0, 1, &m_Viewport);
		vkCmdSetScissor(cb, 0, 1, &m_Scissor);

		vkCmdDraw(cb, 3, 1, 0, 0);

		m_GPUTimer.GetTimeStamp(cb, "Apply Reflection View");
		SetPerfMarkerEnd(cb);

		vkCmdEndRenderPass(cb);
	}

	void SampleRenderer::DownsampleScene(VkCommandBuffer cb)
	{
		m_DownSample.Draw(cb);
		m_GPUTimer.GetTimeStamp(cb, "Downsample");
	}

	void SampleRenderer::RenderBloom(VkCommandBuffer cb)
	{
		m_Bloom.Draw(cb);
		m_GPUTimer.GetTimeStamp(cb, "Bloom");
	}

	void SampleRenderer::ApplyTonemapping(VkCommandBuffer cb, State* pState, SwapChain* pSwapChain)
	{
		vkCmdSetViewport(cb, 0, 1, &m_Viewport);
		vkCmdSetScissor(cb, 0, 1, &m_Scissor);

		m_ToneMapping.Draw(cb, m_HDRSRV, pState->exposure, pState->toneMapper);
		m_GPUTimer.GetTimeStamp(cb, "Tone mapping");
	}

	void SampleRenderer::RenderHUD(VkCommandBuffer cb, SwapChain* pSwapChain)
	{
		vkCmdSetViewport(cb, 0, 1, &m_Viewport);
		vkCmdSetScissor(cb, 0, 1, &m_Scissor);

		m_ImGUI.Draw(cb);

		m_GPUTimer.GetTimeStamp(cb, "ImGUI rendering");
	}

	void SampleRenderer::CopyToTexture(VkCommandBuffer cb, Texture* source, Texture* target)
	{
		VkImageCopy region = {};
		region.dstOffset = { 0, 0, 0 };
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.mipLevel = 0;
		region.extent = { m_Width, m_Height, 1 };
		region.srcOffset = { 0, 0, 0 };
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.mipLevel = 0;
		vkCmdCopyImage(cb, source->Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->Resource(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}

	//--------------------------------------------------------------------------------------
	//
	// OnRender
	//
	//--------------------------------------------------------------------------------------
	void SampleRenderer::OnRender(State* pState, SwapChain* pSwapChain)
	{
		StallFrame(pState->targetFrametime);

		VkCommandBuffer cb1 = BeginNewCommandBuffer();
		BeginFrame(cb1);

		per_frame* pPerFrame = FillFrameConstants(pState);

		// Clears happen in the render passes -----------------------------------------------------------------------

		// Render to shadow map atlas for spot lights ------------------------------------------
		//
		if (m_gltfDepth && pPerFrame)
		{
			RenderSpotLights(cb1, pPerFrame);
		}

		VkClearValue clearValues[3];
		clearValues[0].color.float32[0] = 0;
		clearValues[0].color.float32[1] = 0;
		clearValues[0].color.float32[2] = 0;
		clearValues[0].color.float32[3] = 0;
		clearValues[1].color.float32[0] = 0;
		clearValues[1].color.float32[1] = 0;
		clearValues[1].color.float32[2] = 0;
		clearValues[1].color.float32[3] = 0;
		clearValues[2].depthStencil.depth = 1;
		clearValues[2].depthStencil.stencil = 0;

		VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.pNext = nullptr;
		beginInfo.clearValueCount = _countof(clearValues);
		beginInfo.pClearValues = clearValues;
		beginInfo.renderArea = { 0, 0, m_Width, m_Height };
		beginInfo.renderPass = m_RenderPassMV;
		beginInfo.framebuffer = m_FramebufferMV;
		vkCmdBeginRenderPass(cb1, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Motion vectors ---------------------------------------------------------------------------
		//
		if (m_gltfMotionVectors && pPerFrame)
		{
			RenderMotionVectors(cb1, pPerFrame, pState);
		}

		vkCmdEndRenderPass(cb1);

		// Render Scene to the HDR RT ------------------------------------------------
		//

		if (pPerFrame)
		{
			RenderSkydome(cb1, pPerFrame, pState);

			// Render scene to color buffer
			if (m_gltfPBR)
			{
				RenderScene(cb1);
			}

			beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
			beginInfo.pNext = nullptr;
			beginInfo.clearValueCount = 0;
			beginInfo.pClearValues = nullptr;
			beginInfo.renderArea = { 0, 0, m_Width, m_Height };
			beginInfo.renderPass = m_RenderPassHDR;
			beginInfo.framebuffer = m_FramebufferHDR;
			vkCmdBeginRenderPass(cb1, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

			// Draw object bounding boxes
			if (m_gltfBBox && pState->bDrawBoundingBoxes)
			{
				RenderBoundingBoxes(cb1, pPerFrame);
			}

			// Draw light frustum
			if (pState->bDrawLightFrustum)
			{
				RenderLightFrustums(cb1, pPerFrame, pState);
			}

			vkCmdEndRenderPass(cb1);

			m_GPUTimer.GetTimeStamp(cb1, "Rendering scene");
		}

		// Downsample depth buffer
		if (m_gltfMotionVectors && pPerFrame)
		{
			DownsampleDepthBuffer(cb1);
		}

		if (m_gltfPBR && pPerFrame)
		{
			// Stochastic SSR
			RenderScreenSpaceReflections(cb1, pPerFrame, pState);

			// Keep this frames results for next frame
			CopyHistorySurfaces(cb1);

			// Apply the result of SSR
			ApplyReflectionTarget(cb1, pState);
		}

		if (pPerFrame && pState->bDrawBloom)
		{
			DownsampleScene(cb1);
			RenderBloom(cb1);
		}

		SubmitCommandBuffer(cb1);

		// Wait for swapchain (we are going to render to it) -----------------------------------
		//
		int imageIndex = pSwapChain->WaitForSwapChain();
		m_CommandListRing.OnBeginFrame();

		VkCommandBuffer cb2 = BeginNewCommandBuffer();

		beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.pNext = nullptr;
		beginInfo.clearValueCount = 0;
		beginInfo.pClearValues = nullptr;
		beginInfo.renderArea = { 0, 0, m_Width, m_Height };
		beginInfo.renderPass = pSwapChain->GetRenderPass();
		beginInfo.framebuffer = pSwapChain->GetFramebuffer(imageIndex);
		vkCmdBeginRenderPass(cb2, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		if (pPerFrame)
		{
			// Tonemapping
			ApplyTonemapping(cb2, pState, pSwapChain);
		}

		// Render HUD
		RenderHUD(cb2, pSwapChain);

		m_GPUTimer.OnEndFrame();

		vkCmdEndRenderPass(cb2);

		VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
		VkSemaphore renderFinishedSemaphores = VK_NULL_HANDLE;
		VkFence cmdBufExecutedFences = VK_NULL_HANDLE;
		pSwapChain->GetSemaphores(&imageAvailableSemaphore, &renderFinishedSemaphores, &cmdBufExecutedFences);

		SubmitCommandBuffer(cb2, &imageAvailableSemaphore, &renderFinishedSemaphores, cmdBufExecutedFences);

		// Update previous camera matrices
		if (pPerFrame)
		{
			m_prev_view_projection = pPerFrame->mCameraViewProj;
		}
		pState->camera.UpdatePreviousMatrices();
		m_CurrentFrameIndex++;
	}
}