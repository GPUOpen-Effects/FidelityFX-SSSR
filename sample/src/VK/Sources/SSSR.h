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
#pragma once

#include <vector>
#include <vulkan/vulkan.h>

#include "Base/DynamicBufferRing.h"
#include "Base/Texture.h"
#include "BufferVK.h"
#include "UploadHeapVK.h"
#include <DirectXMath.h>

#include "ShaderPass.h"
#include "BlueNoiseSampler.h"

using namespace CAULDRON_VK;
namespace SSSR_SAMPLE_VK
{
	struct SSSRCreationInfo {
		VkImageView HDRView;
		VkImageView DepthHierarchyView;
		VkImageView MotionVectorsView;
		VkImageView NormalBufferView;
		VkImageView NormalHistoryBufferView;
		VkImageView SpecularRoughnessView;
		VkImageView EnvironmentMapView;
		VkSampler EnvironmentMapSampler;
		bool pingPongNormal;
		bool pingPongRoughness;
		uint32_t outputWidth;
		uint32_t outputHeight;
	};

	struct SSSRConstants
	{
		XMFLOAT4X4 invViewProjection;
		XMFLOAT4X4 projection;
		XMFLOAT4X4 invProjection;
		XMFLOAT4X4 view;
		XMFLOAT4X4 invView;
		XMFLOAT4X4 prevViewProjection;
		uint32_t frameIndex;
		uint32_t maxTraversalIntersections;
		uint32_t minTraversalOccupancy;
		uint32_t mostDetailedMip;
		float temporalStabilityFactor;
		float temporalVarianceThreshold;
		float depthBufferThickness;
		float roughnessThreshold;
		uint32_t samplesPerQuad;
		uint32_t temporalVarianceGuidedTracingEnabled;
	};

	class SSSR
	{
	public:
		void OnCreate(Device* pDevice, VkCommandBuffer command_buffer, ResourceViewHeaps* resourceHeap, DynamicBufferRing* constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters);
		void OnCreateWindowSizeDependentResources(VkCommandBuffer command_buffer, const SSSRCreationInfo& input);

		void OnDestroy();
		void OnDestroyWindowSizeDependentResources();

		void Draw(VkCommandBuffer command_buffer, const SSSRConstants& sssrConstants, bool showIntersectResult);
		void GUI(int* pSlice);
		Texture* GetOutputTexture();
		VkImageView GetOutputTextureView() const;

		std::uint64_t GetTileClassificationElapsedGpuTicks() const;
		std::uint64_t GetIntersectElapsedGpuTicks() const;
		std::uint64_t GetDenoiserElapsedGpuTicks() const;

	private:
		void CreateResources(VkCommandBuffer command_buffer);
		void CreateWindowSizeDependentResources(VkCommandBuffer command_buffer);

		void SetupShaderPass(ShaderPass& pass, const char* shader, const VkDescriptorSetLayoutBinding* bindings, uint32_t bindings_count, VkPipelineShaderStageCreateFlags flags = 0);
		void SetupClassifyTilesPass();
		void SetupPrepareIndirectArgsPass();
		void SetupIntersectionPass();
		void SetupResolveSpatial();
		void SetupResolveTemporal();
		void SetupBlurPass();
		void SetupPerformanceCounters();

		void InitializeResourceDescriptorSets(const SSSRCreationInfo& input);

		BlueNoiseSamplerVK& GetBlueNoiseSampler2SSP();
		void ComputeBarrier(VkCommandBuffer command_buffer) const;
		void IndirectArgumentsBarrier(VkCommandBuffer command_buffer) const;
		void TransitionBarriers(VkCommandBuffer command_buffer, const VkImageMemoryBarrier* image_barriers, uint32_t image_barriers_count) const;
		VkImageMemoryBarrier Transition(VkImage image, VkImageLayout before, VkImageLayout after) const;

		void QueryTimestamps(VkCommandBuffer command_buffer);
		uint32_t GetTimestampQueryIndex() const;

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
		BufferVK m_rayCounter;
		// Indirect arguments for intersection pass.
		BufferVK m_intersectionPassIndirectArgs;
		// Holds the temporal variance of the last two frames.
		BufferVK m_temporalVarianceMask;
		// Tells us if we have to run the denoiser on a specific tile or if we just have to copy the values
		BufferVK m_tileMetaDataMask;
		// Extracted roughness values
		Texture m_roughnessTexture[2];
		VkImageView m_roughnessTextureView[2];

		// Intermediate result of the temporal denoising pass - double buffered to keep history and aliases the intersection result.
		Texture m_temporalDenoiserResult[2];
		VkImageView m_temporalDenoiserResultView[2];

		// Holds the length of each reflection ray - used for temporal reprojection.
		Texture m_rayLengths;
		VkImageView m_rayLengthsView;

		BlueNoiseSamplerVK m_blueNoiseSampler;

		ShaderPass m_classifyTilesPass;
		ShaderPass m_prepareIndirectArgsPass;
		ShaderPass m_intersectPass;
		ShaderPass m_resolveSpatialPass;
		ShaderPass m_resolveTemporalPass;
		ShaderPass m_blurPass;

		VkSampler m_linearSampler;
		Texture m_outputBuffer;
		VkImageView m_outputBufferView;

		uint32_t m_bufferIndex = 0;
		uint32_t m_frameCountBeforeReuse = 0;
		bool m_isSubgroupSizeControlExtensionAvailable = false;

		enum TimestampQuery
		{
			TIMESTAMP_QUERY_INIT,
			TIMESTAMP_QUERY_TILE_CLASSIFICATION,
			TIMESTAMP_QUERY_INTERSECTION,
			TIMESTAMP_QUERY_DENOISING,

			TIMESTAMP_QUERY_COUNT
		};
		/**
			The type definition for an array of timestamp queries.
		*/
		using TimestampQueries = std::vector<TimestampQuery>;

		// The query pool containing the recorded timestamps.
		VkQueryPool m_timestampQueryPool;
		// The number of GPU ticks spent in the tile classification pass.
		std::uint64_t m_tileClassificationElapsedGpuTicks;
		// The number of GPU ticks spent in depth buffer intersection.
		std::uint64_t m_intersectionElapsedGpuTicks;
		// The number of GPU ticks spent denoising.
		std::uint64_t m_denoisingElapsedGpuTicks;
		// The array of timestamp that were queried.
		std::vector<TimestampQueries> m_timestampQueries;
		// The index of the active set of timestamp queries.
		uint32_t m_timestampFrameIndex;
		bool m_isPerformanceCountersEnabled;
	};
}