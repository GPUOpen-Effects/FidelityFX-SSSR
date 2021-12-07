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

#include <vector>
#include <vulkan/vulkan.h>

#include "Base/DynamicBufferRing.h"
#include "Base/Texture.h"
#include "BufferVK.h"
#include "ImageVK.h"
#include "UploadHeapVK.h"
#include <DirectXMath.h>

#include "ShaderPass.h"
#include "BlueNoiseSampler.h"

using namespace CAULDRON_VK;
namespace SSSR_SAMPLE_VK
{
	struct SSSRCreationInfo {
		VkImageView HDRView;
		Texture* DepthHierarchy;
		VkImageView DepthHierarchyView;
		VkImageView MotionVectorsView;
		Texture* NormalBuffer;
		VkImageView NormalBufferView;
		VkImageView SpecularRoughnessView;
		VkImageView EnvironmentMapView;
		VkSampler EnvironmentMapSampler;
		uint32_t outputWidth;
		uint32_t outputHeight;
	};

	struct SSSRConstants
	{
		Vectormath::Matrix4 invViewProjection;
		Vectormath::Matrix4 projection;
		Vectormath::Matrix4 invProjection;
		Vectormath::Matrix4 view;
		Vectormath::Matrix4 invView;
		Vectormath::Matrix4 prevViewProjection;
		unsigned int bufferDimensions[2];
		float inverseBufferDimensions[2];
		float temporalStabilityFactor;
		float depthBufferThickness;
		float roughnessThreshold;
		float varianceThreshold;
		uint32_t frameIndex;
		uint32_t maxTraversalIntersections;
		uint32_t minTraversalOccupancy;
		uint32_t mostDetailedMip;
		uint32_t samplesPerQuad;
		uint32_t temporalVarianceGuidedTracingEnabled;
	};

	class SSSR
	{
	public:
		void OnCreate(Device* pDevice, VkCommandBuffer commandBuffer, ResourceViewHeaps* resourceHeap, DynamicBufferRing* constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters);
		void OnCreateWindowSizeDependentResources(VkCommandBuffer commandBuffer, const SSSRCreationInfo& input);

		void OnDestroy();
		void OnDestroyWindowSizeDependentResources();

		void Draw(VkCommandBuffer commandBuffer, const SSSRConstants& sssrConstants, GPUTimestamps& gpuTimer, bool showIntersectResult);
		void GUI(int* pSlice);
		VkImageView GetOutputTextureView(int frame) const;

	private:
		void CreateResources(VkCommandBuffer commandBuffer);
		void CreateWindowSizeDependentResources(VkCommandBuffer commandBuffer);

		void SetupShaderPass(ShaderPass& pass, const char* shader, const VkDescriptorSetLayoutBinding* bindings, uint32_t bindingsCount, VkPipelineShaderStageCreateFlags flags = 0);
		void SetupClassifyTilesPass();
		void SetupBlueNoisePass();
		void SetupPrepareIndirectArgsPass();
		void SetupIntersectionPass();
		void SetupResolveTemporalPass();
		void SetupPrefilterPass();
		void SetupReprojectPass();

		void InitializeResourceDescriptorSets(const SSSRCreationInfo& input);

		void ComputeBarrier(VkCommandBuffer commandBuffer) const;
		void IndirectArgumentsBarrier(VkCommandBuffer commandBuffer) const;
		void TransitionBarriers(VkCommandBuffer commandBuffer, const VkImageMemoryBarrier* imageBarriers, uint32_t imageBarrierCount) const;
		VkImageMemoryBarrier Transition(VkImage image, VkImageLayout before, VkImageLayout after) const;

		Device* m_pDevice = nullptr;
		DynamicBufferRing* m_pConstantBufferRing;
		ResourceViewHeaps* m_pResourceViewHeaps;
		UploadHeapVK m_uploadHeap;

		uint32_t m_outputWidth;
		uint32_t m_outputHeight;

		VkDescriptorSetLayout m_uniformBufferDescriptorSetLayout;
		VkDescriptorSet m_uniformBufferDescriptorSet[8];

		// Containing all rays that need to be traced.
		BufferVK m_rayList;
		BufferVK m_denoiserTileList;
		BufferVK m_rayCounter;
		// Indirect arguments for intersection pass.
		BufferVK m_intersectionPassIndirectArgs;

		// Intermediate results of the denoiser passes.
		ImageVK m_radiance[2];
		ImageVK m_variance[2];
		ImageVK m_sampleCount[2];
		ImageVK m_averageRadiance[2];
		ImageVK m_reprojectedRadiance;

		// Extracted roughness values
		ImageVK m_roughnessTexture;

		// Extracted roughness from last frame.
		ImageVK m_roughnessHistoryTexture;

		// Normal buffer from last frame.
		ImageVK m_normalHistoryTexture;

		// Depth buffer from last frame.
		ImageVK m_depthHistoryTexture;

		Texture* m_depthHierarchy;
		Texture* m_normalTexture;

		// Blue noise resources.
		ImageVK m_blueNoiseTexture;
		BlueNoiseSamplerVK m_blueNoiseSampler;

		ShaderPass m_classifyTilesPass;
		ShaderPass m_prepareIndirectArgsPass;
		ShaderPass m_intersectPass;
		ShaderPass m_resolveTemporalPass;
		ShaderPass m_reprojectPass;
		ShaderPass m_prefilterPass;
		ShaderPass m_blueNoisePass;

		VkSampler m_linearSampler;
		VkSampler m_previousDepthSampler;

		uint32_t m_frameCountBeforeReuse = 0;
		bool m_isSubgroupSizeControlExtensionAvailable = false;
	};
}