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

#include "stdafx.h"

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
		Texture* SpecularRoughness;
		SkyDome* SkyDome;
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
		SSSR();
		void OnCreate(Device* pDevice, StaticResourceViewHeap& cpuVisibleHeap, ResourceViewHeaps& resourceHeap, UploadHeap& uploadHeap, DynamicBufferRing& constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters);
		void OnCreateWindowSizeDependentResources(const SSSRCreationInfo& input);

		void OnDestroy();
		void OnDestroyWindowSizeDependentResources();

		void Draw(ID3D12GraphicsCommandList* pCommandList, const SSSRConstants& sssrConstants, GPUTimestamps& gpuTimer, bool showIntersectResult);
		Texture* GetOutputTexture(int frame);
		void Recompile();

	private:
		void CreateResources();
		void CreateWindowSizeDependentResources();

		void SetupClassifyTilesPass(bool allocateDescriptorTable);
		void SetupPrepareIndirectArgsPass(bool allocateDescriptorTable);
		void SetupIntersectionPass(bool allocateDescriptorTable);
		void SetupResolveTemporalPass(bool allocateDescriptorTable);
		void SetupPrefilterPass(bool allocateDescriptorTable);
		void SetupReprojectPass(bool allocateDescriptorTable);
		void SetupBlueNoisePass(bool allocateDescriptorTable);
		void InitializeDescriptorTableData(const SSSRCreationInfo& input);

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
		Texture m_denoiserTileList;
		// Contains the number of rays that we trace.
		Texture m_rayCounter;
		// Indirect arguments for intersection pass.
		Texture m_intersectionPassIndirectArgs;

		// Depth buffer of this frame
		Texture* m_depthBuffer;
		// Normal buffer of this frame
		Texture* m_normalBuffer;
		// Extracted roughness values. 
		Texture m_extractedRoughness;
		// Depth buffer copy from last frame.
		Texture m_depthHistory;
		// Normal buffer copy from last frame.
		Texture m_normalHistory;
		// Roughness buffer copy from last frame.
		Texture m_roughnessHistory;

		// Resources produced by the denoiser and intersection pass. Ping ponging to keep history around.
		Texture m_radiance[2];
		Texture m_variance[2];
		Texture m_sampleCount[2];
		Texture m_averageRadiance[2];
		Texture m_reprojectedRadiance;

		// Hold the blue noise buffers.
		BlueNoiseSamplerD3D12 m_blueNoiseSampler;
		Texture m_blueNoiseTexture;
		ShaderPass m_blueNoisePass;

		ShaderPass m_classifyTilesPass;
		ShaderPass m_prepareIndirectArgsPass;
		ShaderPass m_intersectPass;
		ShaderPass m_resolveTemporalPass;
		ShaderPass m_prefilterPass;
		ShaderPass m_reprojectPass;

		D3D12_SAMPLER_DESC m_environmentMapSamplerDesc;

		// The command signature for the indirect dispatches.
		ID3D12CommandSignature* m_pCommandSignature;

		CBV_SRV_UAV m_environmentMapSRV;

		uint32_t m_bufferIndex;
	};
}