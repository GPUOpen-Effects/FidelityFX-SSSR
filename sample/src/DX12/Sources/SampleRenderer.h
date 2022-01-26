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

#include <memory>

#include "base/SaveTexture.h"
#include "SSSR.h"

// We are queuing (backBufferCount + 0.5) frames, so we need to triple buffer the resources that get modified each frame
static const int backBufferCount = 3;

#define USE_VID_MEM true

using namespace CAULDRON_DX12;
using namespace SSSR_SAMPLE_DX12;
//
// This class deals with the GPU side of the sample.
//
class SampleRenderer
{
public:
	struct State
	{
		float time;
		Camera camera;

		float exposure;
		float emmisiveFactor;
		float iblFactor;
		float lightIntensity;
		XMFLOAT3 lightColor;
		Camera lightCamera;

		int   toneMapper;
		int   skyDomeType;
		bool  bDrawBoundingBoxes;
		bool  bDrawLightFrustum;
		bool  bDrawBloom;
		bool  bDrawScreenSpaceReflections;

		float targetFrametime;

		bool bShowIntersectionResults;
		float temporalStability;
		float temporalVarianceThreshold;
		int maxTraversalIterations;
		int mostDetailedDepthHierarchyMipLevel;
		float depthBufferThickness;
		int minTraversalOccupancy;
		int samplesPerQuad;
		bool bEnableVarianceGuidedTracing;
		float roughnessThreshold;

		float tileClassificationTime;
		float intersectionTime;
		float denoisingTime;

		bool showReflectionTarget;
		bool isBenchmarking;
		const std::string* screenshotName;
	};

	void OnCreate(Device* pDevice, SwapChain* pSwapChain);
	void OnDestroy();

	void OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height);
	void OnDestroyWindowSizeDependentResources();

	int LoadScene(GLTFCommon* pGLTFCommon, int stage = 0);
	void UnloadScene();

	const std::vector<TimeStamp>& GetTimingValues() { return m_TimeStamps; }

	void OnRender(State* pState, SwapChain* pSwapChain);
	void Recompile();

private:
	void CreateApplyReflectionsPipeline();
	void CreateDepthDownsamplePipeline();
	void StallFrame(float targetFrametime);
	void BeginFrame();

	per_frame* FillFrameConstants(State* pState);
	void RenderSpotLights(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame);
	void RenderMotionVectors(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState);
	void RenderSkydome(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState);
	void RenderLightFrustums(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState);
	void DownsampleDepthBuffer(ID3D12GraphicsCommandList* pCmdLst1);
	void RenderScreenSpaceReflections(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState);
	void CopyHistorySurfaces(ID3D12GraphicsCommandList* pCmdLst1);
	void ApplyReflectionTarget(ID3D12GraphicsCommandList* pCmdLst1, State* pState);
	void DownsampleScene(ID3D12GraphicsCommandList* pCmdLst1);
	void RenderBloom(ID3D12GraphicsCommandList* pCmdLst1);
	void ApplyTonemapping(ID3D12GraphicsCommandList* pCmdLst2, State* pState, SwapChain* pSwapChain);
	void RenderHUD(ID3D12GraphicsCommandList* pCmdLst2, SwapChain* pSwapChain);

private:
	Device* m_pDevice;

	uint32_t                        m_Width;
	uint32_t                        m_Height;

	D3D12_VIEWPORT                  m_Viewport;
	D3D12_RECT                      m_Scissor;

	// Initialize helper classes
	ResourceViewHeaps               m_ResourceViewHeaps;
	StaticResourceViewHeap          m_CpuVisibleHeap;
	UploadHeap                      m_UploadHeap;
	DynamicBufferRing               m_ConstantBufferRing;
	StaticBufferPool                m_VidMemBufferPool;
	CommandListRing                 m_CommandListRing;
	GPUTimestamps                   m_GPUTimer;

	//gltf passes
	GltfPbrPass* m_gltfPBR;
	GltfBBoxPass* m_gltfBBox;
	GltfDepthPass* m_gltfDepth;
	GltfMotionVectorsPass* m_gltfMotionVectors;
	GLTFTexturesAndBuffers* m_pGLTFTexturesAndBuffers;

	// effects
	Bloom                           m_Bloom;
	SkyDome                         m_SkyDome;
	SkyDome                         m_AmbientLight;
	DownSamplePS                    m_DownSample;
	SkyDomeProc                     m_SkyDomeProc;
	ToneMapping                     m_ToneMapping;

	//SSSR
	SSSR m_Sssr;
	uint32_t m_frame_index = 0;
	XMMATRIX m_prev_view_projection;

	// BRDF LUT
	Texture                         m_BrdfLut;

	// GUI
	ImGUI                           m_ImGUI;

	// depth buffer
	DSV                             m_DepthBufferDSV;
	Texture                         m_DepthBuffer;

	// Motion Vectors resources
	Texture                         m_MotionVectors;
	RTV                             m_MotionVectorsRTV;
	CBV_SRV_UAV                     m_MotionVectorsSRV;
	CBV_SRV_UAV                     m_MotionVectorsInputsSRV;

	// Normal buffer
	Texture                         m_NormalBuffer;
	RTV                             m_NormalBufferRTV;
	CBV_SRV_UAV                     m_NormalBufferSRV;
	Texture                         m_NormalHistoryBuffer;

	// Specular roughness target
	Texture                         m_SpecularRoughness;
	RTV                             m_SpecularRoughnessRTV;

	// shadowmaps
	Texture                         m_ShadowMap;
	DSV                             m_ShadowMapDSV;
	CBV_SRV_UAV                     m_ShadowMapSRV;

	// Resolved RT
	Texture                         m_HDR;
	CBV_SRV_UAV                     m_HDRSRV;
	RTV                             m_HDRRTV;

	// widgets
	Wireframe                       m_Wireframe;
	WireframeBox                    m_WireframeBox;

	std::vector<TimeStamp>          m_TimeStamps;

	RTV                             m_ApplyPipelineRTV;
	ID3D12RootSignature* m_ApplyRootSignature;
	ID3D12PipelineState* m_ApplyPipelineState;
	CBV_SRV_UAV                     m_ApplyPassDescriptorTable;

	// Depth downsampling with single CS
	ID3D12RootSignature* m_DownsampleRootSignature;
	ID3D12PipelineState* m_DownsamplePipelineState;
	D3D12_GPU_DESCRIPTOR_HANDLE     m_DownsampleDescriptorTable;
	CBV_SRV_UAV                     m_DepthBufferDescriptor;
	CBV_SRV_UAV                     m_DepthHierarchyDescriptors[13];
	CBV_SRV_UAV                     m_AtomicCounterUAVGPU;
	Texture                         m_DepthHierarchy;
	Texture                         m_AtomicCounter;
	CBV_SRV_UAV                     m_AtomicCounterUAV;
	UINT                            m_DepthMipLevelCount = 0;

	UINT64                          m_GpuTicksPerSecond;

	SaveTexture                     m_SaveTexture;

	// For multithreaded texture loading
	AsyncPool                       m_AsyncPool;
};
