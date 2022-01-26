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
#include "Utils.h"

#undef max
#undef min

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreate(Device* pDevice, SwapChain* pSwapChain)
{
	m_pDevice = pDevice;

	// Initialize helpers

	// Create all the heaps for the resources views
	const uint32_t cbvDescriptorCount = 2000;
	const uint32_t srvDescriptorCount = 2000;
	const uint32_t uavDescriptorCount = 10;
	const uint32_t dsvDescriptorCount = 10;
	const uint32_t rtvDescriptorCount = 60;
	const uint32_t samplerDescriptorCount = 20;
	m_ResourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, dsvDescriptorCount, rtvDescriptorCount, samplerDescriptorCount);
	m_CpuVisibleHeap.OnCreate(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 20, true);

	// Create a commandlist ring for the Direct queue
	uint32_t commandListsPerBackBuffer = 8;
	m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer, pDevice->GetGraphicsQueue()->GetDesc());

	// Create a 'dynamic' constant buffer
	const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
	m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, &m_ResourceViewHeaps);

	// Create a 'static' pool for vertices, indices and constant buffers
	const uint32_t staticGeometryMemSize = 5 * 128 * 1024 * 1024;
	m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, USE_VID_MEM, "StaticGeom");

	// initialize the GPU time stamps module
	m_GPUTimer.OnCreate(pDevice, backBufferCount);

	// Quick helper to upload resources, it has it's own commandList and uses suballocation.
	const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
	m_UploadHeap.OnCreate(pDevice, uploadHeapMemSize);    // initialize an upload heap (uses suballocation for faster results)

	// Create the depth buffer views
	m_ResourceViewHeaps.AllocDSVDescriptor(1, &m_DepthBufferDSV);

	// Create a Shadowmap atlas to hold 4 cascades/spotlights
	m_ShadowMap.InitDepthStencil(pDevice, "m_pShadowMap", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, 2 * 1024, 2 * 1024, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
	m_ResourceViewHeaps.AllocDSVDescriptor(1, &m_ShadowMapDSV);
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMapSRV);
	m_ShadowMap.CreateDSV(0, &m_ShadowMapDSV);
	m_ShadowMap.CreateSRV(0, &m_ShadowMapSRV);

	m_AmbientLight.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\white\\diffuse.dds", "..\\media\\envmaps\\white\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_SkyDome.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\papermill\\diffuse.dds", "..\\media\\envmaps\\papermill\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_SkyDomeProc.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_Wireframe.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_WireframeBox.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
	m_DownSample.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_Bloom.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_BrdfLut.InitFromFile(pDevice, &m_UploadHeap, "BrdfLut.dds", false); // LUT images are stored as linear

	// Create tonemapping pass
	m_ToneMapping.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());

	// Initialize UI rendering resources
	m_ImGUI.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, pSwapChain->GetFormat());

	m_ResourceViewHeaps.AllocRTVDescriptor(1, &m_HDRRTV);

	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_HDRSRV);

	// motion vectors views
	m_ResourceViewHeaps.AllocRTVDescriptor(1, &m_MotionVectorsRTV);
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_MotionVectorsSRV);
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(2, &m_MotionVectorsInputsSRV);
	m_ResourceViewHeaps.AllocRTVDescriptor(1, &m_NormalBufferRTV);
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_NormalBufferSRV);
	m_ResourceViewHeaps.AllocRTVDescriptor(1, &m_SpecularRoughnessRTV);

	CreateApplyReflectionsPipeline();
	m_ResourceViewHeaps.AllocRTVDescriptor(1, &m_ApplyPipelineRTV);

	CreateDepthDownsamplePipeline();
	m_CpuVisibleHeap.AllocDescriptor(1, &m_AtomicCounterUAV);

	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_DepthBufferDescriptor);
	for (int i = 0; i < 13; ++i)
	{
		m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_DepthHierarchyDescriptors[i]);
	}
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_AtomicCounterUAVGPU);

	m_DownsampleDescriptorTable = m_DepthBufferDescriptor.GetGPU();

	// Create a command list for upload
	ID3D12CommandAllocator* ca;
	ThrowIfFailed(m_pDevice->GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)));
	ID3D12GraphicsCommandList* cl;
	ThrowIfFailed(m_pDevice->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, nullptr, IID_PPV_ARGS(&cl)));

	m_Sssr.OnCreate(m_pDevice, m_CpuVisibleHeap, m_ResourceViewHeaps, m_UploadHeap, m_ConstantBufferRing, backBufferCount, true);

	// Wait for the upload to finish;
	ThrowIfFailed(cl->Close());
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CommandListCast(&cl));
	m_pDevice->GPUFlush();
	cl->Release();
	ca->Release();

	// Desctriptor table for apply pass
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(4, &m_ApplyPassDescriptorTable);

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

	if (m_ApplyPipelineState != nullptr)
		m_ApplyPipelineState->Release();
	if (m_ApplyRootSignature != nullptr)
		m_ApplyRootSignature->Release();
	if (m_DownsamplePipelineState != nullptr)
		m_DownsamplePipelineState->Release();
	if (m_DownsampleRootSignature != nullptr)
		m_DownsampleRootSignature->Release();

	m_UploadHeap.OnDestroy();
	m_GPUTimer.OnDestroy();
	m_VidMemBufferPool.OnDestroy();
	m_ConstantBufferRing.OnDestroy();
	m_CommandListRing.OnDestroy();
	m_CpuVisibleHeap.OnDestroy();
	m_ResourceViewHeaps.OnDestroy();
	m_Sssr.OnDestroy();
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
	//
	m_Viewport = { 0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f };

	// Create scissor rectangle
	//
	m_Scissor = { 0, 0, (LONG)m_Width, (LONG)m_Height };

	// Create depth buffer    
	//
	m_DepthBuffer.InitDepthStencil(m_pDevice, "m_depthBuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
	m_DepthBuffer.CreateSRV(0, &m_DepthBufferDescriptor);
	m_DepthBuffer.CreateDSV(0, &m_DepthBufferDSV);

	// Create Texture + RTV
	//
	CD3DX12_RESOURCE_DESC RDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	m_HDR.InitRenderTarget(m_pDevice, "m_HDR", &RDesc, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_HDR.CreateSRV(0, &m_HDRSRV);
	m_HDR.CreateRTV(0, &m_HDRRTV);
	m_HDR.CreateSRV(0, &m_MotionVectorsInputsSRV);

	m_NormalBuffer.InitRenderTarget(m_pDevice, "m_NormalBuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R10G10B10A2_UNORM, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));
	m_NormalBuffer.CreateRTV(0, &m_NormalBufferRTV);
	m_NormalBuffer.CreateSRV(0, &m_NormalBufferSRV);
	m_NormalHistoryBuffer.Init(m_pDevice, "m_NormalHistoryBuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R10G10B10A2_UNORM, m_Width, m_Height, 1, 1, 1, 0), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

	float clearColorOne[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	m_SpecularRoughness.InitRenderTarget(m_pDevice, "m_SpecularRoughness", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), D3D12_RESOURCE_STATE_RENDER_TARGET, clearColorOne);
	m_SpecularRoughness.CreateRTV(0, &m_SpecularRoughnessRTV);

	m_MotionVectors.InitRenderTarget(m_pDevice, "m_MotionVector", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET));
	m_MotionVectors.CreateRTV(0, &m_MotionVectorsRTV);
	m_MotionVectors.CreateSRV(1, &m_MotionVectorsInputsSRV);
	m_MotionVectors.CreateSRV(0, &m_MotionVectorsSRV);

	// update bloom and downscaling effect
	//
	m_DownSample.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_HDR, 5); // downsample the HDR texture 5 times
	m_Bloom.OnCreateWindowSizeDependentResources(m_Width / 2, m_Height / 2, m_DownSample.GetTexture(), 5, &m_HDR);

	// update the pipelines if the swapchain render pass has changed (for example when the format of the swapchain changes)
	//
	m_ToneMapping.UpdatePipelines(pSwapChain->GetFormat());
	m_ImGUI.UpdatePipeline(pSwapChain->GetFormat());

	// Depth downsampling pass with single CS
	{
		m_DepthMipLevelCount = static_cast<uint32_t>(std::log2(std::max(m_Width, m_Height))) + 1;

		// Downsampled depth buffer
		CD3DX12_RESOURCE_DESC dsResDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, m_Width, m_Height, 1, m_DepthMipLevelCount, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		m_DepthHierarchy.Init(m_pDevice, "m_DepthHierarchy", &dsResDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
		UINT i = 0;
		for (; i < 13u; ++i)
		{
			m_DepthHierarchy.CreateUAV(0, &m_DepthHierarchyDescriptors[i], std::min(i, m_DepthMipLevelCount - 1));
		}

		// Atomic counter
		CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		resDesc.Format = DXGI_FORMAT_R32_UINT;
		m_AtomicCounter.InitBuffer(m_pDevice, "m_AtomicCounter", &resDesc, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_AtomicCounter.CreateBufferUAV(0, NULL, &m_AtomicCounterUAV);
		m_AtomicCounter.CreateBufferUAV(0, NULL, &m_AtomicCounterUAVGPU);
	}

	SSSRCreationInfo sssr_input_textures;
	sssr_input_textures.HDR = &m_HDR;
	sssr_input_textures.NormalBuffer = &m_NormalBuffer;
	sssr_input_textures.MotionVectors = &m_MotionVectors;
	sssr_input_textures.DepthHierarchy = &m_DepthHierarchy;
	sssr_input_textures.SpecularRoughness = &m_SpecularRoughness;
	sssr_input_textures.NormalHistoryBuffer = &m_NormalHistoryBuffer;
	sssr_input_textures.SkyDome = &m_SkyDome;
	sssr_input_textures.pingPongNormal = false;
	sssr_input_textures.pingPongRoughness = false;
	sssr_input_textures.outputWidth = Width;
	sssr_input_textures.outputHeight = Height;
	m_Sssr.OnCreateWindowSizeDependentResources(sssr_input_textures);

	// Fill descriptor table for apply pass
	m_Sssr.GetOutputTexture()->CreateSRV(0, &m_ApplyPassDescriptorTable);
	m_NormalBuffer.CreateSRV(1, &m_ApplyPassDescriptorTable);
	m_SpecularRoughness.CreateSRV(2, &m_ApplyPassDescriptorTable);
	m_BrdfLut.CreateSRV(3, &m_ApplyPassDescriptorTable);
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

	m_HDR.OnDestroy();
	m_DepthBuffer.OnDestroy();
	m_DepthHierarchy.OnDestroy();
	m_AtomicCounter.OnDestroy();
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
		{
			Profile p("m_gltfDepth->OnCreate");

			//create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
			m_gltfDepth = new GltfDepthPass();
			m_gltfDepth->OnCreate(
				m_pDevice,
				&m_UploadHeap,
				&m_ResourceViewHeaps,
				&m_ConstantBufferRing,
				&m_VidMemBufferPool,
				m_pGLTFTexturesAndBuffers,
				pAsyncPool
			);
		}

		{
			Profile p("m_gltfMotionVectors->OnCreate");

			m_gltfMotionVectors = new GltfMotionVectorsPass();
			m_gltfMotionVectors->OnCreate(
				m_pDevice,
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
	}
	else if (stage == 9)
	{
		Profile p("m_gltfPBR->OnCreate");

		// same thing as above but for the PBR pass
		m_gltfPBR = new GltfPbrPass();
		m_gltfPBR->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			&m_AmbientLight,
			false,
			false,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			m_SpecularRoughness.GetFormat(),
			DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT_UNKNOWN,
			1,
			pAsyncPool
		);
	}
	else if (stage == 10)
	{
		Profile p("m_gltfBBox->OnCreate");

		// just a bounding box pass that will draw boundingboxes instead of the geometry itself
		m_gltfBBox = new GltfBBoxPass();
		m_gltfBBox->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			&m_Wireframe
		);
#if (USE_VID_MEM==true)
		// we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
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
	m_pDevice->GPUFlush();
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

void SampleRenderer::BeginFrame()
{
	// Timing values
	//
	m_pDevice->GetGraphicsQueue()->GetTimestampFrequency(&m_GpuTicksPerSecond);

	// Let our resource managers do some house keeping 
	//
	m_ConstantBufferRing.OnBeginFrame();
	m_GPUTimer.OnBeginFrame(m_GpuTicksPerSecond, &m_TimeStamps);
	m_CommandListRing.OnBeginFrame();
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

void SampleRenderer::RenderSpotLights(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame)
{
	UserMarker marker(pCmdLst1, "Shadow Map");

	for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
	{
		if (!(pPerFrame->lights[i].type == LightType_Spot || pPerFrame->lights[i].type == LightType_Directional))
			continue;

		// Set the RT's quadrant where to render the shadomap (these viewport offsets need to match the ones in shadowFiltering.h)
		uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
		uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
		uint32_t viewportWidth = m_ShadowMap.GetWidth() / 2;
		uint32_t viewportHeight = m_ShadowMap.GetHeight() / 2;
		SetViewportAndScissor(pCmdLst1, viewportOffsetsX[i] * viewportWidth, viewportOffsetsY[i] * viewportHeight, viewportWidth, viewportHeight);
		pCmdLst1->OMSetRenderTargets(0, NULL, false, &m_ShadowMapDSV.GetCPU());

		GltfDepthPass::per_frame* cbDepthPerFrame = m_gltfDepth->SetPerFrameConstants();
		cbDepthPerFrame->mViewProj = pPerFrame->lights[i].mLightViewProj;

		m_gltfDepth->Draw(pCmdLst1);

		m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow Map");
	}
}

void SampleRenderer::RenderMotionVectors(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState)
{
	UserMarker marker(pCmdLst1, "Motion Vectors");

	// Compute motion vectors
	pCmdLst1->RSSetViewports(1, &m_Viewport);
	pCmdLst1->RSSetScissorRects(1, &m_Scissor);
	D3D12_CPU_DESCRIPTOR_HANDLE rts[] = { m_MotionVectorsRTV.GetCPU(), m_NormalBufferRTV.GetCPU() };
	pCmdLst1->OMSetRenderTargets(2, rts, false, &m_DepthBufferDSV.GetCPU());

	float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pCmdLst1->ClearRenderTargetView(m_MotionVectorsRTV.GetCPU(), clearColor, 0, nullptr);
	pCmdLst1->ClearRenderTargetView(m_NormalBufferRTV.GetCPU(), clearColor, 0, nullptr);

	float clearColorOne[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	pCmdLst1->ClearRenderTargetView(m_SpecularRoughnessRTV.GetCPU(), clearColorOne, 0, nullptr);

	GltfMotionVectorsPass::per_frame* cbDepthPerFrame = m_gltfMotionVectors->SetPerFrameConstants();
	cbDepthPerFrame->mCurrViewProj = pPerFrame->mCameraViewProj;
	cbDepthPerFrame->mPrevViewProj = pState->camera.GetPrevView() * pState->camera.GetProjection();

	m_gltfMotionVectors->Draw(pCmdLst1);
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Motion Vectors");
}

void SampleRenderer::RenderSkydome(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState)
{
	UserMarker marker(pCmdLst1, "Skydome");

	if (pState->skyDomeType == 1)
	{
		XMMATRIX clipToView = XMMatrixInverse(NULL, pPerFrame->mCameraViewProj);
		m_SkyDome.Draw(pCmdLst1, clipToView);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome");
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
		m_SkyDomeProc.Draw(pCmdLst1, skyDomeConstants);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome proc");
	}
}

void SampleRenderer::RenderLightFrustums(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState)
{
	UserMarker marker(pCmdLst1, "Light frustrums");

	XMVECTOR vCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR vRadius = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
	XMVECTOR vColor = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
	{
		XMMATRIX spotlightMatrix = XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
		XMMATRIX worldMatrix = spotlightMatrix * pPerFrame->mCameraViewProj;
		m_WireframeBox.Draw(pCmdLst1, &m_Wireframe, worldMatrix, vCenter, vRadius, vColor);
	}

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Light frustums");
}

void SampleRenderer::DownsampleDepthBuffer(ID3D12GraphicsCommandList* pCmdLst1)
{
	UserMarker marker(pCmdLst1, "Downsample Depth");

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_ResourceViewHeaps.GetCBV_SRV_UAVHeap() };
	pCmdLst1->SetDescriptorHeaps(1, descriptorHeaps);
	pCmdLst1->SetComputeRootSignature(m_DownsampleRootSignature);
	pCmdLst1->SetComputeRootDescriptorTable(0, m_DownsampleDescriptorTable);
	pCmdLst1->SetPipelineState(m_DownsamplePipelineState);

	// Each threadgroup works on 64x64 texels
	uint32_t dimX = (m_Width + 63) / 64;
	uint32_t dimY = (m_Height + 63) / 64;
	pCmdLst1->Dispatch(dimX, dimY, 1);

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample Depth");
}

void SampleRenderer::RenderScreenSpaceReflections(ID3D12GraphicsCommandList* pCmdLst1, per_frame* pPerFrame, State* pState)
{
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

	sssrConstants.frameIndex = m_frame_index;
	sssrConstants.maxTraversalIntersections = pState->maxTraversalIterations;
	sssrConstants.minTraversalOccupancy = pState->minTraversalOccupancy;
	sssrConstants.mostDetailedMip = pState->mostDetailedDepthHierarchyMipLevel;
	sssrConstants.temporalStabilityFactor = pState->temporalStability;
	sssrConstants.temporalVarianceThreshold = pState->temporalVarianceThreshold;
	sssrConstants.depthBufferThickness = pState->depthBufferThickness;
	sssrConstants.samplesPerQuad = pState->samplesPerQuad;
	sssrConstants.temporalVarianceGuidedTracingEnabled = pState->bEnableVarianceGuidedTracing ? 1 : 0;
	sssrConstants.roughnessThreshold = pState->roughnessThreshold;

	m_Sssr.Draw(pCmdLst1, sssrConstants, pState->bShowIntersectionResults);

	//Extract SSSR Timestamps and calculate averages
	uint64_t tileClassificationTime = m_Sssr.GetTileClassificationElapsedGpuTicks();
	static std::deque<float> tileClassificationTimes(100);
	tileClassificationTimes.pop_front();
	tileClassificationTimes.push_back(static_cast<float>(1000 * static_cast<double>(tileClassificationTime) / m_GpuTicksPerSecond));
	pState->tileClassificationTime = 0;
	for (auto& time : tileClassificationTimes)
	{
		pState->tileClassificationTime += time;
	}
	pState->tileClassificationTime /= tileClassificationTimes.size();

	uint64_t intersectionTime = m_Sssr.GetIntersectElapsedGpuTicks();
	static std::deque<float> intersectionTimes(100);
	intersectionTimes.pop_front();
	intersectionTimes.push_back(static_cast<float>(1000 * static_cast<double>(intersectionTime) / m_GpuTicksPerSecond));
	pState->intersectionTime = 0;
	for (auto& time : intersectionTimes)
	{
		pState->intersectionTime += time;
	}
	pState->intersectionTime /= intersectionTimes.size();

	uint64_t denoisingTime = m_Sssr.GetDenoiserElapsedGpuTicks();
	static std::deque<float> denoisingTimes(100);
	denoisingTimes.pop_front();
	denoisingTimes.push_back(static_cast<float>(1000 * static_cast<double>(denoisingTime) / m_GpuTicksPerSecond));
	pState->denoisingTime = 0;
	for (auto& time : denoisingTimes)
	{
		pState->denoisingTime += time;
	}
	pState->denoisingTime /= denoisingTimes.size();

	m_GPUTimer.GetTimeStamp(pCmdLst1, "FidelityFX SSSR");
}

void SampleRenderer::CopyHistorySurfaces(ID3D12GraphicsCommandList* pCmdLst1)
{
	UserMarker marker(pCmdLst1, "Copy History Normals and Roughness");
	// Keep copy of normal roughness buffer for next frame
	CopyToTexture(pCmdLst1, m_NormalBuffer.GetResource(), m_NormalHistoryBuffer.GetResource(), m_Width, m_Height);
}

void SampleRenderer::ApplyReflectionTarget(ID3D12GraphicsCommandList* pCmdLst1, State* pState)
{
	UserMarker marker(pCmdLst1, "Apply Reflection View");

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

	D3D12_GPU_VIRTUAL_ADDRESS cb = m_ConstantBufferRing.AllocConstantBuffer(sizeof(PassConstants), &constants);

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_ResourceViewHeaps.GetCBV_SRV_UAVHeap() };
	pCmdLst1->SetDescriptorHeaps(1, descriptorHeaps);
	pCmdLst1->SetGraphicsRootSignature(m_ApplyRootSignature);
	pCmdLst1->SetGraphicsRootDescriptorTable(0, m_ApplyPassDescriptorTable.GetGPU());
	pCmdLst1->SetGraphicsRootConstantBufferView(1, cb);
	pCmdLst1->SetPipelineState(m_ApplyPipelineState);
	pCmdLst1->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdLst1->IASetVertexBuffers(0, 0, nullptr);
	pCmdLst1->IASetIndexBuffer(nullptr);

	D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
	viewDesc.Format = m_HDR.GetFormat();
	viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipSlice = 0;
	viewDesc.Texture2D.PlaneSlice = 0;
	m_HDR.CreateRTV(0, &m_ApplyPipelineRTV, &viewDesc);

	SetViewportAndScissor(pCmdLst1, 0, 0, m_Width, m_Height);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_ApplyPipelineRTV.GetCPU();
	pCmdLst1->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
	pCmdLst1->DrawInstanced(3, 1, 0, 0);

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Apply Reflection View");
}

void SampleRenderer::DownsampleScene(ID3D12GraphicsCommandList* pCmdLst1)
{
	UserMarker marker(pCmdLst1, "Downsample Scene");

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { m_HDRRTV.GetCPU() };
	pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

	m_DownSample.Draw(pCmdLst1);
	//m_downSample.Gui();
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample Scene");
}

void SampleRenderer::RenderBloom(ID3D12GraphicsCommandList* pCmdLst1)
{
	UserMarker marker(pCmdLst1, "Render Bloom");

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { m_HDRRTV.GetCPU() };
	pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

	m_Bloom.Draw(pCmdLst1, &m_HDR);
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Render Bloom");
}

void SampleRenderer::ApplyTonemapping(ID3D12GraphicsCommandList* pCmdLst2, State* pState, SwapChain* pSwapChain)
{
	UserMarker marker(pCmdLst2, "Apply Tonemapping");

	pCmdLst2->RSSetViewports(1, &m_Viewport);
	pCmdLst2->RSSetScissorRects(1, &m_Scissor);
	pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), false, NULL);

	m_ToneMapping.Draw(pCmdLst2, &m_HDRSRV, pState->exposure, pState->toneMapper);
	m_GPUTimer.GetTimeStamp(pCmdLst2, "Apply Tonemapping");
}

void SampleRenderer::RenderHUD(ID3D12GraphicsCommandList* pCmdLst2, SwapChain* pSwapChain)
{
	UserMarker marker(pCmdLst2, "Render HUD");

	pCmdLst2->RSSetViewports(1, &m_Viewport);
	pCmdLst2->RSSetScissorRects(1, &m_Scissor);
	pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), false, NULL);

	m_ImGUI.Draw(pCmdLst2);

	m_GPUTimer.GetTimeStamp(pCmdLst2, "Render HUD");
}

void Barriers(ID3D12GraphicsCommandList* pCmdLst, const std::vector<D3D12_RESOURCE_BARRIER>& barriers)
{
	pCmdLst->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnRender(State* pState, SwapChain* pSwapChain)
{
	StallFrame(pState->targetFrametime);
	BeginFrame();

	per_frame* pPerFrame = FillFrameConstants(pState);

	// command buffer calls
	//    
	ID3D12GraphicsCommandList* pCmdLst1 = m_CommandListRing.GetNewCommandList();

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Begin Frame");

	// Clears -----------------------------------------------------------------------
	//
	pCmdLst1->ClearDepthStencilView(m_ShadowMapDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear shadow map");

	float clearValuesFloat[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pCmdLst1->ClearRenderTargetView(m_HDRRTV.GetCPU(), clearValuesFloat, 0, nullptr);
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear HDR");

	pCmdLst1->ClearDepthStencilView(m_DepthBufferDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear depth");

	UINT clearValuesUint[4] = { 0, 0, 0, 0 };
	pCmdLst1->ClearUnorderedAccessViewUint(m_AtomicCounterUAVGPU.GetGPU(), m_AtomicCounterUAV.GetCPU(), m_AtomicCounter.GetResource(), clearValuesUint, 0, nullptr); // Set atomic counter to 0.

	// Render to shadow map atlas for spot lights ------------------------------------------
	//
	if (m_gltfDepth && pPerFrame != NULL)
	{
		RenderSpotLights(pCmdLst1, pPerFrame);
	}

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		});

	// Motion vectors ---------------------------------------------------------------------------
	//
	if (m_gltfMotionVectors != NULL && pPerFrame != NULL)
	{
		RenderMotionVectors(pCmdLst1, pPerFrame, pState);
	}

	// Render Scene to the HDR RT ------------------------------------------------
	//
	pCmdLst1->RSSetViewports(1, &m_Viewport);
	pCmdLst1->RSSetScissorRects(1, &m_Scissor);

	D3D12_CPU_DESCRIPTOR_HANDLE rts[] = { m_HDRRTV.GetCPU(), m_SpecularRoughnessRTV.GetCPU() };
	pCmdLst1->OMSetRenderTargets(2, rts, false, &m_DepthBufferDSV.GetCPU());

	if (pPerFrame != NULL)
	{
		RenderSkydome(pCmdLst1, pPerFrame, pState);

		// Render scene to color buffer
		if (m_gltfPBR)
		{
			//set per frame constant buffer values
			m_gltfPBR->Draw(pCmdLst1, &m_ShadowMapSRV);
		}

		// Draw object bounding boxes
		if (m_gltfBBox)
		{
			if (pState->bDrawBoundingBoxes)
			{
				m_gltfBBox->Draw(pCmdLst1, pPerFrame->mCameraViewProj);
				m_GPUTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
			}
		}

		// Draw light frustum
		if (pState->bDrawLightFrustum)
		{
			RenderLightFrustums(pCmdLst1, pPerFrame, pState);
		}

		m_GPUTimer.GetTimeStamp(pCmdLst1, "Rendering scene");
	}

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
		CD3DX12_RESOURCE_BARRIER::UAV(m_AtomicCounter.GetResource()),
		CD3DX12_RESOURCE_BARRIER::Transition(m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		});

	// Downsample depth buffer
	if (m_gltfMotionVectors != NULL && pPerFrame != NULL)
	{
		DownsampleDepthBuffer(pCmdLst1);
	}

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::UAV(m_DepthHierarchy.GetResource()),
		CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHierarchy.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		});

	// Stochastic Screen Space Reflections
	if (m_gltfPBR && pPerFrame != NULL) // Only draw reflections if we draw objects
	{
		RenderScreenSpaceReflections(pCmdLst1, pPerFrame, pState);
	}

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_Sssr.GetOutputTexture()->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE), // Wait for reflection target to be written
		CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHierarchy.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_NormalHistoryBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
		});

	CopyHistorySurfaces(pCmdLst1); // Keep this frames results for next frame

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_NormalHistoryBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 0)
		});

	// Apply the result of SSSR
	if (m_gltfPBR && pPerFrame != NULL) // only reflect if we draw objects
	{
		ApplyReflectionTarget(pCmdLst1, pState);
	}

	// Bloom, takes HDR as input and applies bloom to it.
	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_Sssr.GetOutputTexture()->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		});

	if (pState->bDrawBloom)
	{
		DownsampleScene(pCmdLst1);
		RenderBloom(pCmdLst1);
	}

	// Submit command buffer
	ThrowIfFailed(pCmdLst1->Close());
	ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

	// Wait for swapchain (we are going to render to it)
	pSwapChain->WaitForSwapChain();
	ID3D12GraphicsCommandList* pCmdLst2 = m_CommandListRing.GetNewCommandList();

	Barriers(pCmdLst2, {
		CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
		});

	// Tonemapping
	ApplyTonemapping(pCmdLst2, pState, pSwapChain);

	Barriers(pCmdLst2, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		});

	// Render HUD
	RenderHUD(pCmdLst2, pSwapChain);

	if (pState->screenshotName != NULL)
	{
		m_SaveTexture.CopyRenderTargetIntoStagingTexture(m_pDevice->GetDevice(), pCmdLst2, pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	// Transition swapchain into present mode
	Barriers(pCmdLst2, {
		CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
		});

	m_GPUTimer.OnEndFrame();

	m_GPUTimer.CollectTimings(pCmdLst2);

	// Close & Submit the command list
	ThrowIfFailed(pCmdLst2->Close());

	ID3D12CommandList* CmdListList2[] = { pCmdLst2 };
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);

	if (pState->screenshotName != NULL)
	{
		m_SaveTexture.SaveStagingTextureAsJpeg(m_pDevice->GetDevice(), m_pDevice->GetGraphicsQueue(), pState->screenshotName->c_str());
		pState->screenshotName = NULL;
	}

	// Update previous camera matrices
	pState->camera.UpdatePreviousMatrices();
	if (pPerFrame)
	{
		m_prev_view_projection = pPerFrame->mCameraViewProj;
	}
	m_frame_index++;
}

void SampleRenderer::Recompile()
{
	m_Sssr.Recompile();
}

void SampleRenderer::CreateApplyReflectionsPipeline()
{
	ID3D12Device* device = m_pDevice->GetDevice();

	HRESULT hr;

	CD3DX12_ROOT_PARAMETER root[2];

	CD3DX12_DESCRIPTOR_RANGE descTable[4];
	descTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	descTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	descTable[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
	descTable[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
	root[0].InitAsDescriptorTable(ARRAYSIZE(descTable), descTable);
	root[1].InitAsConstantBufferView(0);

	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ShaderRegister = 0;
	samplerDesc.RegisterSpace = 0;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = ARRAYSIZE(root);
	rsDesc.pParameters = root;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.pStaticSamplers = &samplerDesc;

	ID3DBlob* rs, * rsError;
	hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &rsError);
	if (FAILED(hr))
	{
		Trace("Failed to serialize root signature for apply pipeline.\n");
		ThrowIfFailed(hr);
	}

	hr = device->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), IID_PPV_ARGS(&m_ApplyRootSignature));
	if (FAILED(hr))
	{
		Trace("Failed to create root signature for apply pipeline.\n");
		ThrowIfFailed(hr);
	}

	hr = m_ApplyRootSignature->SetName(L"Apply Reflections RootSignature");
	if (FAILED(hr))
	{
		Trace("Failed to name root signature for apply pipeline.\n");
		ThrowIfFailed(hr);
	}

	D3D12_SHADER_BYTECODE vsShaderByteCode = {};
	D3D12_SHADER_BYTECODE psShaderByteCode = {};
	DefineList defines;
	CompileShaderFromFile("ApplyReflections.hlsl", &defines, "vs_main", "-T vs_6_0", &vsShaderByteCode);
	CompileShaderFromFile("ApplyReflections.hlsl", &defines, "ps_main", "-T ps_6_0", &psShaderByteCode);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.VS = vsShaderByteCode;
	desc.PS = psShaderByteCode;
	desc.pRootSignature = m_ApplyRootSignature;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.DepthStencilState.DepthEnable = false;
	desc.DepthStencilState.StencilEnable = false;
	desc.BlendState.AlphaToCoverageEnable = false;
	desc.BlendState.IndependentBlendEnable = false;

	desc.NumRenderTargets = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.SampleMask = UINT_MAX;
	desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.BlendState.RenderTarget[0].BlendEnable = true;
	desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_ALPHA;
	desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	desc.BlendState.RenderTarget[0].LogicOpEnable = false;
	desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.AntialiasedLineEnable = false;
	desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	desc.RasterizerState.DepthBias = 0;
	desc.RasterizerState.DepthBiasClamp = 0;
	desc.RasterizerState.DepthClipEnable = false;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.ForcedSampleCount = 0;
	desc.RasterizerState.FrontCounterClockwise = false;
	desc.RasterizerState.MultisampleEnable = false;
	desc.RasterizerState.SlopeScaledDepthBias = 0;

	hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_ApplyPipelineState));
	if (FAILED(hr))
	{
		Trace("Failed to create apply pipeline.\n");
		ThrowIfFailed(hr);
	}

	hr = m_ApplyPipelineState->SetName(L"Apply Reflections Pipeline");
	if (FAILED(hr))
	{
		Trace("Failed to name apply pipeline.\n");
		ThrowIfFailed(hr);
	}

	rs->Release();
}

void SampleRenderer::CreateDepthDownsamplePipeline()
{
	HRESULT hr;

	static constexpr uint32_t numRootParameters = 1;
	CD3DX12_ROOT_PARAMETER root[numRootParameters];

	CD3DX12_DESCRIPTOR_RANGE ranges[3] = {};
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 13, 0);
	ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 13);

	root[0].InitAsDescriptorTable(3, ranges);

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = numRootParameters;
	rsDesc.pParameters = root;
	rsDesc.NumStaticSamplers = 0;
	rsDesc.pStaticSamplers = nullptr;

	ID3DBlob* rs, * rsError;
	hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &rsError);
	if (FAILED(hr))
	{
		Trace("Failed to serialize root signature for downsampling pipeline.\n");
		ThrowIfFailed(hr);
	}

	hr = m_pDevice->GetDevice()->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), IID_PPV_ARGS(&m_DownsampleRootSignature));
	if (FAILED(hr))
	{
		Trace("Failed to create root signature for downsampling pipeline.\n");
		ThrowIfFailed(hr);
	}

	hr = m_DownsampleRootSignature->SetName(L"Depth Downsample RootSignature");
	if (FAILED(hr))
	{
		Trace("Failed to name root signature for downsampling pipeline.\n");
		ThrowIfFailed(hr);
	}

	D3D12_SHADER_BYTECODE shaderByteCode = {};
	DefineList defines;
	CompileShaderFromFile("DepthDownsample.hlsl", &defines, "main", "-T cs_6_0", &shaderByteCode);

	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_DownsampleRootSignature;
	desc.CS = shaderByteCode;

	hr = m_pDevice->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_DownsamplePipelineState));
	if (FAILED(hr))
	{
		Trace("Failed to create downsampling pipeline.\n");
		ThrowIfFailed(hr);
	}

	hr = m_DownsamplePipelineState->SetName(L"Depth Downsample Pipeline");
	if (FAILED(hr))
	{
		Trace("Failed to name downsampling pipeline.\n");
		ThrowIfFailed(hr);
	}

	rs->Release();
}