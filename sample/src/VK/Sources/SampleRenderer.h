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
#include "SSSR.h"

// We are queuing (backBufferCount + 0.5) frames, so we need to triple buffer the resources that get modified each frame
static const int backBufferCount = 3;

#define USE_VID_MEM true

using namespace CAULDRON_VK;

//
// This class deals with the GPU side of the sample.
//
namespace SSSR_SAMPLE_VK
{
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
		};

		void OnCreate(Device* pDevice, SwapChain* pSwapChain);
		void OnDestroy();

		void OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height);
		void OnDestroyWindowSizeDependentResources();

		int LoadScene(GLTFCommon* pGLTFCommon, int stage = 0);
		void UnloadScene();

		const std::vector<TimeStamp>& GetTimingValues() { return m_TimeStamps; }

		void OnRender(State* pState, SwapChain* pSwapChain);

	private:
		void CreateApplyReflectionsPipeline();
		void CreateDepthDownsamplePipeline();
		void StallFrame(float targetFrametime);
		void BeginFrame(VkCommandBuffer cb);
		VkBufferMemoryBarrier BufferBarrier(VkBuffer buffer);
		VkImageMemoryBarrier Transition(VkImage image, VkImageLayout before, VkImageLayout after, VkImageAspectFlags aspectMask, int mipCount = 1);
		void Barriers(VkCommandBuffer cb, const std::vector<VkImageMemoryBarrier>& imageBarriers);

		VkCommandBuffer BeginNewCommandBuffer();
		void SubmitCommandBuffer(VkCommandBuffer cb, VkSemaphore* waitSemaphore = NULL, VkSemaphore* signalSemaphores = NULL, VkFence fence = VK_NULL_HANDLE);

		per_frame* FillFrameConstants(State* pState);
		void RenderSpotLights(VkCommandBuffer cb, per_frame* pPerFrame);
		void RenderMotionVectors(VkCommandBuffer cb, per_frame* pPerFrame, State* pState);
		void RenderSkydome(VkCommandBuffer cb, per_frame* pPerFrame, State* pState);
		void RenderScene(VkCommandBuffer cb);
		void RenderBoundingBoxes(VkCommandBuffer cb, per_frame* pPerFrame);
		void RenderLightFrustums(VkCommandBuffer cb, per_frame* pPerFrame, State* pState);
		void DownsampleDepthBuffer(VkCommandBuffer cb);
		void RenderScreenSpaceReflections(VkCommandBuffer cb, per_frame* pPerFrame, State* pState);
		void CopyHistorySurfaces(VkCommandBuffer cb);
		void ApplyReflectionTarget(VkCommandBuffer cb, State* pState);
		void DownsampleScene(VkCommandBuffer cb);
		void RenderBloom(VkCommandBuffer cb);
		void ApplyTonemapping(VkCommandBuffer cb, State* pState, SwapChain* pSwapChain);
		void RenderHUD(VkCommandBuffer cb, SwapChain* pSwapChain);
		void CopyToTexture(VkCommandBuffer cb, Texture* source, Texture* target);

	private:
		Device* m_pDevice;

		uint32_t                        m_Width;
		uint32_t                        m_Height;

		uint32_t                        m_CurrentBackbufferIndex;
		uint32_t                        m_CurrentFrameIndex;

		VkViewport                      m_Viewport;
		VkRect2D                        m_Scissor;

		// Initialize helper classes
		ResourceViewHeaps               m_ResourceViewHeaps;
		UploadHeap                      m_UploadHeap;
		DynamicBufferRing               m_ConstantBufferRing;
		StaticBufferPool                m_VidMemBufferPool;
		StaticBufferPool                m_SysMemBufferPool;
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

		// Samplers
		VkSampler                       m_LinearSampler;

		// BRDF LUT
		Texture                         m_BrdfLut;
		VkImageView                     m_BrdfLutSRV;

		// GUI
		ImGUI			                m_ImGUI;

		// Temporary render targets

		// depth buffer
		Texture                         m_DepthBuffer;
		VkImageView                     m_DepthBufferDSV;

		// Motion Vectors resources
		Texture                         m_MotionVectors;
		VkImageView                     m_MotionVectorsSRV;

		// Normal buffer
		Texture                         m_NormalBuffer;
		VkImageView                     m_NormalBufferSRV;
		Texture                         m_NormalHistoryBuffer;
		VkImageView                     m_NormalHistoryBufferSRV;

		// Specular roughness target
		Texture                         m_SpecularRoughness;
		VkImageView                     m_SpecularRoughnessSRV;

		// shadowmaps
		Texture                         m_ShadowMap;
		VkImageView                     m_ShadowMapDSV;
		VkImageView                     m_ShadowMapSRV;

		// Resolved RT
		Texture                         m_HDR;
		VkImageView                     m_HDRSRV;

		// widgets
		Wireframe                       m_Wireframe;
		WireframeBox                    m_WireframeBox;

		std::vector<TimeStamp>          m_TimeStamps;

		// SSSR Effect
		SSSR m_Sssr;
		XMMATRIX m_prev_view_projection;

		// Pass to apply reflection target
		VkPipeline                      m_ApplyPipeline;
		VkPipelineLayout                m_ApplyPipelineLayout;
		VkDescriptorSetLayout           m_ApplyPipelineDescriptorSetLayout;
		VkDescriptorSet                 m_ApplyPipelineDescriptorSet[backBufferCount];

		VkImageView                     m_ApplyPipelineRTV;

		// Depth downsampling with single CS
		VkPipeline                      m_DepthDownsamplePipeline;
		VkPipelineLayout                m_DepthDownsamplePipelineLayout;
		VkDescriptorSetLayout           m_DepthDownsampleDescriptorSetLayout;
		VkDescriptorSet                 m_DepthDownsampleDescriptorSet;

		VkImageView                     m_DepthBufferSRV;
		VkImageView                     m_DepthHierarchyDescriptors[13];
		Texture                         m_DepthHierarchy;
		VkImageView                     m_DepthHierarchySRV;

		VkBuffer                        m_AtomicCounter;
		VmaAllocation                   m_AtomicCounterAllocation;
		VkBufferView                    m_AtomicCounterUAV;
		UINT                            m_DepthMipLevelCount = 0;

		double                          m_MillisecondsBetweenGpuTicks;

		// Renderpasses
		VkRenderPass                    m_RenderPassShadow;
		VkRenderPass                    m_RenderPassClearHDR;
		VkRenderPass                    m_RenderPassHDR;
		VkRenderPass                    m_RenderPassMV;
		VkRenderPass                    m_RenderPassPBR;
		VkRenderPass                    m_RenderPassApply;

		// Framebuffers
		VkFramebuffer                   m_FramebufferShadows;
		VkFramebuffer                   m_FramebufferHDR;
		VkFramebuffer                   m_FramebufferMV;
		VkFramebuffer                   m_FramebufferPBR;
		VkFramebuffer                   m_FramebufferApply;

		// For multithreaded texture loading
		AsyncPool                       m_AsyncPool;
	};
}