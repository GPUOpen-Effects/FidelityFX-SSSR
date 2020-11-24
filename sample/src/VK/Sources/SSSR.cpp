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
	std::int32_t const (&sobol_buffer_)[256 * 256];
	std::int32_t const (&ranking_tile_buffer_)[128 * 128 * 8];
	std::int32_t const (&scrambling_tile_buffer_)[128 * 128 * 8];
}
const g_blue_noise_sampler_state = { _1spp::sobol_256spp_256d,  _1spp::rankingTile,  _1spp::scramblingTile };

/**
	Performs a rounded division.

	\param value The value to be divided.
	\param divisor The divisor to be used.
	\return The rounded divided value.
*/
template<typename TYPE>
static inline TYPE RoundedDivide(TYPE value, TYPE divisor)
{
	return (value + divisor - 1) / divisor;
}

VkDescriptorSetLayoutBinding Bind(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding layout_binding = {};
	layout_binding.binding = binding;
	layout_binding.descriptorType = type;
	layout_binding.descriptorCount = 1;
	layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	layout_binding.pImmutableSamplers = nullptr;
	return layout_binding;
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

void CopyToTexture(VkCommandBuffer cb, CAULDRON_VK::Texture* source, CAULDRON_VK::Texture* target, uint32_t width, uint32_t height)
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
	vkCmdCopyImage(cb, source->Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->Resource(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

using namespace CAULDRON_VK;
namespace SSSR_SAMPLE_VK
{
	void SSSR::OnCreate(Device* pDevice, VkCommandBuffer command_buffer, ResourceViewHeaps* resourceHeap, DynamicBufferRing* constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters)
	{
		m_pDevice = pDevice;
		m_pConstantBufferRing = constantBufferRing;
		m_pResourceViewHeaps = resourceHeap;
		m_frameCountBeforeReuse = frameCountBeforeReuse;
		m_isPerformanceCountersEnabled = enablePerformanceCounters;

		VkPhysicalDevice physicalDevice = m_pDevice->GetPhysicalDevice();
		VkDevice device = m_pDevice->GetDevice();

		// Query if the implementation supports VK_EXT_subgroup_size_control
		// This is the case if VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME is present.
		// Rely on the application to enable the extension if it's available.
		VkResult vkResult;
		uint32_t extension_count;
		vkResult = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, NULL);
		assert(VK_SUCCESS == vkResult);
		std::vector<VkExtensionProperties> device_extension_properties(extension_count);
		vkResult = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, device_extension_properties.data());
		assert(VK_SUCCESS == vkResult);
		m_isSubgroupSizeControlExtensionAvailable = std::find_if(device_extension_properties.begin(), device_extension_properties.end(),
			[](const VkExtensionProperties& extensionProps) -> bool { return strcmp(extensionProps.extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) == 0; })
			!= device_extension_properties.end();

		m_uploadHeap.OnCreate(m_pDevice, 1024 * 1024);

		VkDescriptorSetLayoutBinding layout_binding = {};
		layout_binding.binding = 0;
		layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layout_binding.descriptorCount = 1;
		layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layout_binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		descriptor_set_layout_create_info.pNext = nullptr;
		descriptor_set_layout_create_info.flags = 0;
		descriptor_set_layout_create_info.bindingCount = 1;
		descriptor_set_layout_create_info.pBindings = &layout_binding;

		vkResult = vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &m_uniformBufferDescriptorSetLayout);
		assert(vkResult == VK_SUCCESS);

		for (uint32_t i = 0; i < frameCountBeforeReuse; ++i)
		{
			bool bAllocDescriptor = m_pResourceViewHeaps->AllocDescriptor(m_uniformBufferDescriptorSetLayout, &m_uniformBufferDescriptorSet[i]);
			assert(bAllocDescriptor == true);
		}

		CreateResources(command_buffer);
		SetupClassifyTilesPass();
		SetupPrepareIndirectArgsPass();
		SetupIntersectionPass();
		SetupResolveSpatial();
		SetupResolveTemporal();
		SetupBlurPass();

		SetupPerformanceCounters();
	}

	void SSSR::OnCreateWindowSizeDependentResources(VkCommandBuffer command_buffer, const SSSRCreationInfo& input)
	{
		m_outputWidth = input.outputWidth;
		m_outputHeight = input.outputHeight;

		assert(input.outputWidth != 0);
		assert(input.outputHeight != 0);
		assert(input.DepthHierarchyView != VK_NULL_HANDLE);
		assert(input.EnvironmentMapSampler != VK_NULL_HANDLE);
		assert(input.EnvironmentMapView != VK_NULL_HANDLE);
		assert(input.HDRView != VK_NULL_HANDLE);
		assert(input.MotionVectorsView != VK_NULL_HANDLE);
		assert(input.NormalBufferView != VK_NULL_HANDLE);
		assert(input.NormalHistoryBufferView != VK_NULL_HANDLE);
		assert(input.SpecularRoughnessView != VK_NULL_HANDLE);

		CreateWindowSizeDependentResources(command_buffer);
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
		m_prepareIndirectArgsPass.OnDestroy(device, m_pResourceViewHeaps);
		m_intersectPass.OnDestroy(device, m_pResourceViewHeaps);
		m_resolveSpatialPass.OnDestroy(device, m_pResourceViewHeaps);
		m_resolveTemporalPass.OnDestroy(device, m_pResourceViewHeaps);
		m_blurPass.OnDestroy(device, m_pResourceViewHeaps);
		m_uploadHeap.OnDestroy();

		m_rayCounter.OnDestroy();
		m_intersectionPassIndirectArgs.OnDestroy();

		vkDestroySampler(device, m_linearSampler, nullptr);
		m_blueNoiseSampler.OnDestroy();

		if (m_timestampQueryPool)
		{
			vkDestroyQueryPool(m_pDevice->GetDevice(), m_timestampQueryPool, nullptr);
		}

	}
	void SSSR::OnDestroyWindowSizeDependentResources()
	{
		VkDevice device = m_pDevice->GetDevice();

		vkDestroyImageView(device, m_temporalDenoiserResultView[0], nullptr);
		vkDestroyImageView(device, m_temporalDenoiserResultView[1], nullptr);
		vkDestroyImageView(device, m_rayLengthsView, nullptr);
		vkDestroyImageView(device, m_outputBufferView, nullptr);
		vkDestroyImageView(device, m_roughnessTextureView[0], nullptr);
		vkDestroyImageView(device, m_roughnessTextureView[1], nullptr);

		m_temporalDenoiserResult[0].OnDestroy();
		m_temporalDenoiserResult[1].OnDestroy();
		m_rayLengths.OnDestroy();
		m_outputBuffer.OnDestroy();
		m_roughnessTexture[0].OnDestroy();
		m_roughnessTexture[1].OnDestroy();
		m_temporalVarianceMask.OnDestroy();
		m_tileMetaDataMask.OnDestroy();

		m_rayList.OnDestroy();
	}
	void SSSR::Draw(VkCommandBuffer command_buffer, const SSSRConstants& sssrConstants, bool showIntersectResult)
	{
		SetPerfMarkerBegin(command_buffer, "FidelityFX SSSR");

		QueryTimestamps(command_buffer);

		// Ensure the image is cleared
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			1, &barrier,
			0, nullptr,
			0, nullptr);

		uint32_t uniform_buffer_index = sssrConstants.frameIndex % m_frameCountBeforeReuse;
		VkDescriptorSet uniformBufferDescriptorSet = m_uniformBufferDescriptorSet[uniform_buffer_index];

		// Update descriptor to sliding window in upload buffer that contains the updated pass data
		{
			VkDescriptorBufferInfo uniformBufferInfo = m_pConstantBufferRing->AllocConstantBuffer(sizeof(SSSRConstants), (void*)&sssrConstants);

			VkWriteDescriptorSet write_set = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write_set.pNext = nullptr;
			write_set.dstSet = uniformBufferDescriptorSet;
			write_set.dstBinding = 0;
			write_set.dstArrayElement = 0;
			write_set.descriptorCount = 1;
			write_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			write_set.pImageInfo = nullptr;
			write_set.pBufferInfo = &uniformBufferInfo;
			write_set.pTexelBufferView = nullptr;
			vkUpdateDescriptorSets(m_pDevice->GetDevice(), 1, &write_set, 0, nullptr);
		}

		// ClassifyTiles pass
		{
			VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_classifyTilesPass.descriptorSets[m_bufferIndex] };
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_classifyTilesPass.pipeline);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_classifyTilesPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
			uint32_t dim_x = RoundedDivide(m_outputWidth, 8u);
			uint32_t dim_y = RoundedDivide(m_outputHeight, 8u);
			vkCmdDispatch(command_buffer, dim_x, dim_y, 1);
		}

		ComputeBarrier(command_buffer);

		// PrepareIndirectArgs pass
		{
			VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_prepareIndirectArgsPass.descriptorSets[m_bufferIndex] };
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prepareIndirectArgsPass.pipeline);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prepareIndirectArgsPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
			vkCmdDispatch(command_buffer, 1, 1, 1);
		}

		// Query the amount of time spent in the tile classification pass
		if (m_isPerformanceCountersEnabled)
		{
			auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];
			assert(timestamp_queries.size() == 1ull && timestamp_queries[0] == TIMESTAMP_QUERY_INIT);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, GetTimestampQueryIndex());
			timestamp_queries.push_back(TIMESTAMP_QUERY_TILE_CLASSIFICATION);
		}

		// Ensure that the arguments are written
		IndirectArgumentsBarrier(command_buffer);

		// Intersection pass
		{
			VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_intersectPass.descriptorSets[m_bufferIndex] };
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_intersectPass.pipeline);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_intersectPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
			vkCmdDispatchIndirect(command_buffer, m_intersectionPassIndirectArgs.buffer_, 0);
		}

		// Query the amount of time spent in the intersection pass
		if (m_isPerformanceCountersEnabled)
		{
			auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];
			assert(timestamp_queries.size() == 2ull && timestamp_queries[1] == TIMESTAMP_QUERY_TILE_CLASSIFICATION);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, GetTimestampQueryIndex());
			timestamp_queries.push_back(TIMESTAMP_QUERY_INTERSECTION);
		}

		if (!showIntersectResult)
		{
			uint32_t num_tilesX = RoundedDivide(m_outputWidth, 8u);
			uint32_t num_tilesY = RoundedDivide(m_outputHeight, 8u);

			// Ensure that the intersection pass finished
			VkImageMemoryBarrier intersection_finished_barriers[] = {
				Transition(m_temporalDenoiserResult[m_bufferIndex].Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			};
			TransitionBarriers(command_buffer, intersection_finished_barriers, _countof(intersection_finished_barriers));

			// Spatial denoiser pass
			{
				VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_resolveSpatialPass.descriptorSets[m_bufferIndex] };
				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveSpatialPass.pipeline);
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveSpatialPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
				vkCmdDispatch(command_buffer, num_tilesX, num_tilesY, 1);
			}

			VkImageMemoryBarrier spatial_denoiser_finished_barriers[] = {
				Transition(m_temporalDenoiserResult[m_bufferIndex].Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
				Transition(m_temporalDenoiserResult[1 - m_bufferIndex].Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				Transition(m_rayLengths.Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			};
			TransitionBarriers(command_buffer, spatial_denoiser_finished_barriers, _countof(spatial_denoiser_finished_barriers));

			// Temporal denoiser pass
			{
				VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_resolveTemporalPass.descriptorSets[m_bufferIndex] };
				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveTemporalPass.pipeline);
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_resolveTemporalPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
				vkCmdDispatch(command_buffer, num_tilesX, num_tilesY, 1);
			}

			// Ensure that the temporal denoising pass finished
			VkImageMemoryBarrier temporal_denoiser_finished_barriers[] = {
				Transition(m_rayLengths.Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
				Transition(m_temporalDenoiserResult[1 - m_bufferIndex].Resource(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
			};
			TransitionBarriers(command_buffer, temporal_denoiser_finished_barriers, _countof(temporal_denoiser_finished_barriers));

			// Blur pass
			{
				VkDescriptorSet sets[] = { uniformBufferDescriptorSet,  m_blurPass.descriptorSets[m_bufferIndex] };
				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_blurPass.pipeline);
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_blurPass.pipelineLayout, 0, _countof(sets), sets, 0, nullptr);
				vkCmdDispatch(command_buffer, num_tilesX, num_tilesY, 1);
			}

			// Query the amount of time spent in the denoiser passes
			if (m_isPerformanceCountersEnabled)
			{
				auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

				assert(timestamp_queries.size() == 3ull && timestamp_queries[2] == TIMESTAMP_QUERY_INTERSECTION);

				vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, GetTimestampQueryIndex());
				timestamp_queries.push_back(TIMESTAMP_QUERY_DENOISING);
			}
		}
		else
		{
			VkImageMemoryBarrier copy_barrier_begin[] = {
				Transition(m_temporalDenoiserResult[m_bufferIndex].Resource(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
				Transition(m_outputBuffer.Resource(),							VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			};
			TransitionBarriers(command_buffer, copy_barrier_begin, _countof(copy_barrier_begin));

			CopyToTexture(command_buffer, &m_temporalDenoiserResult[m_bufferIndex], &m_outputBuffer, m_outputWidth, m_outputHeight);

			VkImageMemoryBarrier copy_barrier_end[] = {
				Transition(m_temporalDenoiserResult[m_bufferIndex].Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
				Transition(m_outputBuffer.Resource(),							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
			};
			TransitionBarriers(command_buffer, copy_barrier_end, _countof(copy_barrier_end));
		}

		// Move timestamp queries to next frame
		if (m_isPerformanceCountersEnabled)
		{
			m_timestampFrameIndex = (m_timestampFrameIndex + 1u) % m_frameCountBeforeReuse;
		}

		m_bufferIndex = 1 - m_bufferIndex;
		SetPerfMarkerEnd(command_buffer);
	}

	void SSSR::GUI(int* pSlice)
	{

	}

	Texture* SSSR::GetOutputTexture()
	{
		return &m_outputBuffer;
	}

	VkImageView SSSR::GetOutputTextureView() const
	{
		return m_outputBufferView;
	}

	std::uint64_t SSSR::GetTileClassificationElapsedGpuTicks() const
	{
		return m_tileClassificationElapsedGpuTicks;
	}

	std::uint64_t SSSR::GetIntersectElapsedGpuTicks() const
	{
		return m_intersectionElapsedGpuTicks;
	}

	std::uint64_t SSSR::GetDenoiserElapsedGpuTicks() const
	{
		return m_denoisingElapsedGpuTicks;
	}

	void SSSR::CreateResources(VkCommandBuffer command_buffer)
	{
		VkDevice device = m_pDevice->GetDevice();
		VkPhysicalDevice physicalDevice = m_pDevice->GetPhysicalDevice();

		//==============================Create Tile Classification-related buffers============================================
		{
			uint32_t ray_counter_element_count = 2;

			BufferVK::CreateInfo create_info = {};
			create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			create_info.format_ = VK_FORMAT_R32_UINT;
			create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			create_info.size_in_bytes_ = ray_counter_element_count * sizeof(uint32_t);
			create_info.name_ = "SSSR - Ray Counter";
			m_rayCounter = BufferVK(device, physicalDevice, create_info);
		}

		//==============================Create PrepareIndirectArgs-related buffers============================================
		{
			uint32_t intersection_pass_indirect_args_element_count = 3;
			uint32_t denoiser_pass_indirect_args_element_count = 3;
			BufferVK::CreateInfo create_info = {};
			create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			create_info.format_ = VK_FORMAT_R32_UINT;
			create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

			create_info.size_in_bytes_ = intersection_pass_indirect_args_element_count * sizeof(uint32_t);
			create_info.name_ = "SSSR - Intersect Indirect Args";
			m_intersectionPassIndirectArgs = BufferVK(device, physicalDevice, create_info);
		}

		{
			VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			sampler_info.pNext = nullptr;
			sampler_info.flags = 0;
			sampler_info.magFilter = VK_FILTER_LINEAR;
			sampler_info.minFilter = VK_FILTER_LINEAR;
			sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sampler_info.mipLodBias = 0;
			sampler_info.anisotropyEnable = false;
			sampler_info.maxAnisotropy = 0;
			sampler_info.compareEnable = false;
			sampler_info.compareOp = VK_COMPARE_OP_NEVER;
			sampler_info.minLod = 0;
			sampler_info.maxLod = 16;
			sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			sampler_info.unnormalizedCoordinates = false;
			VkResult res = vkCreateSampler(m_pDevice->GetDevice(), &sampler_info, nullptr, &m_linearSampler);
			assert(VK_SUCCESS == res);
		}

		//==============================Create Blue Noise buffers============================================
		{
			auto const& sampler_state = g_blue_noise_sampler_state;
			BlueNoiseSamplerVK& sampler = m_blueNoiseSampler;

			BufferVK::CreateInfo create_info = {};
			create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			create_info.buffer_usage_ = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			create_info.format_ = VK_FORMAT_R32_UINT;

			create_info.size_in_bytes_ = sizeof(sampler_state.sobol_buffer_);
			create_info.name_ = "SSSR - Sobol Buffer";
			m_blueNoiseSampler.sobolBuffer = BufferVK(device, physicalDevice, create_info);

			create_info.size_in_bytes_ = sizeof(sampler_state.ranking_tile_buffer_);
			create_info.name_ = "SSSR - Ranking Tile Buffer";
			m_blueNoiseSampler.rankingTileBuffer = BufferVK(device, physicalDevice, create_info);

			create_info.size_in_bytes_ = sizeof(sampler_state.scrambling_tile_buffer_);
			create_info.name_ = "SSSR - Scrambling Tile Buffer";
			m_blueNoiseSampler.scramblingTileBuffer = BufferVK(device, physicalDevice, create_info);

			VkBufferCopy copyInfo;
			copyInfo.dstOffset = 0;
			copyInfo.size = sizeof(sampler_state.sobol_buffer_);
			copyInfo.srcOffset = 0;

			uint8_t* destAddr;

			destAddr = m_uploadHeap.BeginSuballocate(sizeof(sampler_state.sobol_buffer_), 512);
			memcpy(destAddr, &sampler_state.sobol_buffer_, sizeof(sampler_state.sobol_buffer_));
			m_uploadHeap.EndSuballocate();
			m_uploadHeap.AddCopy(m_blueNoiseSampler.sobolBuffer.buffer_, copyInfo);
			m_uploadHeap.FlushAndFinish();

			copyInfo.size = sizeof(sampler_state.ranking_tile_buffer_);
			copyInfo.srcOffset = 0;
			destAddr = m_uploadHeap.BeginSuballocate(sizeof(sampler_state.ranking_tile_buffer_), 512);
			memcpy(destAddr, &sampler_state.ranking_tile_buffer_, sizeof(sampler_state.ranking_tile_buffer_));
			m_uploadHeap.EndSuballocate();
			m_uploadHeap.AddCopy(m_blueNoiseSampler.rankingTileBuffer.buffer_, copyInfo);
			m_uploadHeap.FlushAndFinish();

			copyInfo.size = sizeof(sampler_state.scrambling_tile_buffer_);
			destAddr = m_uploadHeap.BeginSuballocate(sizeof(sampler_state.scrambling_tile_buffer_), 512);
			memcpy(destAddr, &sampler_state.scrambling_tile_buffer_, sizeof(sampler_state.scrambling_tile_buffer_));
			m_uploadHeap.EndSuballocate();
			m_uploadHeap.AddCopy(m_blueNoiseSampler.scramblingTileBuffer.buffer_, copyInfo);
			m_uploadHeap.FlushAndFinish();
		}
	}

	void SSSR::CreateWindowSizeDependentResources(VkCommandBuffer command_buffer)
	{
		VkDevice device = m_pDevice->GetDevice();
		VkPhysicalDevice physicalDevice = m_pDevice->GetPhysicalDevice();

		//===================================Create Output Buffer============================================
		{
			VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			imageCreateInfo.pNext = nullptr;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.extent = { m_outputWidth, m_outputHeight, 1 };
			imageCreateInfo.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
			imageCreateInfo.flags = 0;
			m_outputBuffer.Init(m_pDevice, &imageCreateInfo, "Reflection Denoiser - OutputBuffer");
			m_outputBuffer.CreateSRV(&m_outputBufferView);
		}

		//==============================Create Tile Classification-related buffers============================================
		{
			uint32_t num_tiles = RoundedDivide(m_outputWidth, 8u) * RoundedDivide(m_outputHeight, 8u);
			uint32_t num_pixels = m_outputWidth * m_outputHeight;

			uint32_t ray_list_element_count = num_pixels;
			uint32_t ray_counter_element_count = 1;

			BufferVK::CreateInfo create_info = {};
			create_info.memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			create_info.format_ = VK_FORMAT_R32_UINT;
			create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

			create_info.size_in_bytes_ = sizeof(uint32_t) * ray_list_element_count;
			create_info.name_ = "SSSR - Ray List";
			m_rayList = BufferVK(device, physicalDevice, create_info);

			// one uint per tile
			create_info.buffer_usage_ = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			create_info.format_ = VK_FORMAT_UNDEFINED;
			create_info.size_in_bytes_ = sizeof(uint32_t) * num_tiles;
			create_info.name_ = "Reflection Denoiser - Tile Meta Data Mask";
			m_tileMetaDataMask = BufferVK(device, physicalDevice, create_info);

			create_info.size_in_bytes_ = sizeof(uint32_t) * num_tiles * 2u;
			create_info.name_ = "Reflection Denoiser - Temporal Variance Mask";
			m_temporalVarianceMask = BufferVK(device, physicalDevice, create_info);
		}

		//==============================Create denoising-related resources==============================
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
			imgCreateInfo.extent = { m_outputWidth, m_outputHeight, 1 };
			imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imgCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			imgCreateInfo.format = VK_FORMAT_R8_UNORM;
			m_roughnessTexture[0].Init(m_pDevice, &imgCreateInfo, "Reflection Denoiser - Extracted Roughness Texture 0");
			m_roughnessTexture[0].CreateSRV(&m_roughnessTextureView[0]);
			m_roughnessTexture[1].Init(m_pDevice, &imgCreateInfo, "Reflection Denoiser - Extracted Roughness Texture 1");
			m_roughnessTexture[1].CreateSRV(&m_roughnessTextureView[1]);

			imgCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imgCreateInfo.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			m_temporalDenoiserResult[0].Init(m_pDevice, &imgCreateInfo, "Reflection Denoiser - Temporal Denoised Result 0");
			m_temporalDenoiserResult[0].CreateSRV(&m_temporalDenoiserResultView[0]);
			m_temporalDenoiserResult[1].Init(m_pDevice, &imgCreateInfo, "Reflection Denoiser - Temporal Denoised Result 1");
			m_temporalDenoiserResult[1].CreateSRV(&m_temporalDenoiserResultView[1]);

			imgCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			imgCreateInfo.format = VK_FORMAT_R16_SFLOAT;
			m_rayLengths.Init(m_pDevice, &imgCreateInfo, "Reflection Denoiser - Ray Lengths");
			m_rayLengths.CreateSRV(&m_rayLengthsView);
		}

		VkImageMemoryBarrier image_barriers[] = {
			Transition(m_roughnessTexture[0].Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
			Transition(m_roughnessTexture[1].Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
			Transition(m_temporalDenoiserResult[0].Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
			Transition(m_temporalDenoiserResult[1].Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
			Transition(m_rayLengths.Resource(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
		};
		TransitionBarriers(command_buffer, image_barriers, _countof(image_barriers));

		// Initial clear of the ray counter. Successive clears are handled by the indirect arguments pass. 
		vkCmdFillBuffer(command_buffer, m_rayCounter.buffer_, 0, VK_WHOLE_SIZE, 0);

		VkClearColorValue clear_calue = {};
		clear_calue.float32[0] = 0;
		clear_calue.float32[1] = 0;
		clear_calue.float32[2] = 0;
		clear_calue.float32[3] = 0;

		VkImageSubresourceRange subresource_range = {};
		subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresource_range.baseArrayLayer = 0;
		subresource_range.baseMipLevel = 0;
		subresource_range.layerCount = 1;
		subresource_range.levelCount = 1;

		// Initial resource clears
		vkCmdClearColorImage(command_buffer, m_temporalDenoiserResult[0].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
		vkCmdClearColorImage(command_buffer, m_temporalDenoiserResult[1].Resource(), VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
		vkCmdClearColorImage(command_buffer, m_rayLengths.Resource(), VK_IMAGE_LAYOUT_GENERAL, &clear_calue, 1, &subresource_range);
	}

	void SSSR::SetupShaderPass(ShaderPass& pass, const char* shader, const VkDescriptorSetLayoutBinding* bindings, uint32_t bindings_count, VkPipelineShaderStageCreateFlags flags)
	{
		VkPipelineShaderStageCreateInfo stage_create_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		pass.bindings_count = bindings_count;

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			VkResult vkResult = VKCompileFromFile(m_pDevice->GetDevice(), VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", "-T cs_6_0", &defines, &stage_create_info);
			stage_create_info.flags = flags;
			assert(vkResult == VK_SUCCESS);
		}

		//==============================DescriptorSetLayout========================================
		{
			VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			descriptor_set_layout_create_info.pNext = nullptr;
			descriptor_set_layout_create_info.flags = 0;
			descriptor_set_layout_create_info.bindingCount = bindings_count;
			descriptor_set_layout_create_info.pBindings = bindings;
			VkResult vkResult = vkCreateDescriptorSetLayout(m_pDevice->GetDevice(), &descriptor_set_layout_create_info, nullptr, &pass.descriptorSetLayout);
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

			VkPipelineLayoutCreateInfo layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			layout_create_info.pNext = nullptr;
			layout_create_info.flags = 0;
			layout_create_info.setLayoutCount = _countof(layouts);
			layout_create_info.pSetLayouts = layouts;
			layout_create_info.pushConstantRangeCount = 0;
			layout_create_info.pPushConstantRanges = nullptr;
			VkResult bCreatePipelineLayout = vkCreatePipelineLayout(m_pDevice->GetDevice(), &layout_create_info, nullptr, &pass.pipelineLayout);
			assert(bCreatePipelineLayout == VK_SUCCESS);
		}

		//==============================Pipeline========================================
		{
			VkComputePipelineCreateInfo create_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
			create_info.pNext = nullptr;
			create_info.basePipelineHandle = VK_NULL_HANDLE;
			create_info.basePipelineIndex = 0;
			create_info.flags = 0;
			create_info.layout = pass.pipelineLayout;
			create_info.stage = stage_create_info;
			VkResult vkResult = vkCreateComputePipelines(m_pDevice->GetDevice(), VK_NULL_HANDLE, 1, &create_info, nullptr, &pass.pipeline);
			assert(vkResult == VK_SUCCESS);
		}
	}

	void SSSR::SetupClassifyTilesPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layout_bindings[] = {
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // g_temporal_variance_mask
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_list
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporally_denoised_reflections
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // g_tile_meta_data_mask
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_extracted_roughness
		};

		SetupShaderPass(m_classifyTilesPass, "ClassifyTiles.hlsl", layout_bindings, _countof(layout_bindings));
	}

	void SSSR::SetupPrepareIndirectArgsPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layout_bindings[] = {
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_intersect_args
		};
		SetupShaderPass(m_prepareIndirectArgsPass, "PrepareIndirectArgs.hlsl", layout_bindings, _countof(layout_bindings));
	}

	void SSSR::SetupIntersectionPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layout_bindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_lit_scene
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer_hierarchy
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_environment_map
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_sobol_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_ranking_tile_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_scrambling_tile_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), // g_ray_list

			//Samplers
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_linear_sampler
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLER), // g_environment_map_sampler

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_intersection_result
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_ray_lengths
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER), // g_ray_counter
		};
		SetupShaderPass(m_intersectPass, "Intersect.hlsl", layout_bindings, _countof(layout_bindings));
	}

	void SSSR::SetupResolveSpatial()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layout_bindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_intersection_result
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // g_tile_meta_data_mask

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_spatially_denoised_reflections
		};

		SetupShaderPass(m_resolveSpatialPass, "ResolveSpatial.hlsl", layout_bindings, _countof(layout_bindings), m_isSubgroupSizeControlExtensionAvailable ? VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT : 0);
	}

	void SSSR::SetupResolveTemporal()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layout_bindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_normal_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_depth_buffer
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_motion_vectors
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_temporally_denoised_reflections_history
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_ray_lengths
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_spatially_denoised_reflections
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // g_tile_meta_data_mask

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_temporally_denoised_reflections
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // g_temporal_variance_mask
		};
		SetupShaderPass(m_resolveTemporalPass, "ResolveTemporal.hlsl", layout_bindings, _countof(layout_bindings));
	}

	void SSSR::SetupBlurPass()
	{
		uint32_t binding = 0;
		VkDescriptorSetLayoutBinding layout_bindings[] = {
			//Input
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_roughness
			Bind(binding++, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE), // g_temporally_denoised_reflections
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // g_tile_meta_data_mask

			//Output
			Bind(binding++, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE), // g_denoised_reflections
		};
		SetupShaderPass(m_blurPass, "BlurReflections.hlsl", layout_bindings, _countof(layout_bindings));
	}

	void SSSR::SetupPerformanceCounters()
	{
		//Create TimeStamp Pool
		VkQueryPoolCreateInfo query_pool_create_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		query_pool_create_info.pNext = nullptr;
		query_pool_create_info.flags = 0;
		query_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		query_pool_create_info.queryCount = TIMESTAMP_QUERY_COUNT * m_frameCountBeforeReuse;
		query_pool_create_info.pipelineStatistics = 0;
		VkResult vsRes = vkCreateQueryPool(m_pDevice->GetDevice(), &query_pool_create_info, NULL, &m_timestampQueryPool);
		assert(VK_SUCCESS == vsRes);

		m_timestampQueries.resize(m_frameCountBeforeReuse);
		for (auto& timestamp_queries : m_timestampQueries)
		{
			timestamp_queries.reserve(TIMESTAMP_QUERY_COUNT);
		}
	}

	BlueNoiseSamplerVK& SSSR::GetBlueNoiseSampler2SSP()
	{
		return m_blueNoiseSampler;
	}

	void SSSR::ComputeBarrier(VkCommandBuffer command_buffer) const
	{
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			1, &barrier,
			0, nullptr,
			0, nullptr);
	}

	void SSSR::IndirectArgumentsBarrier(VkCommandBuffer command_buffer) const
	{
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.pNext = nullptr;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			0,
			1, &barrier,
			0, nullptr,
			0, nullptr);
	}

	void SSSR::TransitionBarriers(VkCommandBuffer command_buffer, const VkImageMemoryBarrier* image_barriers, uint32_t image_barriers_count) const
	{
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			image_barriers_count, image_barriers);
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

	void SSSR::QueryTimestamps(VkCommandBuffer command_buffer)
	{
		// Query timestamp value prior to resolving the reflection view
		if (m_isPerformanceCountersEnabled)
		{
			auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

			auto const start_index = m_timestampFrameIndex * TIMESTAMP_QUERY_COUNT;

			if (!timestamp_queries.empty())
			{
				// Reset performance counters
				m_tileClassificationElapsedGpuTicks = 0ull;
				m_denoisingElapsedGpuTicks = 0ull;
				m_intersectionElapsedGpuTicks = 0ull;

				uint32_t timestamp_count = static_cast<uint32_t>(timestamp_queries.size());

				uint64_t data[TIMESTAMP_QUERY_COUNT * 8]; // maximum of 8 frames in flight allowed
				VkResult result = vkGetQueryPoolResults(m_pDevice->GetDevice(),
					m_timestampQueryPool,
					start_index,
					timestamp_count,
					timestamp_count * sizeof(uint64_t),
					data,
					sizeof(uint64_t),
					VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

				assert(result == VK_SUCCESS);

				for (auto i = 0u, j = 1u; j < timestamp_count; ++i, ++j)
				{
					auto const elapsed_time = (data[j] - data[i]);

					switch (timestamp_queries[j])
					{
					case TIMESTAMP_QUERY_TILE_CLASSIFICATION:
						m_tileClassificationElapsedGpuTicks = elapsed_time;
						break;
					case TIMESTAMP_QUERY_INTERSECTION:
						m_intersectionElapsedGpuTicks = elapsed_time;
						break;
					case TIMESTAMP_QUERY_DENOISING:
						m_denoisingElapsedGpuTicks = elapsed_time;
						break;
					default:
						// unrecognized timestamp query
						assert(false && "unrecognized timestamp query");
						break;
					}
				}

			}

			timestamp_queries.clear();

			vkCmdResetQueryPool(command_buffer, m_timestampQueryPool, start_index, TIMESTAMP_QUERY_COUNT);

			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, GetTimestampQueryIndex());
			timestamp_queries.push_back(TIMESTAMP_QUERY_INIT);
		}
	}

	uint32_t SSSR::GetTimestampQueryIndex() const
	{
		return m_timestampFrameIndex * TIMESTAMP_QUERY_COUNT + static_cast<uint32_t>(m_timestampQueries[m_timestampFrameIndex].size());
	}

	void SSSR::InitializeResourceDescriptorSets(const SSSRCreationInfo& input)
	{
		VkDevice device = m_pDevice->GetDevice();
		VkImageView normal_buffers[] = { input.NormalBufferView, input.NormalHistoryBufferView };

		uint32_t binding = 0;
		VkDescriptorSet target_set = VK_NULL_HANDLE;

		// Place the descriptors
		for (int i = 0; i < 2; ++i)
		{
			// Tile Classifier pass
			{
				target_set = m_classifyTilesPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.SpecularRoughnessView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSetStructuredBuffer(device, binding++, m_temporalVarianceMask.buffer_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_rayList.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_rayCounter.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
				SetDescriptorSet(device, binding++, m_temporalDenoiserResultView[i], target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetStructuredBuffer(device, binding++, m_tileMetaDataMask.buffer_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
				SetDescriptorSet(device, binding++, m_roughnessTextureView[i], target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
			}

			// Indirect args pass
			{
				target_set = m_prepareIndirectArgsPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSetBuffer(device, binding++, m_rayCounter.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_intersectionPassIndirectArgs.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Intersection pass
			{
				target_set = m_intersectPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.HDRView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.DepthHierarchyView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.pingPongNormal ? normal_buffers[i] : input.NormalBufferView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTextureView[i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, input.EnvironmentMapView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				SetDescriptorSetBuffer(device, binding++, m_blueNoiseSampler.sobolBuffer.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_blueNoiseSampler.rankingTileBuffer.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_blueNoiseSampler.scramblingTileBuffer.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
				SetDescriptorSetBuffer(device, binding++, m_rayList.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

				SetDescriptorSetSampler(device, binding++, m_linearSampler, target_set); // g_linear_sampler
				SetDescriptorSetSampler(device, binding++, input.EnvironmentMapSampler, target_set); // g_environment_map_sampler

				SetDescriptorSet(device, binding++, m_temporalDenoiserResultView[i], target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_rayLengthsView, target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetBuffer(device, binding++, m_rayCounter.buffer_view_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
			}

			// Spatial denoising pass
			{
				target_set = m_resolveSpatialPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.DepthHierarchyView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.pingPongNormal ? normal_buffers[i] : input.NormalBufferView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTextureView[i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_temporalDenoiserResultView[i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSetStructuredBuffer(device, binding++, m_tileMetaDataMask.buffer_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
				SetDescriptorSet(device, binding++, m_outputBufferView, target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
			}

			// Temporal denoising pass
			{
				target_set = m_resolveTemporalPass.descriptorSets[i];
				binding = 0;

				SetDescriptorSet(device, binding++, input.pingPongNormal ? normal_buffers[i] : input.NormalBufferView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTextureView[i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, input.pingPongNormal ? normal_buffers[1 - i] : input.NormalHistoryBufferView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_roughnessTextureView[1 - i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, input.DepthHierarchyView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, input.MotionVectorsView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_temporalDenoiserResultView[1 - i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_rayLengthsView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				SetDescriptorSet(device, binding++, m_outputBufferView, target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetStructuredBuffer(device, binding++, m_tileMetaDataMask.buffer_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
				SetDescriptorSet(device, binding++, m_temporalDenoiserResultView[i], target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetStructuredBuffer(device, binding++, m_temporalVarianceMask.buffer_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
			}

			// Blur pass
			{
				target_set = m_blurPass.descriptorSets[i];
				binding = 0;
				SetDescriptorSet(device, binding++, m_roughnessTextureView[i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSet(device, binding++, m_temporalDenoiserResultView[i], target_set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
				SetDescriptorSetStructuredBuffer(device, binding++, m_tileMetaDataMask.buffer_, target_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
				SetDescriptorSet(device, binding++, m_outputBufferView, target_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
			}
		}
	}
}