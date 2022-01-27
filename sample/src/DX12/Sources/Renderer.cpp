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
#include "Utils.h"

#undef max
#undef min

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void Renderer::OnCreate(Device* pDevice, SwapChain* pSwapChain, float FontSize)
{
	m_pDevice = pDevice;

	// Initialize helpers

	// Create all the heaps for the resources views
	const uint32_t cbvDescriptorCount = 4000;
	const uint32_t srvDescriptorCount = 8000;
	const uint32_t uavDescriptorCount = 64;
	const uint32_t dsvDescriptorCount = 64;
	const uint32_t rtvDescriptorCount = 64;
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
	const uint32_t staticGeometryMemSize = (5 * 128) * 1024 * 1024;
	m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, true, "StaticGeom");

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
				{GBUFFER_DEPTH,					DXGI_FORMAT_D32_FLOAT},
				{GBUFFER_NORMAL_BUFFER,			DXGI_FORMAT_R10G10B10A2_UNORM},
				{GBUFFER_SPECULAR_ROUGHNESS,	DXGI_FORMAT_R8G8B8A8_UNORM},
				{GBUFFER_MOTION_VECTORS,		DXGI_FORMAT_R16G16_FLOAT},
				{GBUFFER_FORWARD,				DXGI_FORMAT_R16G16B16A16_FLOAT},
			},
			1
		);

		GBufferFlags fullGBuffer = GBUFFER_DEPTH | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS | GBUFFER_SPECULAR_ROUGHNESS | GBUFFER_NORMAL_BUFFER;
		m_RenderPassFullGBuffer.OnCreate(&m_GBuffer, fullGBuffer);
		m_RenderPassJustDepthAndHdr.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD);
	}


#if USE_SHADOWMASK    
	m_shadowResolve.OnCreate(m_pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing);

	// Create the shadow mask descriptors
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMaskUAV);
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMaskSRV);
#endif

	m_SkyDome.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\papermill\\diffuse.dds", "..\\media\\envmaps\\papermill\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_SkyDomeProc.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_Wireframe.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_WireframeBox.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
	m_DownSample.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_Bloom.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_TAA.OnCreate(pDevice, &m_ResourceViewHeaps, &m_VidMemBufferPool);
	m_MagnifierPS.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);

	// Create tonemapping pass
	m_ToneMappingPS.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());
	m_ToneMappingCS.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing);
	m_ColorConversionPS.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());

	// Initialize UI rendering resources
	m_ImGUI.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, pSwapChain->GetFormat(), FontSize);

	// Create additional ambient light IBL
	m_AmbientLight.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\envmaps\\white\\diffuse.dds", "..\\media\\envmaps\\white\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	
	// Create BRDF LUT for apply pass
	m_BrdfLut.InitFromFile(pDevice, &m_UploadHeap, "BrdfLut.dds", false); // LUT images are stored as linear

	CreateApplyReflectionsPipeline();
	m_ResourceViewHeaps.AllocRTVDescriptor(1, &m_ApplyPipelineRTV);

	CreateDepthDownsamplePipeline();
	m_CpuVisibleHeap.AllocDescriptor(1, &m_AtomicCounterUAV);

	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_DepthBufferDescriptor);
	for (int i = 0; i < 13; ++i)
	{
		m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_DepthHierarchyDescriptors[i]);
	}

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
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(4, &m_ApplyPassDescriptorTable[0]);
	m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(4, &m_ApplyPassDescriptorTable[1]);


	// Make sure upload heap has finished uploading before continuing
	m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
	m_UploadHeap.FlushAndFinish();
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
	m_ToneMappingCS.OnDestroy();
	m_ToneMappingPS.OnDestroy();
	m_TAA.OnDestroy();
	m_Bloom.OnDestroy();
	m_DownSample.OnDestroy();
	m_MagnifierPS.OnDestroy();
	m_WireframeBox.OnDestroy();
	m_Wireframe.OnDestroy();
	m_SkyDomeProc.OnDestroy();
	m_SkyDome.OnDestroy();
#if USE_SHADOWMASK
	m_shadowResolve.OnDestroy();
#endif
	m_GBuffer.OnDestroy();

	m_AmbientLight.OnDestroy();
	m_BrdfLut.OnDestroy();

	if (m_ApplyPipelineState != nullptr)
		m_ApplyPipelineState->Release();
	if (m_ApplyRootSignature != nullptr)
		m_ApplyRootSignature->Release();
	if (m_DownsamplePipelineState != nullptr)
		m_DownsamplePipelineState->Release();
	if (m_DownsampleRootSignature != nullptr)
		m_DownsampleRootSignature->Release();

	m_CpuVisibleHeap.OnDestroy();
	m_Sssr.OnDestroy();

	m_UploadHeap.OnDestroy();
	m_GPUTimer.OnDestroy();
	m_VidMemBufferPool.OnDestroy();
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

	// Set the viewport & scissors rect
	m_Viewport = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
	m_RectScissor = { 0, 0, (LONG)Width, (LONG)Height };

#if USE_SHADOWMASK
	// Create shadow mask
	//
	m_ShadowMask.Init(m_pDevice, "shadowbuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
	m_ShadowMask.CreateUAV(0, &m_ShadowMaskUAV);
	m_ShadowMask.CreateSRV(0, &m_ShadowMaskSRV);
#endif

	// Create GBuffer
	//
	m_GBuffer.OnCreateWindowSizeDependentResources(pSwapChain, Width, Height);
	m_GBuffer.m_DepthBuffer.CreateSRV(0, &m_DepthBufferDescriptor);

	m_RenderPassFullGBuffer.OnCreateWindowSizeDependentResources(Width, Height);
	m_RenderPassJustDepthAndHdr.OnCreateWindowSizeDependentResources(Width, Height);

	m_TAA.OnCreateWindowSizeDependentResources(Width, Height, &m_GBuffer);

	// update bloom and downscaling effect
	//
	m_DownSample.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_GBuffer.m_HDR, 5); //downsample the HDR texture 5 times
	m_Bloom.OnCreateWindowSizeDependentResources(m_Width / 2, m_Height / 2, m_DownSample.GetTexture(), 5, &m_GBuffer.m_HDR);
	m_MagnifierPS.OnCreateWindowSizeDependentResources(&m_GBuffer.m_HDR);

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
	}


	SSSRCreationInfo sssr_input_textures;
	sssr_input_textures.HDR = &m_GBuffer.m_HDR;
	sssr_input_textures.NormalBuffer = &m_GBuffer.m_NormalBuffer;
	sssr_input_textures.MotionVectors = &m_GBuffer.m_MotionVectors;
	sssr_input_textures.DepthHierarchy = &m_DepthHierarchy;
	sssr_input_textures.SpecularRoughness = &m_GBuffer.m_SpecularRoughness;
	sssr_input_textures.SkyDome = &m_SkyDome;
	sssr_input_textures.outputWidth = Width;
	sssr_input_textures.outputHeight = Height;
	m_Sssr.OnCreateWindowSizeDependentResources(sssr_input_textures);

	// Fill descriptor table for apply pass
	for (int i = 0; i < 2; ++i)
	{
		m_Sssr.GetOutputTexture(i)->CreateSRV(0, &m_ApplyPassDescriptorTable[i]);
		m_GBuffer.m_NormalBuffer.CreateSRV(1, &m_ApplyPassDescriptorTable[i]);
		m_GBuffer.m_SpecularRoughness.CreateSRV(2, &m_ApplyPassDescriptorTable[i]);
		m_BrdfLut.CreateSRV(3, &m_ApplyPassDescriptorTable[i]);
	}
}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void Renderer::OnDestroyWindowSizeDependentResources()
{
	m_Bloom.OnDestroyWindowSizeDependentResources();
	m_DownSample.OnDestroyWindowSizeDependentResources();

	m_GBuffer.OnDestroyWindowSizeDependentResources();

	m_TAA.OnDestroyWindowSizeDependentResources();

	m_MagnifierPS.OnDestroyWindowSizeDependentResources();

#if USE_SHADOWMASK
	m_ShadowMask.OnDestroy();
#endif

	m_Sssr.OnDestroyWindowSizeDependentResources();

	m_DepthHierarchy.OnDestroy();
	m_AtomicCounter.OnDestroy();
}

void Renderer::OnUpdateDisplayDependentResources(SwapChain* pSwapChain)
{
	// Update pipelines in case the format of the RTs changed (this happens when going HDR)
	m_ColorConversionPS.UpdatePipelines(pSwapChain->GetFormat(), pSwapChain->GetDisplayMode());
	m_ToneMappingPS.UpdatePipelines(pSwapChain->GetFormat());
	m_ImGUI.UpdatePipeline((pSwapChain->GetDisplayMode() == DISPLAYMODE_SDR) ? pSwapChain->GetFormat() : m_GBuffer.m_HDR.GetFormat());
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int Renderer::LoadScene(GLTFCommon* pGLTFCommon, int Stage)
{
	// show loading progress
	ImGui::OpenPopup("Loading");
	if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		float progress = (float)Stage / 13.0f;
		ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
		ImGui::EndPopup();
	}

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
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			pAsyncPool
		);
	}
	else if (Stage == 9)
	{
		Profile p("m_GLTFPBR->OnCreate");

		// same thing as above but for the PBR pass
		m_GLTFPBR = new GltfPbrPass();
		m_GLTFPBR->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			m_pGLTFTexturesAndBuffers,
			&m_AmbientLight,
			false,
			USE_SHADOWMASK,
			&m_RenderPassFullGBuffer,
			pAsyncPool
		);
	}
	else if (Stage == 10)
	{
		Profile p("m_GLTFBBox->OnCreate");

		// just a bounding box pass that will draw boundingboxes instead of the geometry itself
		m_GLTFBBox = new GltfBBoxPass();
		m_GLTFBBox->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_ResourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			&m_Wireframe
		);

		// we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
	}
	else if (Stage == 11)
	{
		Profile p("Flush");

		m_UploadHeap.FlushAndFinish();

		//once everything is uploaded we dont need he upload heaps anymore
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

	while (!m_shadowMapPool.empty())
	{
		m_shadowMapPool.back().ShadowMap.OnDestroy();
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
		m_ResourceViewHeaps.AllocDSVDescriptor((uint32_t)m_shadowMapPool.size(), &m_ShadowMapPoolDSV);
		m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor((uint32_t)m_shadowMapPool.size(), &m_ShadowMapPoolSRV);

		std::vector<SceneShadowInfo>::iterator CurrentShadow = m_shadowMapPool.begin();
		for (uint32_t i = 0; CurrentShadow < m_shadowMapPool.end(); ++i, ++CurrentShadow)
		{
			CurrentShadow->ShadowMap.InitDepthStencil(m_pDevice, "m_pShadowMap", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, CurrentShadow->ShadowResolution, CurrentShadow->ShadowResolution, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
			CurrentShadow->ShadowMap.CreateDSV(CurrentShadow->ShadowIndex, &m_ShadowMapPoolDSV);
			CurrentShadow->ShadowMap.CreateSRV(CurrentShadow->ShadowIndex, &m_ShadowMapPoolSRV);
		}
	}
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

void Renderer::DownsampleDepthBuffer(ID3D12GraphicsCommandList* pCmdLst1)
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

void Renderer::RenderScreenSpaceReflections(ID3D12GraphicsCommandList* pCmdLst1, const Camera& Cam, per_frame* pPerFrame, const UIState* pState)
{
	UserMarker marker(pCmdLst1, "FidelityFX SSSR");

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

	m_Sssr.Draw(pCmdLst1, sssrConstants, m_GPUTimer, pState->bShowIntersectionResults);
}

void Renderer::ApplyReflectionTarget(ID3D12GraphicsCommandList* pCmdLst1, const Camera& Cam, const UIState* pState)
{
	UserMarker marker(pCmdLst1, "Apply Reflection View");

	struct PassConstants
	{
		math::Vector4 viewDir;
		UINT showReflectionTarget;
		UINT applyReflections;
	} constants;

	constants.viewDir = Cam.GetDirection();
	constants.showReflectionTarget = pState->bShowReflectionTarget ? 1 : 0;
	constants.applyReflections = pState->bApplyScreenSpaceReflections ? 1 : 0;

	D3D12_GPU_VIRTUAL_ADDRESS cb = m_ConstantBufferRing.AllocConstantBuffer(sizeof(PassConstants), &constants);

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_ResourceViewHeaps.GetCBV_SRV_UAVHeap() };
	pCmdLst1->SetDescriptorHeaps(1, descriptorHeaps);
	pCmdLst1->SetGraphicsRootSignature(m_ApplyRootSignature);
	pCmdLst1->SetGraphicsRootDescriptorTable(0, m_ApplyPassDescriptorTable[m_FrameIndex % 2].GetGPU());
	pCmdLst1->SetGraphicsRootConstantBufferView(1, cb);
	pCmdLst1->SetPipelineState(m_ApplyPipelineState);
	pCmdLst1->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdLst1->IASetVertexBuffers(0, 0, nullptr);
	pCmdLst1->IASetIndexBuffer(nullptr);

	D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
	viewDesc.Format = m_GBuffer.m_HDR.GetFormat();
	viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipSlice = 0;
	viewDesc.Texture2D.PlaneSlice = 0;
	m_GBuffer.m_HDR.CreateRTV(0, &m_ApplyPipelineRTV, &viewDesc);

	SetViewportAndScissor(pCmdLst1, 0, 0, m_Width, m_Height);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_ApplyPipelineRTV.GetCPU();
	pCmdLst1->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
	pCmdLst1->DrawInstanced(3, 1, 0, 0);

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Apply Reflection View");
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
void Renderer::OnRender(const UIState* pState, const Camera& Cam, SwapChain* pSwapChain)
{
	StallFrame(pState->targetFrameTime);

	// Timing values
	UINT64 gpuTicksPerSecond;
	m_pDevice->GetGraphicsQueue()->GetTimestampFrequency(&gpuTicksPerSecond);

	// Let our resource managers do some house keeping
	m_CommandListRing.OnBeginFrame();
	m_ConstantBufferRing.OnBeginFrame();
	m_GPUTimer.OnBeginFrame(gpuTicksPerSecond, &m_TimeStamps);

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

	// command buffer calls
	ID3D12GraphicsCommandList* pCmdLst1 = m_CommandListRing.GetNewCommandList();

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Begin Frame");

	pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Render shadow maps 
	std::vector<CD3DX12_RESOURCE_BARRIER> ShadowReadBarriers;
	std::vector<CD3DX12_RESOURCE_BARRIER> ShadowWriteBarriers;
	if (m_GLTFDepth && pPerFrame != NULL)
	{
		std::vector<SceneShadowInfo>::iterator ShadowMap = m_shadowMapPool.begin();
		while (ShadowMap < m_shadowMapPool.end())
		{
			pCmdLst1->ClearDepthStencilView(m_ShadowMapPoolDSV.GetCPU(ShadowMap->ShadowIndex), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			++ShadowMap;
		}
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear shadow maps");

		// Render all shadows
		ShadowMap = m_shadowMapPool.begin();
		while (ShadowMap < m_shadowMapPool.end())
		{
			SetViewportAndScissor(pCmdLst1, 0, 0, ShadowMap->ShadowResolution, ShadowMap->ShadowResolution);
			pCmdLst1->OMSetRenderTargets(0, NULL, false, &m_ShadowMapPoolDSV.GetCPU(ShadowMap->ShadowIndex));

			per_frame* cbDepthPerFrame = m_GLTFDepth->SetPerFrameConstants();
			cbDepthPerFrame->mCameraCurrViewProj = pPerFrame->lights[ShadowMap->LightIndex].mLightViewProj;

			m_GLTFDepth->Draw(pCmdLst1);

			// Push a barrier
			ShadowReadBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(ShadowMap->ShadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			ShadowWriteBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(ShadowMap->ShadowMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

			m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow map");
			++ShadowMap;
		}

		// Transition all shadow map barriers
		pCmdLst1->ResourceBarrier((UINT)ShadowReadBarriers.size(), ShadowReadBarriers.data());
	}

	// Shadow resolve ---------------------------------------------------------------------------
#if USE_SHADOWMASK
	if (pPerFrame != NULL)
	{
		const D3D12_RESOURCE_BARRIER preShadowResolve[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_MotionVectorsDepthMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(preShadowResolve), preShadowResolve);

		ShadowResolveFrame shadowResolveFrame;
		shadowResolveFrame.m_Width = m_Width;
		shadowResolveFrame.m_Height = m_Height;
		shadowResolveFrame.m_ShadowMapSRV = m_ShadowMapSRV;
		shadowResolveFrame.m_DepthBufferSRV = m_MotionVectorsDepthMapSRV;
		shadowResolveFrame.m_ShadowBufferUAV = m_ShadowMaskUAV;

		m_shadowResolve.Draw(pCmdLst1, m_pGLTFTexturesAndBuffers, &shadowResolveFrame);

		const D3D12_RESOURCE_BARRIER postShadowResolve[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_MotionVectorsDepthMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(postShadowResolve), postShadowResolve);
	}
	m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow resolve");
#endif

	// Render Scene to the GBuffer ------------------------------------------------
	if (pPerFrame != NULL)
	{
		pCmdLst1->RSSetViewports(1, &m_Viewport);
		pCmdLst1->RSSetScissorRects(1, &m_RectScissor);

		if (m_GLTFPBR)
		{
			const bool bWireframe = pState->WireframeMode != UIState::WireframeMode::WIREFRAME_MODE_OFF;

			std::vector<GltfPbrPass::BatchList> opaque, transparent;
			m_GLTFPBR->BuildBatchLists(&opaque, &transparent, bWireframe);

			// Render opaque geometry
			{
				m_RenderPassFullGBuffer.BeginPass(pCmdLst1, true);
#if USE_SHADOWMASK
				m_GLTFPBR->DrawBatchList(pCmdLst1, &m_ShadowMaskSRV, &solid, bWireframe);
#else
				m_GLTFPBR->DrawBatchList(pCmdLst1, &m_ShadowMapPoolSRV, &opaque, bWireframe);
#endif
				m_GPUTimer.GetTimeStamp(pCmdLst1, "PBR Opaque");
				m_RenderPassFullGBuffer.EndPass();
			}

			// draw skydome
			{
				m_RenderPassJustDepthAndHdr.BeginPass(pCmdLst1, false);

				// Render skydome
				if (pState->SelectedSkydomeTypeIndex == 1)
				{
					math::Matrix4 clipToView = math::inverse(pPerFrame->mCameraCurrViewProj);
					m_SkyDome.Draw(pCmdLst1, clipToView);
					m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome cube");
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
					m_SkyDomeProc.Draw(pCmdLst1, skyDomeConstants);

					m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome proc");
				}

				m_RenderPassJustDepthAndHdr.EndPass();
			}

			// draw transparent geometry
			{
				m_RenderPassFullGBuffer.BeginPass(pCmdLst1, false);

				std::sort(transparent.begin(), transparent.end());
				m_GLTFPBR->DrawBatchList(pCmdLst1, &m_ShadowMapPoolSRV, &transparent, bWireframe);
				m_GPUTimer.GetTimeStamp(pCmdLst1, "PBR Transparent");

				m_RenderPassFullGBuffer.EndPass();
			}
		}

		// draw object's bounding boxes
		if (m_GLTFBBox && pPerFrame != NULL)
		{
			if (pState->bDrawBoundingBoxes)
			{
				m_GLTFBBox->Draw(pCmdLst1, pPerFrame->mCameraCurrViewProj);

				m_GPUTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
			}
		}

		// draw light's frustums
		if (pState->bDrawLightFrustum && pPerFrame != NULL)
		{
			UserMarker marker(pCmdLst1, "light frustrums");

			math::Vector4 vCenter = math::Vector4(0.0f, 0.0f, 0.5f, 0.0f);
			math::Vector4 vRadius = math::Vector4(1.0f, 1.0f, 0.5f, 0.0f);
			math::Vector4 vColor = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
			{
				math::Matrix4 spotlightMatrix = math::inverse(pPerFrame->lights[i].mLightViewProj); // XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
				math::Matrix4 worldMatrix = pPerFrame->mCameraCurrViewProj * spotlightMatrix; //spotlightMatrix * pPerFrame->mCameraCurrViewProj;
				m_WireframeBox.Draw(pCmdLst1, &m_Wireframe, worldMatrix, vCenter, vRadius, vColor);
			}

			m_GPUTimer.GetTimeStamp(pCmdLst1, "Light's frustum");
		}
	}

	if (ShadowWriteBarriers.size())
		pCmdLst1->ResourceBarrier((UINT)ShadowWriteBarriers.size(), ShadowWriteBarriers.data());

	D3D12_RESOURCE_BARRIER preResolve[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::UAV(m_AtomicCounter.GetResource()),
	};
	pCmdLst1->ResourceBarrier(_countof(preResolve), preResolve);

	// Downsample depth buffer
	if (m_GLTFPBR && pPerFrame != NULL)
	{
		DownsampleDepthBuffer(pCmdLst1);
	}

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::UAV(m_DepthHierarchy.GetResource()),
		CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHierarchy.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
		});

	// Stochastic Screen Space Reflections
	if (m_GLTFPBR && pPerFrame != NULL) // Only draw reflections if we draw objects
	{
		RenderScreenSpaceReflections(pCmdLst1, Cam, pPerFrame, pState);
	}

	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_Sssr.GetOutputTexture(m_FrameIndex % 2)->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE), // Wait for reflection target to be written
		CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHierarchy.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 0),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 0)
		});

	// Apply the result of SSSR
	if (m_GLTFPBR && pPerFrame != NULL) // Only reflect if we draw objects
	{
		ApplyReflectionTarget(pCmdLst1, Cam, pState);
	}

	// Bloom, takes HDR as input and applies bloom to it.
	Barriers(pCmdLst1, {
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_Sssr.GetOutputTexture(m_FrameIndex % 2)->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		});

	// Bloom, takes HDR as input and applies bloom to it.
	if (pState->bUseBloom)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { m_GBuffer.m_HDRRTV.GetCPU() };
		pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

		m_DownSample.Draw(pCmdLst1);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample");

		m_Bloom.Draw(pCmdLst1, &m_GBuffer.m_HDR);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Bloom");
	}
	// Apply TAA & Sharpen to m_HDR
	if (pState->bUseTAA)
	{
		m_TAA.Draw(pCmdLst1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "TAA");
	}

	// Magnifier Pass: m_HDR as input, pass' own output
	if (pState->bUseMagnifier)
	{
		// Note: assumes m_GBuffer.HDR is in D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		m_MagnifierPS.Draw(pCmdLst1, pState->MagnifierParams, m_GBuffer.m_HDRSRV);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Magnifier");

		// Transition magnifier state to PIXEL_SHADER_RESOURCE, as it is going to be pRscCurrentInput replacing m_GBuffer.m_HDR which is in that state.
		pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_MagnifierPS.GetPassOutputResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	// Start tracking input/output resources at this point to handle HDR and SDR render paths 
	ID3D12Resource* pRscCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputResource() : m_GBuffer.m_HDR.GetResource();
	CBV_SRV_UAV                  SRVCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputSRV() : m_GBuffer.m_HDRSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE  RTVCurrentOutput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputRTV().GetCPU() : m_GBuffer.m_HDRRTV.GetCPU();
	CBV_SRV_UAV                  UAVCurrentOutput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputUAV() : m_GBuffer.m_HDRUAV;


	// If using FreeSync HDR we need to do the tonemapping in-place and then apply the GUI, later we'll apply the color conversion into the swapchain
	const bool bHDR = pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR;
	if (bHDR)
	{
		// In place Tonemapping ------------------------------------------------------------------------
		{
			D3D12_RESOURCE_BARRIER inputRscToUAV = CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCmdLst1->ResourceBarrier(1, &inputRscToUAV);

			m_ToneMappingCS.Draw(pCmdLst1, &UAVCurrentOutput, pState->Exposure, pState->SelectedTonemapperIndex, m_Width, m_Height);

			D3D12_RESOURCE_BARRIER inputRscToRTV = CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
			pCmdLst1->ResourceBarrier(1, &inputRscToRTV);
		}

		// Render HUD  ------------------------------------------------------------------------
		{
			pCmdLst1->RSSetViewports(1, &m_Viewport);
			pCmdLst1->RSSetScissorRects(1, &m_RectScissor);
			pCmdLst1->OMSetRenderTargets(1, &RTVCurrentOutput, true, NULL);

			m_ImGUI.Draw(pCmdLst1);

			D3D12_RESOURCE_BARRIER hdrToSRV = CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCmdLst1->ResourceBarrier(1, &hdrToSRV);

			m_GPUTimer.GetTimeStamp(pCmdLst1, "ImGUI Rendering");
		}
	}

	// submit command buffer #1
	ThrowIfFailed(pCmdLst1->Close());
	ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

	// Wait for swapchain (we are going to render to it) -----------------------------------
	pSwapChain->WaitForSwapChain();

	// Keep tracking input/output resource views 
	pRscCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputResource() : m_GBuffer.m_HDR.GetResource(); // these haven't changed, re-assign as sanity check
	SRVCurrentInput = pState->bUseMagnifier ? m_MagnifierPS.GetPassOutputSRV() : m_GBuffer.m_HDRSRV;            // these haven't changed, re-assign as sanity check
	RTVCurrentOutput = *pSwapChain->GetCurrentBackBufferRTV();
	UAVCurrentOutput = {}; // no BackBufferUAV.


	ID3D12GraphicsCommandList* pCmdLst2 = m_CommandListRing.GetNewCommandList();

	pCmdLst2->RSSetViewports(1, &m_Viewport);
	pCmdLst2->RSSetScissorRects(1, &m_RectScissor);
	pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), true, NULL);

	if (bHDR)
	{
		// FS HDR mode! Apply color conversion now.
		m_ColorConversionPS.Draw(pCmdLst2, &SRVCurrentInput);
		m_GPUTimer.GetTimeStamp(pCmdLst2, "Color conversion");

		pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
	}
	else
	{
		// non FreeSync HDR mode, that is SDR, here we apply the tonemapping from the HDR into the swapchain and then we render the GUI

		// Tonemapping ------------------------------------------------------------------------
		{
			m_ToneMappingPS.Draw(pCmdLst2, &SRVCurrentInput, pState->Exposure, pState->SelectedTonemapperIndex);
			m_GPUTimer.GetTimeStamp(pCmdLst2, "Tone mapping");

			pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}

		// Render HUD  ------------------------------------------------------------------------
		{
			m_ImGUI.Draw(pCmdLst2);
			m_GPUTimer.GetTimeStamp(pCmdLst2, "ImGUI Rendering");
		}
	}

	// If magnifier is used, make sure m_GBuffer.m_HDR which is not pRscCurrentInput gets reverted back to RT state.
	if (pState->bUseMagnifier)
		pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));

	if (!m_pScreenShotName.empty())
	{
		m_SaveTexture.CopyRenderTargetIntoStagingTexture(m_pDevice->GetDevice(), pCmdLst2, pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	// Transition swapchain into present mode

	pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	m_GPUTimer.OnEndFrame();

	m_GPUTimer.CollectTimings(pCmdLst2);

	// Close & Submit the command list #2 -------------------------------------------------
	ThrowIfFailed(pCmdLst2->Close());

	ID3D12CommandList* CmdListList2[] = { pCmdLst2 };
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);

	// Handle screenshot request
	if (!m_pScreenShotName.empty())
	{
		m_SaveTexture.SaveStagingTextureAsJpeg(m_pDevice->GetDevice(), m_pDevice->GetGraphicsQueue(), m_pScreenShotName.c_str());
		m_pScreenShotName.clear();
	}

	m_FrameIndex++;
}

void Renderer::Recompile()
{
	m_Sssr.Recompile();
}

void Renderer::CreateApplyReflectionsPipeline()
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

void Renderer::CreateDepthDownsamplePipeline()
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