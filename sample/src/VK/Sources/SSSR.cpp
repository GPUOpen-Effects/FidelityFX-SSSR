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

#include "SSSR.h"
#include <cassert>

namespace _1spp
{
#include "../../../samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
}

/**
	The available blue noise sampler with 2ssp sampling mode.
*/
struct
{
	std::int32_t const (&m_sobolBuffer)[256 * 256];
	std::int32_t const (&m_rankingTileBuffer)[128 * 128 * 8];
	std::int32_t const (&scramblingTileBuffer)[128 * 128 * 8];
}
const g_blueNoiseSamplerState = { _1spp::sobol_256spp_256d,  _1spp::rankingTile,  _1spp::scramblingTile };

VkDescriptorSetLayoutBinding Bind(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding layoutBinding = {};
	layoutBinding.binding = binding;
	layoutBinding.descriptorType = type;
	layoutBinding.descriptorCount = 1;
	layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	layoutBinding.pImmutableSamplers = nullptr;
	return layoutBinding;
};

void SetDescriptorSetStructuredBuffer(VkDevice device, uint32_t index, VkBuffer& buffer, VkDescriptorSet descriptorSet, VkDescriptorType type)
{
	VkDescriptorBufferInfo bufferinfo;
	bufferinfo.buffer = buffer;
	bufferinfo.offset = 0;
	bufferinfo.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write_set.pNext = nullptr;
	write_set.dstSet = descriptorSet;
	write_set.dstBinding = index;
	write_set.dstArrayElement = 0;
	write_set.descriptorCount = 1;
	write_set.descriptorType = type;
	write_set.pImageInfo = nullptr;
	write_set.pBufferInfo = &bufferinfo;
	write_set.pTexelBufferView = nullptr;

	vkUpdateDescriptorSets(device, 1, &write_set, 0, NULL);
};

void SetDescriptorSetBuffer(VkDevice device, uint32_t index, VkBufferView bufferView, VkDescriptorSet descriptorSet, VkDescriptorType type)
{
	VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write_set.pNext = nullptr;
	write_set.dstSet = descriptorSet;
	write_set.dstBinding = index;
	write_set.dstArrayElement = 0;
	write_set.descriptorCount = 1;
	write_set.descriptorType = type;
	write_set.pImageInfo = nullptr;
	write_set.pBufferInfo = nullptr;
	write_set.pTexelBufferView = &bufferView;

	vkUpdateDescriptorSets(device, 1, &write_set, 0, NULL);
};

void SetDescriptorSet(VkDevice device, uint32_t index, VkImageView imageView, VkDescriptorSet descriptorSet, VkDescriptorType type, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL)
{
	VkDescriptorImageInfo desc_image;
	desc_image.sampler = VK_NULL_HANDLE;
	desc_image.imageView = imageView;
	desc_image.imageLayout = layout;

	VkWriteDescriptorSet write;
	write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.pNext = NULL;
	write.dstSet = descriptorSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = &desc_image;
	write.dstBinding = index;
	write.dstArrayElement = 0;

	vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

void SetDescriptorSetSampler(VkDevice device, uint32_t index, VkSampler sampler, VkDescriptorSet descriptorSet)
{
	VkDescriptorImageInfo image_info = {};
	image_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.imageView = VK_NULL_HANDLE;
	image_info.sampler = sampler;

	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.pNext = nullptr;
	write.dstSet = descriptorSet;
	write.dstBinding = index;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	write.pImageInfo = &image_info;
	write.pBufferInfo = nullptr;
	write.pTexelBufferView = nullptr;

	vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

void CopyToTexture(VkCommandBuffer cb, VkImage source, VkImage target, uint32_t width, uint32_t height)
{
	VkImageCopy region = {};
	region.dstOffset = { 0, 0, 0 };
	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.baseArrayLayer = 0;
	region.dstSubresource.layerCount = 1;
	region.dstSubresource.mipLevel = 0;
	region.extent = { width, height, 1 };
	region.srcOffset = { 0, 0, 0 };
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.baseArrayLayer = 0;
	region.srcSubresource.layerCount = 1;
	region.srcSubresource.mipLevel = 0;
	vkCmdCopyImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

using namespace CAULDRON_VK;
namespace SSSR_SAMPLE_VK
{
	void SSSR::OnCreate(Device* pDevice, VkCommandBuffer commandBuffer, ResourceViewHeaps* resourceHeap, DynamicBufferRing* constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters)
	{
		m_pDevice = pDevice;
		m_pConstantBufferRing = constantBufferRing;
		m_pResourceViewHeaps = resourceHeap;
		m_frameCountBeforeReuse = frameCountBeforeReuse;

		VkPhysicalDevice physicalDevice = m_pDevice->GetPhysicalDevice();
		VkDevice device = m_pDevice->GetDevice();

		// Query if the implementation supports VK_EXT_subgroup_size_control
		// This is the case if VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME is present.
		// Rely on the application to enable the extension if it's available.
		VkResult vkResult;
		uint32_t extensionCount;
		vkResult = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, NULL);
		assert(VK_SUCCESS == vkResult);
		std::vector<VkExtensionProperties> deviceExtensionProperties(extensionCount);
		vkResult = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, deviceExtensionProperties.data());
		assert(VK_SUCCESS == vkResult);
		m_isSubgroupSizeControlExtensionAvailable = std::find_if(deviceExtensionProperties.begin(), deviceExtensionProperties.end(),
			[](const VkExtensionProperties& extensionProps) -> bool { return strcmp(extensionProps.extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) == 0; })
			!= deviceExtensionProperties.end();

		m_uploadHeap.OnCreate(m_pDevice, 1024 * 1024);

		VkDescriptorSetLayoutBinding layoutBinding = {};
		layoutBinding.binding = 0;
		layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBinding.descriptorCount = 1;
		layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layoutBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		descriptorSetLayoutCreateInfo.pNext = nullptr;
		descriptorSetLayoutCreateInfo.flags = 0;
		descriptorSetLayoutCreateInfo.bindingCount = 1;
		descriptorSetLayoutCreateInfo.pBindings = &layoutBinding;

		vkResult = vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &m_uniformBufferDescriptorSetLayout);
		assert(vkResult == VK_SUCCESS);

		for (uint32_t i = 0; i < frameCountBeforeReuse; ++i)
		{
			bool bAllocDescriptor = m_pResourceViewHeaps->AllocDescriptor(m_uniformBufferDescriptorSetLayout, &m_uniformBufferDescriptorSet[i]);
			assert(bAllocDescriptor == true);
		}

		CreateResources(commandBuffer);
		SetupClassifyTilesPass();
		SetupBlueNoisePass();
		SetupPrepareIndirectArgsPass();
		SetupIntersectionPass();
		SetupResolveTemporalPass();
		SetupReprojectPass();
		SetupPrefilterPass();
	}

	void SSSR::OnCreateWindowSizeDependentResources(VkCommandBuffer commandBuffer, const SSSRCreationInfo& input)
	{
		assert(input.outputWidth != 0);
		assert(input.outputHeight != 0);
		assert(input.DepthHierarchy);
		assert(input.DepthHierarchyView != VK_NULL_HANDLE);
		assert(input.EnvironmentMapSampler != VK_NULL_HANDLE);
		assert(input.EnvironmentMapView != VK_NULL_HANDLE);
		assert(input.HDRView != VK_NULL_HANDLE);
		assert(input.MotionVectorsView != VK_NULL_HANDLE);
		assert(input.NormalBuffer);
		assert(input.NormalBufferView != VK_NULL_HANDLE);
		assert(input.SpecularRoughnessView != VK_NULL_HANDLE);

		m_outputWidth = input.outputWidth;
		m_outputHeight = input.outputHeight;
		m_depthHierarchy = input.DepthHierarchy;
		m_normalTexture = input.NormalBuffer;

		CreateWindowSizeDependentResources(commandBuffer);
		InitializeResourceDescriptorSets(input);
	}

	void SSSR::OnDestroy()
	{
		VkDevice device = m_pDevice->GetDevice();
		for (size_t i = 0; i < m_frameCountBeforeReuse; i++)
		{
			m_pResourceViewHeaps->FreeDescriptor(m_uniformBufferDescriptorSet[i]);
		}
		vkDestroyDescriptorSetLayout(device, m_uniformBufferDescriptorSetLayout, nullptr);

		m_classifyTilesPass.OnDestroy(device, m_pResourceViewHeaps);
		m_blueNoisePass.OnDestroy(device, m_pResourceViewHeaps);
		m_prepareIndirectArgsPass.OnDestroy(device, m_pResourceViewHeaps);
		m_intersectPass.OnDestroy(device, m_pResourceViewHeaps);
		m_resolveTemporalPass.OnDestroy(device, m_pResourceViewHeaps);
		m_reprojectPass.OnDestroy(device, m_pResourceViewHeaps);
		m_prefilterPass.OnDestroy(device, m_pResourceViewHeaps);
		m_uploadHeap.OnDestroy();

		m_rayCounter.OnDestroy();
		m_intersectionPassIndirectArgs.OnDestroy();

		vkDestroySampler(device, m_linearSampler, nullptr);
		vkDestroySampler(device, m_previousDepthSampler, nullptr);

		m_blueNoiseTexture.OnDestroy();
		m_blueNoiseSampler.OnDestroy();
	}

	void SSSR::OnDestroyWindowSizeDependentResources()
	{
		VkDevice device = m_pDevice->GetDevice();

		m_radiance[0].OnDestroy();
		m_radiance[1].OnDestroy();
		m_variance[0].OnDestroy();
		m_variance[1].OnDestroy();
		m_sampleCount[0].OnDestroy();
		m_sampleCount[1].OnDestroy();
		m_averageRadiance[0].OnDestroy();
		m_averageRadiance[1].OnDestroy();
		m_reprojectedRadiance.OnDestroy();
		m_roughnessTexture.OnDestroy();
		m_roughnessHistoryTexture.OnDestroy();
		m_normalHistoryTexture.OnDestroy();
		m_depthHistoryTexture.OnDestroy();
		m_rayList.OnDestroy();
		m_denoiserTileList.OnDestroy();
	}

	void SSSR::Draw(VkCommandBuffer commandBuffer, const SSSRConstants& sssrConstants, GPUTimestamps& gpuTimer, bool showIntersectResult)
	{
		SetPerfMarkerBegin(commandBuffer, "FidelityFX SSSR");

		uint32_t bufferIndex = sssrConstants.frameIndex % 2;
		uint32_t uniformBufferIndex = sssrConstants.frameIndex % m_frameCountBeforeReuse;
		VkDescriptorSet uniformBufferDescriptorSet = m_uniformBufferDescriptorSet[uniformBufferIndex];

		// Update descriptor to sliding window in upload buffer that contains the updated pass data
		{
			VkDescriptorBufferInfo uniformBufferInfo = m_pConstantBufferRing->AllocConstantBuffer(sizeof(SSSRConstants), (void*)&sssrConstants);

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

		// Classify Tiles & Prepare Blue Noise Texture 
		{
			VkImageMemoryBarrier barriers[] = {
				m_variance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_blueNoiseTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
			};
			TransitionBarriers(commandBuffer, barriers, _countof(barriers));

			SetPerfMarkerBegin(commandBuffer, "FFX DNSR ClassifyTiles");
			VkDescriptorSet classifySets[] = { uniformBufferDescriptorSet,  m_classifyTilesPass.descriptorSets[bufferIndex] };
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_classifyTilesPass.pipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_classifyTilesPass.pipelineLayout, 0, _countof(classifySets), classifySets, 0, nullptr);
			uint32_t dim_x = DivideRoundingUp(m_outputWidth, 8u);
			uint32_t dim_y = DivideRoundingUp(m_outputHeight, 8u);
			vkCmdDispatch(commandBuffer, dim_x, dim_y, 1);
			SetPerfMarkerEnd(commandBuffer);

			SetPerfMarkerBegin(commandBuffer, "FFX DNSR PrepareBlueNoise");
			VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_blueNoisePass.descriptorSets[bufferIndex] };
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_blueNoisePass.pipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_blueNoisePass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
			vkCmdDispatch(commandBuffer, 128u / 8u, 128u / 8u, 1);
			SetPerfMarkerEnd(commandBuffer);

			gpuTimer.GetTimeStamp(commandBuffer, "FFX DNSR ClassifyTiles + PrepareBlueNoise");
		}

		// Prepare Indirect Args and Intersection
		{
			VkImageMemoryBarrier barriers[] = {
				m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				m_blueNoiseTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
			};
			TransitionBarriers(commandBuffer, barriers, _countof(barriers));

			SetPerfMarkerBegin(commandBuffer, "FFX SSSR PrepareIndirectArgs");
			VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_prepareIndirectArgsPass.descriptorSets[bufferIndex] };
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prepareIndirectArgsPass.pipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prepareIndirectArgsPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
			vkCmdDispatch(commandBuffer, 1, 1, 1);
			SetPerfMarkerEnd(commandBuffer);
			gpuTimer.GetTimeStamp(commandBuffer, "FFX SSSR PrepareIndirectArgs");

			// Ensure that the arguments are written
			IndirectArgumentsBarrier(commandBuffer);

			SetPerfMarkerBegin(commandBuffer, "FFX SSSR Intersection");
			VkDescriptorSet intersectionSets[] = { uniformBufferDescriptorSet,  m_intersectPass.descriptorSets[bufferIndex] };
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_intersectPass.pipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_intersectPass.pipelineLayout, 0, _countof(intersectionSets), intersectionSets, 0, nullptr);
			vkCmdDispatchIndirect(commandBuffer, m_intersectionPassIndirectArgs.m_buffer, 0);
			SetPerfMarkerEnd(commandBuffer);
			gpuTimer.GetTimeStamp(commandBuffer, "FFX SSSR Intersection");
		}

		if (showIntersectResult)
		{
			// Ensure that the intersection pass finished
			{
				VkImageMemoryBarrier barriers[] = {
					m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				};
				TransitionBarriers(commandBuffer, barriers, _countof(barriers));
			}
		}
		else
		{
			uint32_t num_tilesX = DivideRoundingUp(m_outputWidth, 8u);
			uint32_t num_tilesY = DivideRoundingUp(m_outputHeight, 8u);

			// Reproject pass
			{
				VkImageMemoryBarrier barriers[] = {
					m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_depthHistoryTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_roughnessHistoryTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_normalHistoryTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_radiance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_averageRadiance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_variance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_sampleCount[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_blueNoiseTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_reprojectedRadiance.Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_averageRadiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_variance[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_sampleCount[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
				};
				TransitionBarriers(commandBuffer, barriers, _countof(barriers));

				SetPerfMarkerBegin(commandBuffer, "FFX DNSR Reproject");
				VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_reprojectPass.descriptorSets[bufferIndex] };
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_reprojectPass.pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_reprojectPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
				vkCmdDispatchIndirect(commandBuffer, m_intersectionPassIndirectArgs.m_buffer, 12);
				SetPerfMarkerEnd(commandBuffer);
				gpuTimer.GetTimeStamp(commandBuffer, "FFX DNSR Reproject");
			}

			// Prefilter pass
			{
				VkImageMemoryBarrier barriers[] = {
					m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_averageRadiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_variance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_sampleCount[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),

					m_radiance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_variance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_sampleCount[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
				};
				TransitionBarriers(commandBuffer, barriers, _countof(barriers));

				SetPerfMarkerBegin(commandBuffer, "FFX DNSR Prefilter");
				VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_prefilterPass.descriptorSets[bufferIndex] };
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilterPass.pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilterPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
				vkCmdDispatchIndirect(commandBuffer, m_intersectionPassIndirectArgs.m_buffer, 12);
				SetPerfMarkerEnd(commandBuffer);
				gpuTimer.GetTimeStamp(commandBuffer, "FFX DNSR Prefilter");
			}

			// Temporal resolve pass
			{
				VkImageMemoryBarrier barriers[] = {
					m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_averageRadiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_radiance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_reprojectedRadiance.Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_variance[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_sampleCount[1 - bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					
					m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_variance[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
					m_sampleCount[bufferIndex].Transition(VK_IMAGE_LAYOUT_GENERAL),
				};
				TransitionBarriers(commandBuffer, barriers, _countof(barriers));

				SetPerfMarkerBegin(commandBuffer, "FFX DNSR Resolve Temporal");
				VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_resolveTemporalPass.descriptorSets[bufferIndex] };
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveTemporalPass.pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveTemporalPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
				vkCmdDispatchIndirect(commandBuffer, m_intersectionPassIndirectArgs.m_buffer, 12);
				SetPerfMarkerEnd(commandBuffer);
				gpuTimer.GetTimeStamp(commandBuffer, "FFX DNSR Resolve Temporal");
			}

			// Ensure that the temporal resolve pass finished
			{
				VkImageMemoryBarrier barriers[] = {
					m_radiance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_variance[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					m_sampleCount[bufferIndex].Transition(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				};
				TransitionBarriers(commandBuffer, barriers, _countof(barriers));
			}

			// Optional depth copy to keep the depth buffer for the next frame. Skip this if your engine already keeps a copy around.
			{
				assert(m_depthHierarchy);
				assert(m_normalTexture);
				{
					VkImageMemoryBarrier barriers[] = {
						m_depthHistoryTexture.Transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
						Transition(m_depthHierarchy->Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
						m_normalHistoryTexture.Transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
						Transition(m_normalTexture->Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
						m_roughnessHistoryTexture.Transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
						m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
					};
					TransitionBarriers(commandBuffer, barriers, _countof(barriers));
				}

				CopyToTexture(commandBuffer, m_depthHierarchy->Resource(), m_depthHistoryTexture.Resource(), m_outputWidth, m_outputHeight);
				CopyToTexture(commandBuffer, m_normalTexture->Resource(), m_normalHistoryTexture.Resource(), m_outputWidth, m_outputHeight);
				CopyToTexture(commandBuffer, m_roughnessTexture.Resource(), m_roughnessHistoryTexture.Resource(), m_outputWidth, m_outputHeight);

				{
					VkImageMemoryBarrier barriers[] = {
						Transition(m_depthHierarchy->Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
						Transition(m_normalTexture->Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
					};
					TransitionBarriers(commandBuffer, barriers, _countof(barriers));
				}
			}
		}

		SetPerfMarkerEnd(commandBuffer);
	}

	void SSSR::GUI(int* pSlice)
	{

	}

	VkImageView SSSR::GetOutputTextureView(int frame) const
	{
		return m_radiance[frame % 2].View();
	}

	void SSSR::CreateResources(VkCommandBuffer commandBuffer)
	{
		VkDevice device = m_pDevice->GetDevice();
		VkPhysicalDevice physicalDevice = m_pDevice->GetPhysicalDevice();

		//==============================Create Tile Classification-related buffers============================================
		{
			uint32_t rayCounterElementCount = 4;

			BufferVK::CreateInfo createInfo = {};
			createInfo.memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			createInfo.format = VK_FORMAT_R32_UINT;
			createInfo.bufferUsage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			createInfo.sizeInBytes = rayCounterElementCount * sizeof(uint32_t);
			m_rayCounter = BufferVK(device, physicalDevice, createInfo, "SSSR - Ray Counter");
		}

		//==============================Create PrepareIndirectArgs-related buffers============================================
		{
			uint32_t intersectionPassIndirectArgsElementCount = 6;
			BufferVK::CreateInfo createInfo = {};
			createInfo.memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			createInfo.format = VK_FORMAT_R32_UINT;
			createInfo.bufferUsage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

			createInfo.sizeInBytes = intersectionPassIndirectArgsElementCount * sizeof(uint32_t);
			m_intersectionPassIndirectArgs = BufferVK(device, physicalDevice, createInfo, "SSSR - Intersect Indirect Args");
		}

		// Linear sampler
		{
			VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			samplerInfo.pNext = nullptr;
			samplerInfo.flags = 0;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.mipLodBias = 0;
			samplerInfo.anisotropyEnable = false;
			samplerInfo.maxAnisotropy = 0;
			samplerInfo.compareEnable = false;
			samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
			samplerInfo.minLod = 0;
			samplerInfo.maxLod = 16;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			samplerInfo.unnormalizedCoordinates = false;
			VkResult res = vkCreateSampler(m_pDevice->GetDevice(), &samplerInfo, nullptr, &m_linearSampler);
			assert(VK_SUCCESS == res);
		}

		// Previous depth sampler 
		{
			VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			samplerInfo.pNext = nullptr;
			samplerInfo.flags = 0;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.mipLodBias = 0;
			samplerInfo.anisotropyEnable = false;
			samplerInfo.maxAnisotropy = 0;
			samplerInfo.compareEnable = false;
			samplerInfo.compareOp = VK_COMPARE_OP_LESS; // TODO: Does this achieve the same as D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR?
			samplerInfo.minLod = 0;
			samplerInfo.maxLod = 16;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			samplerInfo.unnormalizedCoordinates = false;
			VkResult res = vkCreateSampler(m_pDevice->GetDevice(), &samplerInfo, nullptr, &m_previousDepthSampler);
			assert(VK_SUCCESS == res);
		}

		//==============================Create Blue Noise buffers============================================
		{
			auto const& samplerState = g_blueNoiseSamplerState;
			BlueNoiseSamplerVK& sampler = m_blueNoiseSampler;

			BufferVK::CreateInfo createInfo = {};
			createInfo.memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			createInfo.bufferUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			createInfo.format = VK_FORMAT_R32_UINT;

			createInfo.sizeInBytes = sizeof(samplerState.m_sobolBuffer);
			m_blueNoiseSampler.sobolBuffer = BufferVK(device, physicalDevice, createInfo, "SSSR - Sobol Buffer");

			createInfo.sizeInBytes = sizeof(samplerState.m_rankingTileBuffer);
			m_blueNoiseSampler.rankingTileBuffer = BufferVK(device, physicalDevice, createInfo, "SSSR - Ranking Tile Buffer");

			createInfo.sizeInBytes = sizeof(samplerState.scramblingTileBuffer);
			m_blueNoiseSampler.scramblingTileBuffer = BufferVK(device, physicalDevice, createInfo, "SSSR - Scrambling Tile Buffer");

			VkBufferCopy copyInfo;
			copyInfo.dstOffset = 0;
			copyInfo.size = sizeof(samplerState.m_sobolBuffer);
			copyInfo.srcOffset = 0;

			uint8_t* destAddr;

			destAddr = m_uploadHeap.BeginSuballocate(sizeof(samplerState.m_sobolBuffer), 512);
			memcpy(destAddr, &samplerState.m_sobolBuffer, sizeof(samplerState.m_sobolBuffer));
			m_uploadHeap.EndSuballocate();
			m_uploadHeap.AddCopy(m_blueNoiseSampler.sobolBuffer.m_buffer, copyInfo);
			m_uploadHeap.FlushAndFinish();

			copyInfo.size = sizeof(samplerState.m_rankingTileBuffer);
			copyInfo.srcOffset = 0;
			destAddr = m_uploadHeap.BeginSuballocate(sizeof(samplerState.m_rankingTileBuffer), 512);
			memcpy(destAddr, &samplerState.m_rankingTileBuffer, sizeof(samplerState.m_rankingTileBuffer));
			m_uploadHeap.EndSuballocate();
			m_uploadHeap.AddCopy(m_blueNoiseSampler.rankingTileBuffer.m_buffer, copyInfo);
			m_uploadHeap.FlushAndFinish();

			copyInfo.size = sizeof(samplerState.scramblingTileBuffer);
			destAddr = m_uploadHeap.BeginSuballocate(sizeof(samplerState.scramblingTileBuffer), 512);
			memcpy(destAddr, &samplerState.scramblingTileBuffer, sizeof(samplerState.scramblingTileBuffer));
			m_uploadHeap.EndSuballocate();
			m_uploadHeap.AddCopy(m_blueNoiseSampler.scramblingTileBuffer.m_buffer, copyInfo);
			m_uploadHeap.FlushAndFinish();

			ImageVK::CreateInfo blueNoiseCreateInfo = {};
			blueNoiseCreateInfo.format = VK_FORMAT_R8G8_UNORM;
			blueNoiseCreateInfo.width = 128;
			blueNoiseCreateInfo.height = 128;
			m_blueNoiseTexture = ImageVK(m_pDevice, blueNoiseCreateInfo, "Reflection Denoiser - Blue Noise Texture");
		}
	}

	void SSSR::CreateWindowSizeDependentResources(VkCommandBuffer commandBuffer)
	{
		VkDevice device = m_pDevice->GetDevice();
		VkPhysicalDevice physicalDevice = m_pDevice->GetPhysicalDevice();

		//==============================Create Tile Classification-related buffers============================================
		{
			uint32_t numTiles = DivideRoundingUp(m_outputWidth, 8u) * DivideRoundingUp(m_outputHeight, 8u);
			uint32_t numPixels = m_outputWidth * m_outputHeight;

			uint32_t rayListElementCount = numPixels;
			uint32_t rayCounterElementCount = 1;

			BufferVK::CreateInfo createInfo = {};
			createInfo.memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			createInfo.format = VK_FORMAT_R32_UINT;
			createInfo.bufferUsage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

			createInfo.sizeInBytes = sizeof(uint32_t) * rayListElementCount;
			m_rayList = BufferVK(device, physicalDevice, createInfo, "SSSR - Ray List");
		}
		{
			uint32_t numTiles = DivideRoundingUp(m_outputWidth, 8u) * DivideRoundingUp(m_outputHeight, 8u);
			uint32_t numPixels = m_outputWidth * m_outputHeight;

			uint32_t denoiserTileListElementCount = numPixels;
			uint32_t rayCounterElementCount = 1;

			BufferVK::CreateInfo createInfo = {};
			createInfo.memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			createInfo.format = VK_FORMAT_R32_UINT;
			createInfo.bufferUsage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

			createInfo.sizeInBytes = sizeof(uint32_t) * denoiserTileListElementCount;
			m_denoiserTileList = BufferVK(device, physicalDevice, createInfo, "SSSR - Denoiser Tile List");
		}

		//==============================Create denoising-related resources==============================
		{
			ImageVK::CreateInfo radianceCreateInfo = {};
			radianceCreateInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
			radianceCreateInfo.width = m_outputWidth;
			radianceCreateInfo.height = m_outputHeight;
			m_radiance[0] = ImageVK(m_pDevice, radianceCreateInfo, "Reflection Denoiser - Radiance 0");
			m_radiance[1] = ImageVK(m_pDevice, radianceCreateInfo, "Reflection Denoiser - Radiance 1");
			m_reprojectedRadiance = ImageVK(m_pDevice, radianceCreateInfo, "Reflection Denoiser - Reprojected Radiance");
			
			ImageVK::CreateInfo averageRadianceCreateInfo = {};
			averageRadianceCreateInfo.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			averageRadianceCreateInfo.width = DivideRoundingUp(m_outputWidth, 8u);
			averageRadianceCreateInfo.height = DivideRoundingUp(m_outputHeight, 8u);
			m_averageRadiance[0] = ImageVK(m_pDevice, averageRadianceCreateInfo, "Reflection Denoiser - Average Radiance 0");
			m_averageRadiance[1] = ImageVK(m_pDevice, averageRadianceCreateInfo, "Reflection Denoiser - Average Radiance 1");

			ImageVK::CreateInfo varianceCreateInfo = {};
			varianceCreateInfo.format = VK_FORMAT_R16_SFLOAT;
			varianceCreateInfo.width = m_outputWidth;
			varianceCreateInfo.height = m_outputHeight;
			m_variance[0] = ImageVK(m_pDevice, varianceCreateInfo, "Reflection Denoiser - Variance 0");
			m_variance[1] = ImageVK(m_pDevice, varianceCreateInfo, "Reflection Denoiser - Variance 1");
			
			ImageVK::CreateInfo sampleCountCreateInfo = varianceCreateInfo;
			m_sampleCount[0] = ImageVK(m_pDevice, sampleCountCreateInfo, "Reflection Denoiser - Sample Count 0");
			m_sampleCount[1] = ImageVK(m_pDevice, sampleCountCreateInfo, "Reflection Denoiser - Sample Count 1");

			ImageVK::CreateInfo imgCreateInfo = {};
			imgCreateInfo.width = m_outputWidth;
			imgCreateInfo.height = m_outputHeight;

			imgCreateInfo.format = VK_FORMAT_R8_UNORM;
			m_roughnessTexture = ImageVK(m_pDevice, imgCreateInfo, "Reflection Denoiser - Extracted Roughness");
			m_roughnessHistoryTexture = ImageVK(m_pDevice, imgCreateInfo, "Reflection Denoiser - Extracted Roughness History");

			imgCreateInfo.format = VK_FORMAT_R32_SFLOAT;
			m_depthHistoryTexture = ImageVK(m_pDevice, imgCreateInfo, "Reflection Denoiser - Depth History");

			imgCreateInfo.format = m_normalTexture->GetFormat();
			m_normalHistoryTexture = ImageVK(m_pDevice, imgCreateInfo, "Reflection Denoiser - Normal History");
		}

		// Initial transitions for clearing
		{
			VkImageMemoryBarrier imageBarriers[] = {
				m_radiance[0].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_radiance[1].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_reprojectedRadiance.Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_averageRadiance[0].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_averageRadiance[1].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_variance[0].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_variance[1].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_sampleCount[0].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_sampleCount[1].Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_roughnessTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_roughnessHistoryTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_depthHistoryTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_normalHistoryTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
				m_blueNoiseTexture.Transition(VK_IMAGE_LAYOUT_GENERAL),
			};
			TransitionBarriers(commandBuffer, imageBarriers, _countof(imageBarriers));
		}

		// Initial clear of the ray counter. Successive clears are handled by the indirect arguments pass. 
		vkCmdFillBuffer(commandBuffer, m_rayCounter.m_buffer, 0, VK_WHOLE_SIZE, 0);

		VkClearColorValue clearValue = {};
		clearValue.float32[0] = 0;
		clearValue.float32[1] = 0;
		clearValue.float32[2] = 0;
		clearValue.float32[3] = 0;

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;

		// Initial resource clears
		vkCmdClearColorImage(commandBuffer, m_radiance[0].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_radiance[1].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_reprojectedRadiance.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_averageRadiance[0].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_averageRadiance[1].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_variance[0].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_variance[1].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_sampleCount[0].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_sampleCount[1].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_roughnessTexture.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_roughnessHistoryTexture.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_depthHistoryTexture.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_normalHistoryTexture.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
		vkCmdClearColorImage(commandBuffer, m_blueNoiseTexture.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
	}

	void SSSR::SetupShaderPass(ShaderPass& pass, const char* shader, const VkDescriptorSetLayoutBinding* bindings, uint32_t bindingsCount, VkPipelineShaderStageCreateFlags flags)
	{
		VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		pass.bindingsCount = bindingsCount;

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			VkResult vkResult = VKCompileFromFile(m_pDevice->GetDevice(), VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", "-enable-16bit-types -T cs_6_2", &defines, &stageCreateInfo);
			stageCreateInfo.flags = flags;
			assert(vkResult == VK_SUCCESS);
		}

		//==============================DescriptorSetLayout========================================
		{
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			descriptorSetLayoutCreateInfo.pNext = nullptr;
			descriptorSetLayoutCreateInfo.flags = 0;
			descriptorSetLayoutCreateInfo.bindingCount = bindingsCount;
			descriptorSetLayoutCreateInfo.pBindings = bindings;
			VkResult vkResult = vkCreateDescriptorSetLayout(m_pDevice->GetDevice(), &descriptorSetLayoutCreateInfo, nullptr, &pass.descriptorSetLayout);
			assert(vkResult == VK_SUCCESS);

			for (int i = 0; i < 2; ++i)
			{
				pass.descriptorSets.emplace_back();
				bool bDescriptorAlloc = m_pResourceViewHeaps->AllocDescriptor(pass.descriptorSetLayout, &pass.descriptorSets.back());
				assert(bDescriptorAlloc == true);
			}
		}

		//==============================PipelineLayout========================================
		{
			VkDescriptorSetLayout layouts[2];
			layouts[0] = m_uniformBufferDescriptorSetLayout;
			layouts[1] = pass.descriptorSetLayout;

			VkPipelineLayoutCreateInfo layoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			layoutCreateInfo.pNext = nullptr;
			layoutCreateInfo.flags = 0;
			layoutCreateInfo.setLayoutCount = _countof(layouts);
			layoutCreateInfo.pSetLayouts = layouts;
			layoutCreateInfo.pushConstantRangeCount = 0;
			layoutCreateInfo.pPushConstantRanges = nullptr;
			VkResult bCreatePipelineLayout = vkCreatePipelineLayout(m_pDevice->GetDevice(), &layoutCreateInfo, nullptr, &pass.pipelineLayout);
			assert(bCreatePipelineLayout == VK_SUCCESS);
		}

		//==============================Pipeline========================================
		{
			VkComputePipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
			createInfo.pNext = nullptr;
			createInfo.basePipelineHandle = VK_NULL_HANDLE;
			createInfo.basePipelineIndex = 0;
			createInfo.flags = 0;
			createInfo.layout = pass.pipelineLayout;
			createInfo.stage = stageCreateInfo;
			VkResult vkResult = vkCreateComputePipelines(m_pDevice->GetDevice(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &pass.pipeline);
			assert(vkResult == VK_SUCCESS);
		}
	}

	void SSSR::SetupClassifyTilesPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_variance_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_environment_map
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_environment_map_sampler
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_list
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_intersection_output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_extracted_roughness

			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_denoiser_tile_list
		};

		SetupShaderPass(m_classifyTilesPass, "ClassifyTiles.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::SetupBlueNoisePass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_sobol_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_ranking_tile_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_scrambling_tile_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_blue_noise_texture
		};

		SetupShaderPass(m_blueNoisePass, "PrepareBlueNoiseTexture.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::SetupPrepareIndirectArgsPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_intersect_args
		};
		SetupShaderPass(m_prepareIndirectArgsPass, "PrepareIndirectArgs.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::SetupIntersectionPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_lit_scene
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer_hierarchy
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_environment_map
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_blue_noise_texture
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_ray_list

			//Samplers
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_environment_map_sampler

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_intersection_result
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
		};
		SetupShaderPass(m_intersectPass, "Intersect.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::SetupResolveTemporalPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_average_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_reprojected_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_variance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_sample_count
			
			//Samplers
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_linear_sampler

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_variance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_sample_count

			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_denoiser_tile_list
		};
		SetupShaderPass(m_resolveTemporalPass, "ResolveTemporal.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::SetupPrefilterPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_average_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_variance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_sample_count

			//Samplers
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_linear_sampler

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_variance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_sample_count

			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_denoiser_tile_list
		};

		SetupShaderPass(m_prefilterPass, "Prefilter.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::SetupReprojectPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layoutBindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_in_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_radiance_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_motion_vector
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_average_radiance_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_variance_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_sample_count_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_blue_noise_texture

			//Samplers
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_linear_sampler

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_reprojected_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_average_radiance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_variance
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_out_sample_count

			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_denoiser_tile_list
		};
		SetupShaderPass(m_reprojectPass, "Reproject.hlsl", layoutBindings, _countof(layoutBindings));
	}

	void SSSR::ComputeBarrier(VkCommandBuffer commandBuffer) const
	{
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			1, &barrier,
			0, nullptr,
			0, nullptr);
	}

	void SSSR::IndirectArgumentsBarrier(VkCommandBuffer commandBuffer) const
	{
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			0,
			1, &barrier,
			0, nullptr,
			0, nullptr);
	}

	void SSSR::TransitionBarriers(VkCommandBuffer commandBuffer, const VkImageMemoryBarrier* imageBarriers, uint32_t imageBarrierCount) const
	{
		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			imageBarrierCount, imageBarriers);
	}

	VkImageMemoryBarrier SSSR::Transition(VkImage image, VkImageLayout before, VkImageLayout after) const
	{
		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = before;
		barrier.newLayout = after;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;

		barrier.subresourceRange = subresourceRange;
		return barrier;
	}

	void SSSR::InitializeResourceDescriptorSets(const SSSRCreationInfo& input)
	{
		VkDevice device = m_pDevice->GetDevice();

		uint32_t binding = 0;
		VkDescriptorSet targetSet = VK_NULL_HANDLE;

		// Place the descriptors
		for (int i = 0; i < 2; ++i)
		{
			// Tile Classifier pass
			{
				targetSet = m_classifyTilesPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.SpecularRoughnessView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.DepthHierarchyView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_variance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				SetDescriptorSet(device, binding++, input.NormalBufferView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.EnvironmentMapView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSetSampler(device, binding++, input.EnvironmentMapSampler, targetSet); // g_environment_map_sampler

				SetDescriptorSetBuffer(device, binding++, m_rayList.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_rayCounter.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
				SetDescriptorSet(device, binding++, m_radiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_roughnessTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetBuffer(device, binding++, m_denoiserTileList.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Blue Noise pass
			{
				targetSet = m_blueNoisePass.descriptorSets[i];
				binding = 0;

				SetDescriptorSetBuffer(device, binding++, m_blueNoiseSampler.sobolBuffer.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_blueNoiseSampler.rankingTileBuffer.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_blueNoiseSampler.scramblingTileBuffer.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

				SetDescriptorSet(device, binding++, m_blueNoiseTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
			}

			// Indirect args pass
			{
				targetSet = m_prepareIndirectArgsPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSetBuffer(device, binding++, m_rayCounter.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_intersectionPassIndirectArgs.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Intersection pass
			{
				targetSet = m_intersectPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.HDRView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.DepthHierarchyView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.NormalBufferView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.EnvironmentMapView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				SetDescriptorSet(device, binding++, m_blueNoiseTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSetBuffer(device, binding++, m_rayList.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

				SetDescriptorSetSampler(device, binding++, input.EnvironmentMapSampler, targetSet); // g_environment_map_sampler

				SetDescriptorSet(device, binding++, m_radiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetBuffer(device, binding++, m_rayCounter.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Reproject pass
			{
				targetSet = m_reprojectPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.DepthHierarchyView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.NormalBufferView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_depthHistoryTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessHistoryTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_normalHistoryTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				SetDescriptorSet(device, binding++, m_radiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_radiance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.MotionVectorsView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				SetDescriptorSet(device, binding++, m_averageRadiance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_variance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_sampleCount[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_blueNoiseTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				SetDescriptorSetSampler(device, binding++, m_linearSampler, targetSet);

				SetDescriptorSet(device, binding++, m_reprojectedRadiance.View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_averageRadiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_variance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_sampleCount[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetBuffer(device, binding++, m_denoiserTileList.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Prefilter pass
			{
				targetSet = m_prefilterPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.DepthHierarchyView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.NormalBufferView, targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_averageRadiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_radiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_variance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_sampleCount[i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				
				SetDescriptorSetSampler(device, binding++, m_linearSampler, targetSet);

				SetDescriptorSet(device, binding++, m_radiance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_variance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_sampleCount[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetBuffer(device, binding++, m_denoiserTileList.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Temporal denoising pass
			{
				targetSet = m_resolveTemporalPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, m_roughnessTexture.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_averageRadiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_radiance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_reprojectedRadiance.View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_variance[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_sampleCount[1 - i].View(), targetSet, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				
				SetDescriptorSetSampler(device, binding++, m_linearSampler, targetSet);

				SetDescriptorSet(device, binding++, m_radiance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_variance[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_sampleCount[i].View(), targetSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetBuffer(device, binding++, m_denoiserTileList.m_bufferView, targetSet, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}
		}
	}
}