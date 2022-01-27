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

#include "Renderer.h"
#include "UI.h"

#undef min
#undef max

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void Renderer::OnCreate(Device* pDevice, SwapChain* pSwapChain, float FontSize)
{
	m_pDevice = pDevice;
	m_FrameIndex = 0;

	// Initialize helpers

	// Create all the heaps for the resources views
	const uint32_t cbvDescriptorCount = 2000;
	const uint32_t srvDescriptorCount = 8000;
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
	const uint32_t staticGeometryMemSize = (1 * 128) * 1024 * 1024;
	m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, true, "StaticGeom");

	// Create a 'static' pool for vertices and indices in system memory
	const uint32_t systemGeometryMemSize = 32 * 1024;
	m_SysMemBufferPool.OnCreate(pDevice, systemGeometryMemSize, false, "PostProcGeom");

	// initialize the GPU time stamps module
	m_GPUTimer.OnCreate(pDevice, backBufferCount);

	// Quick helper to upload resources, it has it's own commandList and uses suballocation.
	const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
	m_UploadHeap.OnCreate(pDevice, uploadHeapMemSize);    // initialize an upload heap (uses suballocation for faster results)


	// Create GBuffer and render passes
	//
	{
		m_GBuffer.OnCreate(
			pDevice,
			&m_ResourceViewHeaps,
			{
				{ GBUFFER_DEPTH, VK_FORMAT_D32_SFLOAT},
				{ GBUFFER_FORWARD, VK_FORMAT_R16G16B16A16_SFLOAT},
				{ GBUFFER_MOTION_VECTORS, VK_FORMAT_R16G16_SFLOAT},
				{ GBUFFER_NORMAL_BUFFER,			VK_FORMAT_A2B10G10R10_UNORM_PACK32},
				{ GBUFFER_SPECULAR_ROUGHNESS,	VK_FORMAT_R8G8B8A8_UNORM},
				{ GBUFFER_MOTION_VECTORS,		VK_FORMAT_R16G16_SFLOAT},
			},
			1
			);

		GBufferFlags fullGBuffer = GBUFFER_DEPTH | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS | GBUFFER_NORMAL_BUFFER | GBUFFER_SPECULAR_ROUGHNESS | GBUFFER_MOTION_VECTORS;
		bool bClear = true;
		m_RenderPassFullGBufferWithClear.OnCreate(&m_GBuffer, fullGBuffer, bClear, "m_RenderPassFullGBufferWithClear");
		m_RenderPassFullGBuffer.OnCreate(&m_GBuffer, fullGBuffer, !bClear, "m_RenderPassFullGBuffer");
		m_RenderPassJustDepthAndHdr.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD, !bClear, "m_RenderPassJustDepthAndHdr");
	}

	CreateApplyReflectionsPipeline();
	CreateDepthDownsamplePipeline();

	// Create render pass shadow, will clear contents
	{
		VkAttachmentDescription depthAttachments;
		AttachClearBeforeUse(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachments);
		m_RenderPassShadow = CreateRenderPassOptimal(m_pDevice->GetDevice(), 0, NULL, &depthAttachments);
	}

	m_SkyDome.OnCreate(pDevice, m_RenderPassJustDepthAndHdr.GetRenderPass(), &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\papermill\\diffuse.dds", "..\\media\\envmaps\\papermill\\specular.dds", VK_SAMPLE_COUNT_1_BIT);
	m_SkyDomeProc.OnCreate(pDevice, m_RenderPassJustDepthAndHdr.GetRenderPass(), &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_1_BIT);
	m_Wireframe.OnCreate(pDevice, m_RenderPassJustDepthAndHdr.GetRenderPass(), &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_1_BIT);
	m_WireframeBox.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
	m_DownSample.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);
	m_Bloom.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);
	m_TAA.OnCreate(pDevice, &m_ResourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing);
	m_MagnifierPS.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);

	// Create tonemapping pass
	m_ToneMappingCS.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing);
	m_ToneMappingPS.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_ResourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing);
	m_ColorConversionPS.OnCreate(pDevice, pSwapChain->GetRenderPass(), &m_ResourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing);

	// Initialize UI rendering resources
	m_ImGUI.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_UploadHeap, &m_ConstantBufferRing, FontSize);

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

	VkCommandBuffer cb1 = BeginNewCommandBuffer();
	m_Sssr.OnCreate(pDevice, cb1, &m_ResourceViewHeaps, &m_ConstantBufferRing, backBufferCount, true);
	// Wait for the upload to finish;
	SubmitCommandBuffer(cb1);
	m_pDevice->GPUFlush();

	m_BrdfLut.InitFromFile(pDevice, &m_UploadHeap, "BrdfLut.dds", false); // LUT images are stored as linear
	m_BrdfLut.CreateSRV(&m_BrdfLutSRV);

	// Make sure upload heap has finished uploading before continuing
	m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
	m_UploadHeap.FlushAndFinish();

	m_ApplyPipelineRTV = VK_NULL_HANDLE;
}

//--------------------------------------------------------------------------------------
//
// OnDestroy 
//
//--------------------------------------------------------------------------------------
void Renderer::OnDestroy()
{
	m_AsyncPool.Flush();

	m_ImGUI.OnDestroy();
	m_ColorConversionPS.OnDestroy();
	m_ToneMappingPS.OnDestroy();
	m_ToneMappingCS.OnDestroy();
	m_TAA.OnDestroy();
	m_Bloom.OnDestroy();
	m_DownSample.OnDestroy();
	m_MagnifierPS.OnDestroy();
	m_WireframeBox.OnDestroy();
	m_Wireframe.OnDestroy();
	m_SkyDomeProc.OnDestroy();
	m_SkyDome.OnDestroy();

	m_BrdfLut.OnDestroy();
	m_Sssr.OnDestroy();

	m_RenderPassFullGBufferWithClear.OnDestroy();
	m_RenderPassJustDepthAndHdr.OnDestroy();
	m_RenderPassFullGBuffer.OnDestroy();
	m_GBuffer.OnDestroy();

	VkDevice device = m_pDevice->GetDevice();

	vkDestroyRenderPass(device, m_RenderPassShadow, nullptr);
	vkDestroyRenderPass(device, m_ApplyRenderPass, nullptr);

	vkDestroySampler(device, m_LinearSampler, nullptr);
	vkDestroyImageView(device, m_BrdfLutSRV, nullptr);

	vkDestroyPipeline(device, m_DepthDownsamplePipeline, nullptr);
	vkDestroyPipelineLayout(device, m_DepthDownsamplePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_DepthDownsampleDescriptorSetLayout, nullptr);
	m_ResourceViewHeaps.FreeDescriptor(m_DepthDownsampleDescriptorSet);

	vkDestroyPipeline(device, m_ApplyPipeline, nullptr);
	vkDestroyPipelineLayout(device, m_ApplyPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_ApplyPipelineDescriptorSetLayouts[0], nullptr);
	vkDestroyDescriptorSetLayout(device, m_ApplyPipelineDescriptorSetLayouts[1], nullptr);

	for (int i = 0; i < 2; ++i)
	{
		m_ResourceViewHeaps.FreeDescriptor(m_ApplyPipelineDescriptorSet[i]);
	}

	for (int i = 0; i < backBufferCount; ++i)
	{
		m_ResourceViewHeaps.FreeDescriptor(m_ApplyPipelineUniformBufferDescriptorSet[i]);
	}

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
void Renderer::OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height)
{
	m_Width = Width;
	m_Height = Height;

	// Set the viewport
	//
	m_Viewport.x = 0;
	m_Viewport.y = (float)Height;
	m_Viewport.width = (float)Width;
	m_Viewport.height = -(float)(Height);
	m_Viewport.minDepth = (float)0.0f;
	m_Viewport.maxDepth = (float)1.0f;

	// Create scissor rectangle
	//
	m_RectScissor.extent.width = Width;
	m_RectScissor.extent.height = Height;
	m_RectScissor.offset.x = 0;
	m_RectScissor.offset.y = 0;

	// Create GBuffer
	//
	m_GBuffer.OnCreateWindowSizeDependentResources(pSwapChain, Width, Height);

	// Create frame buffers for the GBuffer render passes
	//
	m_RenderPassFullGBufferWithClear.OnCreateWindowSizeDependentResources(Width, Height);
	m_RenderPassJustDepthAndHdr.OnCreateWindowSizeDependentResources(Width, Height);
	m_RenderPassFullGBuffer.OnCreateWindowSizeDependentResources(Width, Height);

	// Update PostProcessing passes
	//
	m_DownSample.OnCreateWindowSizeDependentResources(Width, Height, &m_GBuffer.m_HDR, 6); //downsample the HDR texture 6 times
	m_Bloom.OnCreateWindowSizeDependentResources(Width / 2, Height / 2, m_DownSample.GetTexture(), 6, &m_GBuffer.m_HDR);
	m_TAA.OnCreateWindowSizeDependentResources(Width, Height, &m_GBuffer);
	m_MagnifierPS.OnCreateWindowSizeDependentResources(&m_GBuffer.m_HDR);
	m_bMagResourceReInit = true;

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
	imageCreateInfo.usage = (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	imageCreateInfo.flags = 0;

	imageCreateInfo.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;

	{
		m_GBuffer.m_HDR.CreateRTV(&m_ApplyPipelineRTV);
		VkImageView attachmentViews[1] = { m_ApplyPipelineRTV };

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.pNext = NULL;
		framebufferInfo.renderPass = m_ApplyRenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = attachmentViews;
		framebufferInfo.width = m_Width;
		framebufferInfo.height = m_Height;
		framebufferInfo.layers = 1;

		VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &framebufferInfo, NULL, &m_ApplyFramebuffer);
		assert(res == VK_SUCCESS);
	}

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
	sssrInput.HDRView = m_GBuffer.m_HDRSRV;
	sssrInput.DepthHierarchy = &m_DepthHierarchy;
	sssrInput.DepthHierarchyView = m_DepthHierarchySRV;
	sssrInput.MotionVectorsView = m_GBuffer.m_MotionVectorsSRV;
	sssrInput.NormalBuffer = &m_GBuffer.m_NormalBuffer;
	sssrInput.NormalBufferView = m_GBuffer.m_NormalBufferSRV;
	sssrInput.SpecularRoughnessView = m_GBuffer.m_SpecularRoughnessSRV;
	sssrInput.EnvironmentMapView = m_SkyDome.GetCubeSpecularTextureView();
	sssrInput.EnvironmentMapSampler = m_SkyDome.GetCubeSpecularTextureSampler();
	sssrInput.outputWidth = m_Width;
	sssrInput.outputHeight = m_Height;
	m_Sssr.OnCreateWindowSizeDependentResources(cb, sssrInput);

	for (int i = 0; i < 2; ++i)
	{
		// Fill apply reflections descriptor set
		VkDescriptorImageInfo applyReflectionsImageInfos[5];
		applyReflectionsImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[0].imageView = m_Sssr.GetOutputTextureView(i);
		applyReflectionsImageInfos[0].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[1].imageView = m_GBuffer.m_NormalBufferSRV;
		applyReflectionsImageInfos[1].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[2].imageView = m_GBuffer.m_SpecularRoughnessSRV;
		applyReflectionsImageInfos[2].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		applyReflectionsImageInfos[3].imageView = m_BrdfLutSRV;
		applyReflectionsImageInfos[3].sampler = VK_NULL_HANDLE;
		applyReflectionsImageInfos[4].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		applyReflectionsImageInfos[4].imageView = VK_NULL_HANDLE;
		applyReflectionsImageInfos[4].sampler = m_LinearSampler;

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
	downsampleImageInfos[0].imageView = m_GBuffer.m_DepthBufferDSV;
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
		Transition(m_DepthHierarchy.Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, m_DepthMipLevelCount),
		Transition(m_DownSample.GetTexture()->Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 6),
		});

	SubmitCommandBuffer(cb);
}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void Renderer::OnDestroyWindowSizeDependentResources()
{
	VkDevice device = m_pDevice->GetDevice();

	m_Bloom.OnDestroyWindowSizeDependentResources();
	m_DownSample.OnDestroyWindowSizeDependentResources();
	m_TAA.OnDestroyWindowSizeDependentResources();
	m_MagnifierPS.OnDestroyWindowSizeDependentResources();

	m_RenderPassFullGBufferWithClear.OnDestroyWindowSizeDependentResources();
	m_RenderPassJustDepthAndHdr.OnDestroyWindowSizeDependentResources();
	m_RenderPassFullGBuffer.OnDestroyWindowSizeDependentResources();
	m_GBuffer.OnDestroyWindowSizeDependentResources();
	m_Sssr.OnDestroyWindowSizeDependentResources();

	vkDestroyImageView(device, m_ApplyPipelineRTV, nullptr);

	for (int i = 0; i < 13; ++i)
	{
		if (m_DepthHierarchyDescriptors[i] != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_DepthHierarchyDescriptors[i], nullptr);
		}
		m_DepthHierarchyDescriptors[i] = VK_NULL_HANDLE;
	}
	vkDestroyImageView(device, m_DepthHierarchySRV, nullptr);
	vkDestroyBufferView(device, m_AtomicCounterUAV, nullptr);

	m_DepthHierarchy.OnDestroy();

	vkDestroyFramebuffer(device, m_ApplyFramebuffer, nullptr);

	vmaDestroyBuffer(m_pDevice->GetAllocator(), m_AtomicCounter, m_AtomicCounterAllocation);
}

void Renderer::OnUpdateDisplayDependentResources(SwapChain* pSwapChain, bool bUseMagnifier)
{
	// Update the pipelines if the swapchain render pass has changed (for example when the format of the swapchain changes)
	//
	m_ColorConversionPS.UpdatePipelines(pSwapChain->GetRenderPass(), pSwapChain->GetDisplayMode());
	m_ToneMappingPS.UpdatePipelines(pSwapChain->GetRenderPass());

	m_ImGUI.UpdatePipeline((pSwapChain->GetDisplayMode() == DISPLAYMODE_SDR) ? pSwapChain->GetRenderPass() : bUseMagnifier ? m_MagnifierPS.GetPassRenderPass() : m_RenderPassJustDepthAndHdr.GetRenderPass());
}

//--------------------------------------------------------------------------------------
//
// OnUpdateLocalDimmingChangedResources
//
//--------------------------------------------------------------------------------------
void Renderer::OnUpdateLocalDimmingChangedResources(SwapChain* pSwapChain)
{
	m_ColorConversionPS.UpdatePipelines(pSwapChain->GetRenderPass(), pSwapChain->GetDisplayMode());
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int Renderer::LoadScene(GLTFCommon* pGLTFCommon, int Stage)
{
	// show loading progress
	//
	ImGui::OpenPopup("Loading");
	if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		float progress = (float)Stage / 13.0f;
		ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
		ImGui::EndPopup();
	}

	// use multi threading
	AsyncPool* pAsyncPool = &m_AsyncPool;

	// Loading stages
	//
	if (Stage == 0)
	{

	}
	else if (Stage == 5)
	{
		Profile p("m_pGltfLoader->Load");

		m_pGLTFTexturesAndBuffers = new GLTFTexturesAndBuffers();
		m_pGLTFTexturesAndBuffers->OnCreate(m_pDevice, pGLTFCommon, &m_UploadHeap, &m_VidMemBufferPool, &m_ConstantBufferRing);
	}
	else if (Stage == 6)
	{
		Profile p("LoadTextures");

		// here we are loading onto the GPU all the textures and the inverse matrices
		// this data will be used to create the PBR and Depth passes       
		m_pGLTFTexturesAndBuffers->LoadTextures(pAsyncPool);
	}
	else if (Stage == 7)
	{
		Profile p("m_gltfDepth->OnCreate");

		//create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
		m_GLTFDepth = new GltfDepthPass();
		m_GLTFDepth->OnCreate(
			m_pDevice,
			m_RenderPassShadow,
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			pAsyncPool
		);

		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
		m_UploadHeap.FlushAndFinish();
	}
	else if (Stage == 8)
	{
		Profile p("m_GLTFPBR->OnCreate");

		// same thing as above but for the PBR pass
		m_GLTFPBR = new GltfPbrPass();
		m_GLTFPBR->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			&m_SkyDome,
			false, // use SSAO mask
			m_ShadowSRVPool,
			&m_RenderPassFullGBufferWithClear,
			pAsyncPool
		);

		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
		m_UploadHeap.FlushAndFinish();
	}
	else if (Stage == 9)
	{
		Profile p("m_GLTFBBox->OnCreate");

		// just a bounding box pass that will draw boundingboxes instead of the geometry itself
		m_GLTFBBox = new GltfBBoxPass();
		m_GLTFBBox->OnCreate(
			m_pDevice,
			m_RenderPassJustDepthAndHdr.GetRenderPass(),
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			&m_Wireframe
		);

		// we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
	}
	else if (Stage == 10)
	{
		Profile p("Flush");

		m_UploadHeap.FlushAndFinish();

		//once everything is uploaded we dont need the upload heaps anymore
		m_VidMemBufferPool.FreeUploadHeap();

		// tell caller that we are done loading the map
		return 0;
	}

	Stage++;
	return Stage;
}

//--------------------------------------------------------------------------------------
//
// UnloadScene
//
//--------------------------------------------------------------------------------------
void Renderer::UnloadScene()
{
	// wait for all the async loading operations to finish
	m_AsyncPool.Flush();

	m_pDevice->GPUFlush();

	if (m_GLTFPBR)
	{
		m_GLTFPBR->OnDestroy();
		delete m_GLTFPBR;
		m_GLTFPBR = NULL;
	}

	if (m_GLTFDepth)
	{
		m_GLTFDepth->OnDestroy();
		delete m_GLTFDepth;
		m_GLTFDepth = NULL;
	}

	if (m_GLTFBBox)
	{
		m_GLTFBBox->OnDestroy();
		delete m_GLTFBBox;
		m_GLTFBBox = NULL;
	}

	if (m_pGLTFTexturesAndBuffers)
	{
		m_pGLTFTexturesAndBuffers->OnDestroy();
		delete m_pGLTFTexturesAndBuffers;
		m_pGLTFTexturesAndBuffers = NULL;
	}

	assert(m_shadowMapPool.size() == m_ShadowSRVPool.size());
	while (!m_shadowMapPool.empty())
	{
		m_shadowMapPool.back().ShadowMap.OnDestroy();
		vkDestroyFramebuffer(m_pDevice->GetDevice(), m_shadowMapPool.back().ShadowFrameBuffer, nullptr);
		vkDestroyImageView(m_pDevice->GetDevice(), m_ShadowSRVPool.back(), nullptr);
		vkDestroyImageView(m_pDevice->GetDevice(), m_shadowMapPool.back().ShadowDSV, nullptr);
		m_ShadowSRVPool.pop_back();
		m_shadowMapPool.pop_back();
	}
}

void Renderer::AllocateShadowMaps(GLTFCommon* pGLTFCommon)
{
	// Go through the lights and allocate shadow information
	uint32_t NumShadows = 0;
	for (int i = 0; i < pGLTFCommon->m_lightInstances.size(); ++i)
	{
		const tfLight& lightData = pGLTFCommon->m_lights[pGLTFCommon->m_lightInstances[i].m_lightId];
		if (lightData.m_shadowResolution)
		{
			SceneShadowInfo ShadowInfo;
			ShadowInfo.ShadowResolution = lightData.m_shadowResolution;
			ShadowInfo.ShadowIndex = NumShadows++;
			ShadowInfo.LightIndex = i;
			m_shadowMapPool.push_back(ShadowInfo);
		}
	}

	if (NumShadows > MaxShadowInstances)
	{
		Trace("Number of shadows has exceeded maximum supported. Please grow value in gltfCommon.h/perFrameStruct.h");
		throw;
	}

	// If we had shadow information, allocate all required maps and bindings
	if (!m_shadowMapPool.empty())
	{
		std::vector<SceneShadowInfo>::iterator CurrentShadow = m_shadowMapPool.begin();
		for (uint32_t i = 0; CurrentShadow < m_shadowMapPool.end(); ++i, ++CurrentShadow)
		{
			CurrentShadow->ShadowMap.InitDepthStencil(m_pDevice, CurrentShadow->ShadowResolution, CurrentShadow->ShadowResolution, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, "ShadowMap");
			CurrentShadow->ShadowMap.CreateDSV(&CurrentShadow->ShadowDSV);

			// Create render pass shadow, will clear contents
			{
				VkAttachmentDescription depthAttachments;
				AttachClearBeforeUse(VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachments);

				// Create frame buffer
				VkImageView attachmentViews[1] = { CurrentShadow->ShadowDSV };
				VkFramebufferCreateInfo fb_info = {};
				fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				fb_info.pNext = NULL;
				fb_info.renderPass = m_RenderPassShadow;
				fb_info.attachmentCount = 1;
				fb_info.pAttachments = attachmentViews;
				fb_info.width = CurrentShadow->ShadowResolution;
				fb_info.height = CurrentShadow->ShadowResolution;
				fb_info.layers = 1;
				VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &CurrentShadow->ShadowFrameBuffer);
				assert(res == VK_SUCCESS);
			}

			VkImageView ShadowSRV;
			CurrentShadow->ShadowMap.CreateSRV(&ShadowSRV);
			m_ShadowSRVPool.push_back(ShadowSRV);
		}
	}
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void Renderer::OnRender(const UIState* pState, const Camera& Cam, SwapChain* pSwapChain)
{
	StallFrame(pState->targetFrameTime);

	// Let our resource managers do some house keeping 
	m_ConstantBufferRing.OnBeginFrame();

	// command buffer calls
	VkCommandBuffer cmdBuf1 = m_CommandListRing.GetNewCommandList();

	{
		VkCommandBufferBeginInfo cmd_buf_info;
		cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmd_buf_info.pNext = NULL;
		cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		cmd_buf_info.pInheritanceInfo = NULL;
		VkResult res = vkBeginCommandBuffer(cmdBuf1, &cmd_buf_info);
		assert(res == VK_SUCCESS);
	}

	m_GPUTimer.OnBeginFrame(cmdBuf1, &m_TimeStamps);

	// Sets the perFrame data 
	per_frame* pPerFrame = NULL;
	if (m_pGLTFTexturesAndBuffers)
	{
		// fill as much as possible using the GLTF (camera, lights, ...)
		pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(Cam);

		// Set some lighting factors
		pPerFrame->iblFactor = pState->IBLFactor;
		pPerFrame->emmisiveFactor = pState->EmissiveFactor;
		pPerFrame->invScreenResolution[0] = 1.0f / ((float)m_Width);
		pPerFrame->invScreenResolution[1] = 1.0f / ((float)m_Height);

		pPerFrame->wireframeOptions.setX(pState->WireframeColor[0]);
		pPerFrame->wireframeOptions.setY(pState->WireframeColor[1]);
		pPerFrame->wireframeOptions.setZ(pState->WireframeColor[2]);
		pPerFrame->wireframeOptions.setW(pState->WireframeMode == UIState::WireframeMode::WIREFRAME_MODE_SOLID_COLOR ? 1.0f : 0.0f);

		m_pGLTFTexturesAndBuffers->SetPerFrameConstants();
		m_pGLTFTexturesAndBuffers->SetSkinningMatricesForSkeletons();
	}

	// Render all shadow maps
	if (m_GLTFDepth && pPerFrame != NULL)
	{
		SetPerfMarkerBegin(cmdBuf1, "ShadowPass");

		VkClearValue depthClearValues[1];
		depthClearValues[0].depthStencil.depth = 1.0f;
		depthClearValues[0].depthStencil.stencil = 0;

		VkRenderPassBeginInfo rpBegin;
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.pNext = NULL;
		rpBegin.renderPass = m_RenderPassShadow;
		rpBegin.renderArea.offset.x = 0;
		rpBegin.renderArea.offset.y = 0;
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues = depthClearValues;

		std::vector<SceneShadowInfo>::iterator ShadowMap = m_shadowMapPool.begin();
		while (ShadowMap < m_shadowMapPool.end())
		{
			// Clear shadow map
			rpBegin.framebuffer = ShadowMap->ShadowFrameBuffer;
			rpBegin.renderArea.extent.width = ShadowMap->ShadowResolution;
			rpBegin.renderArea.extent.height = ShadowMap->ShadowResolution;
			vkCmdBeginRenderPass(cmdBuf1, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			// Render to shadow map
			SetViewportAndScissor(cmdBuf1, 0, 0, ShadowMap->ShadowResolution, ShadowMap->ShadowResolution);

			// Set per frame constant buffer values
			GltfDepthPass::per_frame* cbPerFrame = m_GLTFDepth->SetPerFrameConstants();
			cbPerFrame->mViewProj = pPerFrame->lights[ShadowMap->LightIndex].mLightViewProj;

			m_GLTFDepth->Draw(cmdBuf1);

			m_GPUTimer.GetTimeStamp(cmdBuf1, "Shadow Map Render");

			vkCmdEndRenderPass(cmdBuf1);
			++ShadowMap;
		}

		SetPerfMarkerEnd(cmdBuf1);
	}

	// Render Scene to the GBuffer ------------------------------------------------
	SetPerfMarkerBegin(cmdBuf1, "Color pass");

	VkRect2D renderArea = { 0, 0, m_Width, m_Height };

	if (pPerFrame != NULL && m_GLTFPBR)
	{
		const bool bWireframe = pState->WireframeMode != UIState::WireframeMode::WIREFRAME_MODE_OFF;

		std::vector<GltfPbrPass::BatchList> opaque, transparent;
		m_GLTFPBR->BuildBatchLists(&opaque, &transparent, bWireframe);

		// Render opaque 
		{
			m_RenderPassFullGBufferWithClear.BeginPass(cmdBuf1, renderArea);

			m_GLTFPBR->DrawBatchList(cmdBuf1, &opaque, bWireframe);
			m_GPUTimer.GetTimeStamp(cmdBuf1, "PBR Opaque");

			m_RenderPassFullGBufferWithClear.EndPass(cmdBuf1);
		}

		// Render skydome
		{
			m_RenderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);

			if (pState->SelectedSkydomeTypeIndex == 1)
			{
				math::Matrix4 clipToView = math::inverse(pPerFrame->mCameraCurrViewProj);
				m_SkyDome.Draw(cmdBuf1, clipToView);

				m_GPUTimer.GetTimeStamp(cmdBuf1, "Skydome cube");
			}
			else if (pState->SelectedSkydomeTypeIndex == 0)
			{
				SkyDomeProc::Constants skyDomeConstants;
				skyDomeConstants.invViewProj = math::inverse(pPerFrame->mCameraCurrViewProj);
				skyDomeConstants.vSunDirection = math::Vector4(1.0f, 0.05f, 0.0f, 0.0f);
				skyDomeConstants.turbidity = 10.0f;
				skyDomeConstants.rayleigh = 2.0f;
				skyDomeConstants.mieCoefficient = 0.005f;
				skyDomeConstants.mieDirectionalG = 0.8f;
				skyDomeConstants.luminance = 1.0f;
				m_SkyDomeProc.Draw(cmdBuf1, skyDomeConstants);

				m_GPUTimer.GetTimeStamp(cmdBuf1, "Skydome Proc");
			}

			m_RenderPassJustDepthAndHdr.EndPass(cmdBuf1);
		}

		// draw transparent geometry
		{
			m_RenderPassFullGBuffer.BeginPass(cmdBuf1, renderArea);

			std::sort(transparent.begin(), transparent.end());
			m_GLTFPBR->DrawBatchList(cmdBuf1, &transparent, bWireframe);
			m_GPUTimer.GetTimeStamp(cmdBuf1, "PBR Transparent");

			m_RenderPassFullGBuffer.EndPass(cmdBuf1);
		}

		Barriers(cmdBuf1, {
			Transition(m_GBuffer.m_HDR.Resource(),					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			Transition(m_GBuffer.m_NormalBuffer.Resource(),			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			Transition(m_GBuffer.m_MotionVectors.Resource(),		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			Transition(m_GBuffer.m_SpecularRoughness.Resource(),	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			Transition(m_GBuffer.m_DepthBuffer.Resource(),			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
			});

		// Downsample depth buffer
		DownsampleDepthBuffer(cmdBuf1);

		Barriers(cmdBuf1, {
			Transition(m_DepthHierarchy.Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, m_DepthMipLevelCount),
		});

		// Stochastic SSR
		RenderScreenSpaceReflections(cmdBuf1, Cam, pPerFrame, pState);

		// Apply the result of SSR
		ApplyReflectionTarget(cmdBuf1, Cam, pState);

		Barriers(cmdBuf1, {
			Transition(m_DepthHierarchy.Resource(),					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,							VK_IMAGE_ASPECT_COLOR_BIT, m_DepthMipLevelCount),
			Transition(m_GBuffer.m_DepthBuffer.Resource(),			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
			Transition(m_GBuffer.m_HDR.Resource(),					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
			Transition(m_GBuffer.m_NormalBuffer.Resource(),			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
			Transition(m_GBuffer.m_MotionVectors.Resource(),		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
			Transition(m_GBuffer.m_SpecularRoughness.Resource(),	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
		});

		// draw object's bounding boxes
		{
			m_RenderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);

			if (m_GLTFBBox)
			{
				if (pState->bDrawBoundingBoxes)
				{
					m_GLTFBBox->Draw(cmdBuf1, pPerFrame->mCameraCurrViewProj);

					m_GPUTimer.GetTimeStamp(cmdBuf1, "Bounding Box");
				}
			}

			// draw light's frustums
			if (pState->bDrawLightFrustum && pPerFrame != NULL)
			{
				SetPerfMarkerBegin(cmdBuf1, "light frustums");

				math::Vector4 vCenter = math::Vector4(0.0f, 0.0f, 0.5f, 0.0f);
				math::Vector4 vRadius = math::Vector4(1.0f, 1.0f, 0.5f, 0.0f);
				math::Vector4 vColor = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
				{
					math::Matrix4 spotlightMatrix = math::inverse(pPerFrame->lights[i].mLightViewProj);
					math::Matrix4 worldMatrix = pPerFrame->mCameraCurrViewProj * spotlightMatrix;
					m_WireframeBox.Draw(cmdBuf1, &m_Wireframe, worldMatrix, vCenter, vRadius, vColor);
				}

				m_GPUTimer.GetTimeStamp(cmdBuf1, "Light's frustum");

				SetPerfMarkerEnd(cmdBuf1);
			}

			m_RenderPassJustDepthAndHdr.EndPass(cmdBuf1);
		}
	}
	else
	{
		m_RenderPassFullGBufferWithClear.BeginPass(cmdBuf1, renderArea);
		m_RenderPassFullGBufferWithClear.EndPass(cmdBuf1);
		m_RenderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);
		m_RenderPassJustDepthAndHdr.EndPass(cmdBuf1);
	}

	VkImageMemoryBarrier barrier[1] = {};
	barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier[0].pNext = NULL;
	barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier[0].subresourceRange.baseMipLevel = 0;
	barrier[0].subresourceRange.levelCount = 1;
	barrier[0].subresourceRange.baseArrayLayer = 0;
	barrier[0].subresourceRange.layerCount = 1;
	barrier[0].image = m_GBuffer.m_HDR.Resource();
	vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, barrier);

	SetPerfMarkerEnd(cmdBuf1);

	// Post proc---------------------------------------------------------------------------

	// Bloom, takes HDR as input and applies bloom to it.
	if (pState->bUseBloom)
	{
		SetPerfMarkerBegin(cmdBuf1, "PostProcess");

		// Downsample pass
		m_DownSample.Draw(cmdBuf1);
		m_GPUTimer.GetTimeStamp(cmdBuf1, "Downsample");

		// Bloom pass (needs the downsampled data)
		m_Bloom.Draw(cmdBuf1);
		m_GPUTimer.GetTimeStamp(cmdBuf1, "Bloom");

		SetPerfMarkerEnd(cmdBuf1);
	}

	// Apply TAA & Sharpen to m_HDR
	if (pState->bUseTAA)
	{
		{
			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.pNext = NULL;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			VkImageMemoryBarrier barriers[3];
			barriers[0] = barrier;
			barriers[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			barriers[0].image = m_GBuffer.m_DepthBuffer.Resource();

			barriers[1] = barrier;
			barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barriers[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barriers[1].image = m_GBuffer.m_MotionVectors.Resource();

			// no layout transition but we still need to wait
			barriers[2] = barrier;
			barriers[2].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[2].image = m_GBuffer.m_HDR.Resource();

			vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 3, barriers);
		}

		m_TAA.Draw(cmdBuf1);
		m_GPUTimer.GetTimeStamp(cmdBuf1, "TAA");
	}


	// Magnifier Pass: m_HDR as input, pass' own output
	if (pState->bUseMagnifier)
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.pNext = NULL;
		barrier.srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.image = m_MagnifierPS.GetPassOutputResource();

		if (m_bMagResourceReInit)
		{
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
			m_bMagResourceReInit = false;
		}
		else
		{
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
		}

		// Note: assumes the input texture (specified in OnCreateWindowSizeDependentResources()) is in read state
		m_MagnifierPS.Draw(cmdBuf1, pState->MagnifierParams);
		m_GPUTimer.GetTimeStamp(cmdBuf1, "Magnifier");
	}


	// Start tracking input/output resources at this point to handle HDR and SDR render paths 
	VkImage      ImgCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputResource() : m_GBuffer.m_HDR.Resource();
	VkImageView  SRVCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputSRV() : m_GBuffer.m_HDRSRV;

	// If using FreeSync HDR, we need to do these in order: Tonemapping -> GUI -> Color Conversion
	const bool bHDR = pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR;
	if (bHDR)
	{
		// In place Tonemapping ------------------------------------------------------------------------
		{
			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.pNext = NULL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.image = ImgCurrentInput;
			vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

			m_ToneMappingCS.Draw(cmdBuf1, SRVCurrentInput, pState->Exposure, pState->SelectedTonemapperIndex, m_Width, m_Height);

			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.image = ImgCurrentInput;
			vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

		}

		// Render HUD  ------------------------------------------------------------------------
		{

			if (pState->bUseMagnifier)
			{
				m_MagnifierPS.BeginPass(cmdBuf1, renderArea);
			}
			else
			{
				m_RenderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);
			}

			vkCmdSetScissor(cmdBuf1, 0, 1, &m_RectScissor);
			vkCmdSetViewport(cmdBuf1, 0, 1, &m_Viewport);

			m_ImGUI.Draw(cmdBuf1);

			if (pState->bUseMagnifier)
			{
				m_MagnifierPS.EndPass(cmdBuf1);
			}
			else
			{
				m_RenderPassJustDepthAndHdr.EndPass(cmdBuf1);
			}

			if (bHDR && !pState->bUseMagnifier)
			{
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.pNext = NULL;
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;
				barrier.image = ImgCurrentInput;
				vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
			}

			m_GPUTimer.GetTimeStamp(cmdBuf1, "ImGUI Rendering");
		}
	}

	// submit command buffer
	{
		VkResult res = vkEndCommandBuffer(cmdBuf1);
		assert(res == VK_SUCCESS);

		VkSubmitInfo submit_info;
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.pNext = NULL;
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &cmdBuf1;
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;
		res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
		assert(res == VK_SUCCESS);
	}

	// Wait for swapchain (we are going to render to it) -----------------------------------
	int imageIndex = pSwapChain->WaitForSwapChain();

	// Keep tracking input/output resource views 
	ImgCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputResource() : m_GBuffer.m_HDR.Resource(); // these haven't changed, re-assign as sanity check
	SRVCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputSRV() : m_GBuffer.m_HDRSRV;         // these haven't changed, re-assign as sanity check

	m_CommandListRing.OnBeginFrame();

	VkCommandBuffer cmdBuf2 = m_CommandListRing.GetNewCommandList();

	{
		VkCommandBufferBeginInfo cmd_buf_info;
		cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmd_buf_info.pNext = NULL;
		cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		cmd_buf_info.pInheritanceInfo = NULL;
		VkResult res = vkBeginCommandBuffer(cmdBuf2, &cmd_buf_info);
		assert(res == VK_SUCCESS);
	}

	SetPerfMarkerBegin(cmdBuf2, "Swapchain RenderPass");

	// prepare render pass
	{
		VkRenderPassBeginInfo rpBegin = {};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.pNext = NULL;
		rpBegin.renderPass = pSwapChain->GetRenderPass();
		rpBegin.framebuffer = pSwapChain->GetFramebuffer(imageIndex);
		rpBegin.renderArea.offset.x = 0;
		rpBegin.renderArea.offset.y = 0;
		rpBegin.renderArea.extent.width = m_Width;
		rpBegin.renderArea.extent.height = m_Height;
		rpBegin.clearValueCount = 0;
		rpBegin.pClearValues = NULL;
		vkCmdBeginRenderPass(cmdBuf2, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
	}

	vkCmdSetScissor(cmdBuf2, 0, 1, &m_RectScissor);
	vkCmdSetViewport(cmdBuf2, 0, 1, &m_Viewport);

	if (bHDR)
	{
		m_ColorConversionPS.Draw(cmdBuf2, SRVCurrentInput);
		m_GPUTimer.GetTimeStamp(cmdBuf2, "Color Conversion");
	}

	// For SDR pipeline, we apply the tonemapping and then draw the GUI and skip the color conversion
	else
	{
		// Tonemapping ------------------------------------------------------------------------
		{
			m_ToneMappingPS.Draw(cmdBuf2, SRVCurrentInput, pState->Exposure, pState->SelectedTonemapperIndex);
			m_GPUTimer.GetTimeStamp(cmdBuf2, "Tonemapping");
		}

		// Render HUD  -------------------------------------------------------------------------
		{
			m_ImGUI.Draw(cmdBuf2);
			m_GPUTimer.GetTimeStamp(cmdBuf2, "ImGUI Rendering");
		}
	}

	SetPerfMarkerEnd(cmdBuf2);

	m_GPUTimer.OnEndFrame();

	vkCmdEndRenderPass(cmdBuf2);

	// Close & Submit the command list ----------------------------------------------------
	{
		VkResult res = vkEndCommandBuffer(cmdBuf2);
		assert(res == VK_SUCCESS);

		VkSemaphore ImageAvailableSemaphore;
		VkSemaphore RenderFinishedSemaphores;
		VkFence CmdBufExecutedFences;
		pSwapChain->GetSemaphores(&ImageAvailableSemaphore, &RenderFinishedSemaphores, &CmdBufExecutedFences);

		VkPipelineStageFlags submitWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo2;
		submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo2.pNext = NULL;
		submitInfo2.waitSemaphoreCount = 1;
		submitInfo2.pWaitSemaphores = &ImageAvailableSemaphore;
		submitInfo2.pWaitDstStageMask = &submitWaitStage;
		submitInfo2.commandBufferCount = 1;
		submitInfo2.pCommandBuffers = &cmdBuf2;
		submitInfo2.signalSemaphoreCount = 1;
		submitInfo2.pSignalSemaphores = &RenderFinishedSemaphores;

		res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submitInfo2, CmdBufExecutedFences);
		assert(res == VK_SUCCESS);
	}

	m_FrameIndex++;
}

void Renderer::CreateApplyReflectionsPipeline()
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

	bindings[5].binding = 0;
	bindings[5].descriptorCount = 1;
	bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
	bindings[5].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCreateInfo[2];
	descSetLayoutCreateInfo[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCreateInfo[0].pNext = nullptr;
	descSetLayoutCreateInfo[0].bindingCount = 1;
	descSetLayoutCreateInfo[0].pBindings = &bindings[5];
	descSetLayoutCreateInfo[0].flags = 0;

	descSetLayoutCreateInfo[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCreateInfo[1].pNext = nullptr;
	descSetLayoutCreateInfo[1].bindingCount = 5;
	descSetLayoutCreateInfo[1].pBindings = &bindings[0];
	descSetLayoutCreateInfo[1].flags = 0;

	if (VK_SUCCESS != vkCreateDescriptorSetLayout(device, &descSetLayoutCreateInfo[0], nullptr, &m_ApplyPipelineDescriptorSetLayouts[0]))
	{
		Trace("Failed to create set layout for apply reflections pipeline.");
	}

	if (VK_SUCCESS != vkCreateDescriptorSetLayout(device, &descSetLayoutCreateInfo[1], nullptr, &m_ApplyPipelineDescriptorSetLayouts[1]))
	{
		Trace("Failed to create set layout for apply reflections pipeline.");
	}

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutCreateInfo.flags = 0;
	pipelineLayoutCreateInfo.pNext = nullptr;
	pipelineLayoutCreateInfo.setLayoutCount = ARRAYSIZE(m_ApplyPipelineDescriptorSetLayouts);
	pipelineLayoutCreateInfo.pSetLayouts = m_ApplyPipelineDescriptorSetLayouts;
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
	m_ApplyRenderPass = CreateRenderPassOptimal(m_pDevice->GetDevice(), _countof(colorAttachments), colorAttachments, nullptr);

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
	pipelineCreateInfo.renderPass = m_ApplyRenderPass;
	pipelineCreateInfo.subpass = 0;

	if (VK_SUCCESS != vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_ApplyPipeline))
	{
		Trace("Failed to create pipeline for the apply reflection target pass.");
	}

	for (int i = 0; i < 2; ++i)
	{
		m_ResourceViewHeaps.AllocDescriptor(m_ApplyPipelineDescriptorSetLayouts[1], &m_ApplyPipelineDescriptorSet[i]);
	}

	for (int i = 0; i < backBufferCount; ++i)
	{
		m_ResourceViewHeaps.AllocDescriptor(m_ApplyPipelineDescriptorSetLayouts[0], &m_ApplyPipelineUniformBufferDescriptorSet[i]);
	}
}

void Renderer::CreateDepthDownsamplePipeline()
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

void Renderer::StallFrame(float targetFrametime)
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

VkBufferMemoryBarrier Renderer::BufferBarrier(VkBuffer buffer)
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

VkImageMemoryBarrier Renderer::Transition(VkImage image, VkImageLayout before, VkImageLayout after, VkImageAspectFlags aspectMask, int mipCount)
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

void Renderer::Barriers(VkCommandBuffer cb, const std::vector<VkImageMemoryBarrier>& imageBarriers)
{
	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
}

VkCommandBuffer Renderer::BeginNewCommandBuffer()
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

void Renderer::SubmitCommandBuffer(VkCommandBuffer cb, VkSemaphore* waitSemaphore, VkSemaphore* signalSemaphores, VkFence fence)
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

void Renderer::DownsampleDepthBuffer(VkCommandBuffer cb)
{
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

void Renderer::RenderScreenSpaceReflections(VkCommandBuffer cb, const Camera& Cam, per_frame* pPerFrame, const UIState* pState)
{
	SSSRConstants sssrConstants = {};
	sssrConstants.bufferDimensions[0] = m_Width;
	sssrConstants.bufferDimensions[1] = m_Height;
	sssrConstants.inverseBufferDimensions[0] = 1.0f / m_Width;
	sssrConstants.inverseBufferDimensions[1] = 1.0f / m_Height;
	sssrConstants.frameIndex = m_FrameIndex;
	sssrConstants.maxTraversalIntersections = pState->maxTraversalIterations;
	sssrConstants.minTraversalOccupancy = pState->minTraversalOccupancy;
	sssrConstants.mostDetailedMip = pState->mostDetailedDepthHierarchyMipLevel;
	sssrConstants.temporalStabilityFactor = pState->temporalStability;
	sssrConstants.depthBufferThickness = pState->depthBufferThickness;
	sssrConstants.samplesPerQuad = pState->samplesPerQuad;
	sssrConstants.temporalVarianceGuidedTracingEnabled = pState->bEnableTemporalVarianceGuidedTracing ? 1 : 0;
	sssrConstants.varianceThreshold = pState->temporalVarianceThreshold;
	sssrConstants.roughnessThreshold = pState->roughnessThreshold;

	math::Matrix4 view = Cam.GetView();
	math::Matrix4 proj = Cam.GetProjection();

	sssrConstants.projection = proj;
	sssrConstants.invProjection = math::inverse(proj);
	sssrConstants.view = view;
	sssrConstants.invView = math::inverse(view);
	sssrConstants.prevViewProjection = pPerFrame->mCameraPrevViewProj;
	sssrConstants.invViewProjection = pPerFrame->mInverseCameraCurrViewProj;

	m_Sssr.Draw(cb, sssrConstants, m_GPUTimer, pState->bShowIntersectionResults);
}

void Renderer::ApplyReflectionTarget(VkCommandBuffer cb, const Camera& Cam, const UIState* pState)
{
	VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	beginInfo.pNext = nullptr;
	beginInfo.clearValueCount = 0;
	beginInfo.pClearValues = nullptr;
	beginInfo.renderArea = { 0, 0, m_Width, m_Height };
	beginInfo.renderPass = m_ApplyRenderPass;
	beginInfo.framebuffer = m_ApplyFramebuffer;
	vkCmdBeginRenderPass(cb, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

	SetPerfMarkerBegin(cb, "Apply Reflection View");

	struct PassConstants
	{
		math::Vector4 viewDir;
		UINT showReflectionTarget;
		UINT applyReflections;
	} constants;

	constants.viewDir = Cam.GetDirection();
	constants.showReflectionTarget = pState->bShowReflectionTarget ? 1 : 0;
	constants.applyReflections = pState->bApplyScreenSpaceReflections ? 1 : 0;

	uint32_t uniformBufferIndex = m_FrameIndex % backBufferCount;
	VkDescriptorSet uniformBufferDescriptorSet = m_ApplyPipelineUniformBufferDescriptorSet[uniformBufferIndex];
	{
		VkDescriptorBufferInfo uniformBufferInfo = m_ConstantBufferRing.AllocConstantBuffer(sizeof(PassConstants), &constants);
		VkWriteDescriptorSet writeSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		writeSet.pNext = nullptr;
		writeSet.dstSet = uniformBufferDescriptorSet;
		writeSet.dstBinding = 0;
		writeSet.dstArrayElement = 0;
		writeSet.descriptorCount = 1;
		writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeSet.pImageInfo = nullptr;
		writeSet.pBufferInfo = &uniformBufferInfo;
		writeSet.pTexelBufferView = nullptr;
		vkUpdateDescriptorSets(m_pDevice->GetDevice(), 1, &writeSet, 0, nullptr);
	}

	VkDescriptorSet sets[] = { uniformBufferDescriptorSet, m_ApplyPipelineDescriptorSet[m_FrameIndex % 2] };
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ApplyPipeline);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ApplyPipelineLayout, 0, _countof(sets), sets, 0, nullptr);
	vkCmdSetViewport(cb, 0, 1, &m_Viewport);
	vkCmdSetScissor(cb, 0, 1, &m_RectScissor);

	vkCmdDraw(cb, 3, 1, 0, 0);

	m_GPUTimer.GetTimeStamp(cb, "Apply Reflection View");
	SetPerfMarkerEnd(cb);

	vkCmdEndRenderPass(cb);
}