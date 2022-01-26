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

#include "Base/DynamicBufferRing.h"
#include "Base/Texture.h"
#include "BufferDX12.h"
#include "ShaderPass.h"
#include "BlueNoiseSampler.h"

using namespace CAULDRON_DX12;
namespace SSSR_SAMPLE_DX12
{
	class DescriptorTable : public ResourceView { };

	struct SSSRCreationInfo {
		Texture* HDR;
		Texture* DepthHierarchy;
		Texture* MotionVectors;
		Texture* NormalBuffer;
		Texture* NormalHistoryBuffer;
		Texture* SpecularRoughness;
		SkyDome* SkyDome;
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
		SSSR();
		void OnCreate(Device* pDevice, StaticResourceViewHeap& cpuVisibleHeap, ResourceViewHeaps& resourceHeap, UploadHeap& uploadHeap, DynamicBufferRing& constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters);
		void OnCreateWindowSizeDependentResources(const SSSRCreationInfo& input);

		void OnDestroy();
		void OnDestroyWindowSizeDependentResources();

		void Draw(ID3D12GraphicsCommandList* pCommandList, const SSSRConstants& sssrConstants, bool showIntersectResult = false);
		Texture* GetOutputTexture();

		std::uint64_t GetTileClassificationElapsedGpuTicks() const;
		std::uint64_t GetIntersectElapsedGpuTicks() const;
		std::uint64_t GetDenoiserElapsedGpuTicks() const;

		void Recompile();
	private:
		void CreateResources();
		void CreateWindowSizeDependentResources();

		void SetupClassifyTilesPass(bool allocateDescriptorTable);
		void SetupPrepareIndirectArgsPass(bool allocateDescriptorTable);
		void SetupIntersectionPass(bool allocateDescriptorTable);
		void SetupResolveSpatialPass(bool allocateDescriptorTable);
		void SetupResolveTemporalPass(bool allocateDescriptorTable);
		void SetupBlurPass(bool allocateDescriptorTable);
		void InitializeDescriptorTableData(const SSSRCreationInfo& input);
		void SetupPerformanceCounters();
		void QueryTimestamps(ID3D12GraphicsCommandList* pCommandList);
		uint32_t GetTimestampQueryIndex() const;

		Device* m_pDevice;
		DynamicBufferRing* m_pConstantBufferRing;
		StaticResourceViewHeap* m_pCpuVisibleHeap;
		ResourceViewHeaps* m_pResourceViewHeaps;
		UploadHeap* m_pUploadHeap;
		UploadHeapBuffersDX12 m_uploadHeapBuffers;

		uint32_t m_screenWidth;
		uint32_t m_screenHeight;

		// Containing all rays that need to be traced.
		Texture m_rayList;
		// Contains the number of rays that we trace.
		Texture m_rayCounter;
		// Indirect arguments for intersection pass.
		Texture m_intersectionPassIndirectArgs;
		// Intermediate result of the temporal denoising pass - double buffered to keep history and aliases the intersection result.
		Texture m_temporalDenoiserResult[2];
		// Holds the length of each reflection ray - used for temporal reprojection.
		Texture m_rayLengths;
		// Holds the temporal variance of the last two frames.
		Texture m_temporalVarianceMask;
		// Tells us if we have to run the denoiser on a specific tile or if we just have to copy the values
		Texture m_tileMetaDataMask;
		// Extracted roughness values, also double buffered to keep the history. 
		Texture m_roughnessTexture[2];

		// Hold the blue noise buffers.
		BlueNoiseSamplerD3D12 m_blueNoiseSampler;

		ShaderPass m_ClassifyTilesPass;
		ShaderPass m_PrepareIndirectArgsPass;
		ShaderPass m_IntersectPass;
		ShaderPass m_ResolveSpatialPass;
		ShaderPass m_ResolveTemporalPass;
		ShaderPass m_BlurPass;

		D3D12_SAMPLER_DESC m_environmentMapSamplerDesc;

		// The command signature for the indirect dispatches.
		ID3D12CommandSignature* m_pCommandSignature;

		CBV_SRV_UAV m_environmentMapSRV;
		Texture m_outputBuffer;

		uint32_t m_frameCountBeforeReuse;
		uint32_t m_bufferIndex;

		enum TimestampQuery
		{
			TIMESTAMP_QUERY_INIT,
			TIMESTAMP_QUERY_TILE_CLASSIFICATION,
			TIMESTAMP_QUERY_INTERSECTION,
			TIMESTAMP_QUERY_DENOISING,

			TIMESTAMP_QUERY_COUNT
		};

		//The type definition for an array of timestamp queries.	
		using TimestampQueries = std::vector<TimestampQuery>;

		// The query heap for the recorded timestamps.
		ID3D12QueryHeap* m_pTimestampQueryHeap;
		// The buffer for reading the timestamp queries.
		ID3D12Resource* m_pTimestampQueryBuffer;
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