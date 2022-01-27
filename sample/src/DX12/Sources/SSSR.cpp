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
#include "Base\ShaderCompilerHelper.h"
#include "Utils.h"

namespace _1spp
{
#include "../../../samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
}

struct
{
	std::int32_t const (&sobolBuffer)[256 * 256];
	std::int32_t const (&rankingTileBuffer)[128 * 128 * 8];
	std::int32_t const (&scramblingTileBuffer)[128 * 128 * 8];
}
const g_blueNoiseSamplerState = { _1spp::sobol_256spp_256d,  _1spp::rankingTile,  _1spp::scramblingTile };

using namespace CAULDRON_DX12;
namespace SSSR_SAMPLE_DX12
{
	/**
		Initializes a linear sampler for a static sampler description.

		\param shader_register The slot of this sampler.
		\return The resulting sampler description.
	*/
	inline D3D12_STATIC_SAMPLER_DESC InitLinearSampler(uint32_t shader_register)
	{
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
		samplerDesc.ShaderRegister = shader_register;
		samplerDesc.RegisterSpace = 0;
		samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // Compute
		return samplerDesc;
	}

	SSSR::SSSR()
	{
		m_pDevice = nullptr;
		m_pConstantBufferRing = nullptr;
		m_pCpuVisibleHeap = nullptr;
		m_pResourceViewHeaps = nullptr;
		m_pUploadHeap = nullptr;
		m_pCommandSignature = nullptr;
		m_screenWidth = 0;
		m_screenHeight = 0;
		m_bufferIndex = 0;
		m_environmentMapSamplerDesc = {};
	}

	void SSSR_SAMPLE_DX12::SSSR::OnCreate(Device* pDevice, StaticResourceViewHeap& cpuVisibleHeap, ResourceViewHeaps& resourceHeap, UploadHeap& uploadHeap, DynamicBufferRing& constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters)
	{
		m_pDevice = pDevice;
		m_pConstantBufferRing = &constantBufferRing;
		m_pCpuVisibleHeap = &cpuVisibleHeap;
		m_pResourceViewHeaps = &resourceHeap;
		m_pUploadHeap = &uploadHeap;
		m_uploadHeapBuffers.OnCreate(pDevice, 1024 * 1024);

		cpuVisibleHeap.AllocDescriptor(1, &m_environmentMapSRV);

		CreateResources();
		SetupClassifyTilesPass(true);
		SetupPrepareIndirectArgsPass(true);
		SetupIntersectionPass(true);
		SetupResolveTemporalPass(true);
		SetupPrefilterPass(true);
		SetupReprojectPass(true);
		SetupBlueNoisePass(true);
	}

	void SSSR::OnCreateWindowSizeDependentResources(const SSSRCreationInfo& input)
	{
		assert(input.outputWidth > 0);
		assert(input.outputHeight > 0);
		assert(input.HDR != nullptr);
		assert(input.DepthHierarchy != nullptr);
		assert(input.MotionVectors != nullptr);
		assert(input.NormalBuffer != nullptr);
		assert(input.SpecularRoughness != nullptr);
		assert(input.SkyDome != nullptr);

		m_screenWidth = input.outputWidth;
		m_screenHeight = input.outputHeight;
		m_depthBuffer = input.DepthHierarchy;
		m_normalBuffer = input.NormalBuffer;

		D3D12_STATIC_SAMPLER_DESC environmentSamplerDesc = {};
		input.SkyDome->SetDescriptorSpec(0, &m_environmentMapSRV, 0, &environmentSamplerDesc);

		m_environmentMapSamplerDesc.AddressU = environmentSamplerDesc.AddressU;
		m_environmentMapSamplerDesc.AddressV = environmentSamplerDesc.AddressV;
		m_environmentMapSamplerDesc.AddressW = environmentSamplerDesc.AddressW;
		m_environmentMapSamplerDesc.ComparisonFunc = environmentSamplerDesc.ComparisonFunc;
		m_environmentMapSamplerDesc.Filter = environmentSamplerDesc.Filter;
		m_environmentMapSamplerDesc.MaxAnisotropy = environmentSamplerDesc.MaxAnisotropy;
		m_environmentMapSamplerDesc.MaxLOD = environmentSamplerDesc.MaxLOD;
		m_environmentMapSamplerDesc.MinLOD = environmentSamplerDesc.MinLOD;
		m_environmentMapSamplerDesc.MipLODBias = environmentSamplerDesc.MipLODBias;

		CreateWindowSizeDependentResources();
		InitializeDescriptorTableData(input);
	}

	void SSSR_SAMPLE_DX12::SSSR::OnDestroy()
	{
		m_uploadHeapBuffers.OnDestroy();

		m_classifyTilesPass.OnDestroy();
		m_prepareIndirectArgsPass.OnDestroy();
		m_intersectPass.OnDestroy();
		m_resolveTemporalPass.OnDestroy();
		m_prefilterPass.OnDestroy();
		m_reprojectPass.OnDestroy();
		m_blueNoisePass.OnDestroy();

		m_rayCounter.OnDestroy();
		m_intersectionPassIndirectArgs.OnDestroy();
		m_blueNoiseTexture.OnDestroy();
		m_blueNoiseSampler.OnDestroy();

		if (m_pCommandSignature)
		{
			m_pCommandSignature->Release();
		}
	}

	void SSSR::OnDestroyWindowSizeDependentResources()
	{
		m_rayList.OnDestroy();
		m_denoiserTileList.OnDestroy();
		m_extractedRoughness.OnDestroy();
		m_depthHistory.OnDestroy();
		m_normalHistory.OnDestroy();
		m_roughnessHistory.OnDestroy();
		m_radiance[0].OnDestroy();
		m_radiance[1].OnDestroy();
		m_variance[0].OnDestroy();
		m_variance[1].OnDestroy();
		m_sampleCount[0].OnDestroy();
		m_sampleCount[1].OnDestroy();
		m_averageRadiance[0].OnDestroy();
		m_averageRadiance[1].OnDestroy();
		m_reprojectedRadiance.OnDestroy();
	}

	void SSSR_SAMPLE_DX12::SSSR::Draw(ID3D12GraphicsCommandList* pCommandList, const SSSRConstants& sssrConstants, GPUTimestamps& gpuTimer, bool showIntersectResult)
	{
		//Set Constantbuffer data
		D3D12_GPU_VIRTUAL_ADDRESS constantbufferAddress = m_pConstantBufferRing->AllocConstantBuffer(sizeof(SSSRConstants), (void*)&sssrConstants);

		//Render
		ID3D12DescriptorHeap* descriptorHeaps[] = { m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap() };
		pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Ensure that the ray list is in UA state
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_rayList.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_denoiserTileList.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_extractedRoughness.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_blueNoiseTexture.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			pCommandList->ResourceBarrier(_countof(barriers), barriers);
		}

		{
			UserMarker marker(pCommandList, "FFX DNSR ClassifyTiles");
			pCommandList->SetComputeRootSignature(m_classifyTilesPass.pRootSignature);
			pCommandList->SetComputeRootDescriptorTable(0, m_classifyTilesPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
			pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
			pCommandList->SetComputeRootDescriptorTable(2, m_classifyTilesPass.descriptorTables_Sampler[m_bufferIndex].GetGPU());
			pCommandList->SetPipelineState(m_classifyTilesPass.pPipeline);
			uint32_t dim_x = DivideRoundingUp(m_screenWidth, 8u);
			uint32_t dim_y = DivideRoundingUp(m_screenHeight, 8u);
			pCommandList->Dispatch(dim_x, dim_y, 1);
		}

		// At the same time prepare the blue noise texture for intersection
		{
			UserMarker marker(pCommandList, "FFX DNSR PrepareBlueNoise");
			pCommandList->SetComputeRootSignature(m_blueNoisePass.pRootSignature);
			pCommandList->SetComputeRootDescriptorTable(0, m_blueNoisePass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
			pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
			pCommandList->SetPipelineState(m_blueNoisePass.pPipeline);
			uint32_t dim_x = 128u / 8u;
			uint32_t dim_y = 128u / 8u;
			pCommandList->Dispatch(dim_x, dim_y, 1);
		}

		gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR ClassifyTiles + PrepareBlueNoise");

		// Ensure that the tile classification pass finished
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::UAV(m_rayCounter.GetResource()),
					CD3DX12_RESOURCE_BARRIER::Transition(m_rayList.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_denoiserTileList.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_intersectionPassIndirectArgs.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_extractedRoughness.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_blueNoiseTexture.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCommandList->ResourceBarrier(_countof(barriers), barriers);
		}

		{
			UserMarker marker(pCommandList, "FFX SSSR PrepareIndirectArgs");
			pCommandList->SetComputeRootSignature(m_prepareIndirectArgsPass.pRootSignature);
			pCommandList->SetComputeRootDescriptorTable(0, m_prepareIndirectArgsPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
			pCommandList->SetPipelineState(m_prepareIndirectArgsPass.pPipeline);
			pCommandList->Dispatch(1, 1, 1);
			gpuTimer.GetTimeStamp(pCommandList, "FFX SSSR PrepareIndirectArgs");
		}

		// Ensure that the arguments are written
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_intersectionPassIndirectArgs.GetResource(),	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
			};
			pCommandList->ResourceBarrier(_countof(barriers), barriers);
		}

		{
			UserMarker marker(pCommandList, "FFX SSSR Intersection");
			pCommandList->SetComputeRootSignature(m_intersectPass.pRootSignature);
			pCommandList->SetComputeRootDescriptorTable(0, m_intersectPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
			pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
			pCommandList->SetComputeRootDescriptorTable(2, m_intersectPass.descriptorTables_Sampler[m_bufferIndex].GetGPU());
			pCommandList->SetPipelineState(m_intersectPass.pPipeline);
			pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), 0, nullptr, 0);
			gpuTimer.GetTimeStamp(pCommandList, "FFX SSSR Intersection");
		}

		if (showIntersectResult)
		{
			// Ensure that the intersection pass is done.
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCommandList->ResourceBarrier(_countof(barriers), barriers);
			}
		}
		else
		{
			// Ensure that the intersection pass is done.
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					// Transition to SRV
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					// Transition to UAV
					CD3DX12_RESOURCE_BARRIER::Transition(m_reprojectedRadiance.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_averageRadiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_variance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_sampleCount[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				};
				pCommandList->ResourceBarrier(_countof(barriers), barriers);
			}

			// Reproject pass
			{
				UserMarker marker(pCommandList, "FFX DNSR Reproject");
				pCommandList->SetComputeRootSignature(m_reprojectPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_reprojectPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_reprojectPass.pPipeline);
				pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), 12, nullptr, 0);
				gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR Reproject");
			}

			// Ensure that the Reproject pass is done
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					// Transition to SRV
					CD3DX12_RESOURCE_BARRIER::Transition(m_averageRadiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_variance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_sampleCount[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					// Transition to UAV
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[1 - m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_variance[1 - m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_sampleCount[1 - m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				};
				pCommandList->ResourceBarrier(_countof(barriers), barriers);
			}

			// Prefilter pass
			{
				UserMarker marker(pCommandList, "FFX DNSR Prefilter");
				pCommandList->SetComputeRootSignature(m_prefilterPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_prefilterPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_prefilterPass.pPipeline);
				pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), 12, nullptr, 0);
				gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR Prefilter");
			}

			// Ensure that the Prefilter pass is done
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					// Transition to SRV
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[1 - m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_reprojectedRadiance.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_variance[1 - m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_sampleCount[1 - m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					// Transition to UAV
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_variance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_sampleCount[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				};
				pCommandList->ResourceBarrier(_countof(barriers), barriers);
			}

			// Temporal accumulation passes
			{
				UserMarker marker(pCommandList, "FFX DNSR Resolve Temporal");
				pCommandList->SetComputeRootSignature(m_resolveTemporalPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_resolveTemporalPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_resolveTemporalPass.pPipeline);
				pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), 12, nullptr, 0);
				gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR Resolve Temporal");
			}

			// Ensure that the temporal accumulation finished
			{
				D3D12_RESOURCE_BARRIER barriers[] = { 
					CD3DX12_RESOURCE_BARRIER::Transition(m_radiance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_variance[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_sampleCount[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCommandList->ResourceBarrier(_countof(barriers), barriers);
			}

			// Also, copy the depth buffer for the next frame. This is optional if the engine already keeps a copy around. 
			{
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(m_depthHistory.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
						CD3DX12_RESOURCE_BARRIER::Transition(m_depthBuffer->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_normalHistory.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
						CD3DX12_RESOURCE_BARRIER::Transition(m_normalBuffer->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_roughnessHistory.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
						CD3DX12_RESOURCE_BARRIER::Transition(m_extractedRoughness.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
					};
					pCommandList->ResourceBarrier(_countof(barriers), barriers);
				}

				CopyToTexture(pCommandList, m_depthBuffer->GetResource(), m_depthHistory.GetResource(), m_screenWidth, m_screenHeight);
				CopyToTexture(pCommandList, m_normalBuffer->GetResource(), m_normalHistory.GetResource(), m_screenWidth, m_screenHeight);
				CopyToTexture(pCommandList, m_extractedRoughness.GetResource(), m_roughnessHistory.GetResource(), m_screenWidth, m_screenHeight);

				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(m_depthHistory.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_depthBuffer->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_normalHistory.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_normalBuffer->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_roughnessHistory.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(m_extractedRoughness.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					};
					pCommandList->ResourceBarrier(_countof(barriers), barriers);
				}
			}
		}

		m_bufferIndex = 1 - m_bufferIndex;
	}

	Texture* SSSR::GetOutputTexture(int frame)
	{
		return &m_radiance[frame % 2];
	}

	void SSSR::Recompile()
	{
		m_pDevice->GPUFlush();
		m_classifyTilesPass.DestroyPipeline();
		m_prepareIndirectArgsPass.DestroyPipeline();
		m_intersectPass.DestroyPipeline();
		m_resolveTemporalPass.DestroyPipeline();
		m_reprojectPass.DestroyPipeline();
		m_prefilterPass.DestroyPipeline();
		m_blueNoisePass.DestroyPipeline();

		SetupClassifyTilesPass(false);
		SetupPrepareIndirectArgsPass(false);
		SetupIntersectionPass(false);
		SetupResolveTemporalPass(false);
		SetupReprojectPass(false);
		SetupPrefilterPass(false);
		SetupBlueNoisePass(false);
	}

	void SSSR::CreateResources()
	{
		uint32_t elementSize = 4;
		//==============================Create Tile Classification-related buffers============================================
		{
			m_rayCounter.InitBuffer(m_pDevice, "SSSR - Ray Counter", &CD3DX12_RESOURCE_DESC::Buffer(4ull * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		//==============================Create PrepareIndirectArgs-related buffers============================================
		{
			m_intersectionPassIndirectArgs.InitBuffer(m_pDevice, "SSSR - Intersect Indirect Args", &CD3DX12_RESOURCE_DESC::Buffer(6ull * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		}
		//==============================Command Signature==========================================
		{
			D3D12_INDIRECT_ARGUMENT_DESC dispatch = {};
			dispatch.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

			D3D12_COMMAND_SIGNATURE_DESC desc = {};
			desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
			desc.NodeMask = 0;
			desc.NumArgumentDescs = 1;
			desc.pArgumentDescs = &dispatch;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&m_pCommandSignature)));
		}
		//==============================Blue Noise buffers============================================
		{
			auto const& sampler_state = g_blueNoiseSamplerState;
			BlueNoiseSamplerD3D12& sampler = m_blueNoiseSampler;
			sampler.sobolBuffer.InitFromMem(m_pDevice, "SSSR - Sobol Buffer", &m_uploadHeapBuffers, &sampler_state.sobolBuffer, _countof(sampler_state.sobolBuffer), sizeof(std::int32_t));
			sampler.rankingTileBuffer.InitFromMem(m_pDevice, "SSSR - Ranking Tile Buffer", &m_uploadHeapBuffers, &sampler_state.rankingTileBuffer, _countof(sampler_state.rankingTileBuffer), sizeof(std::int32_t));
			sampler.scramblingTileBuffer.InitFromMem(m_pDevice, "SSSR - Scrambling Tile Buffer", &m_uploadHeapBuffers, &sampler_state.scramblingTileBuffer, _countof(sampler_state.scramblingTileBuffer), sizeof(std::int32_t));
			m_uploadHeapBuffers.FlushAndFinish();

			CD3DX12_RESOURCE_DESC blueNoiseDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8_UNORM, 128, 128, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			m_blueNoiseTexture.Init(m_pDevice, "Reflection Denoiser - Blue Noise Texture", &blueNoiseDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
		}
	}

	void SSSR::CreateWindowSizeDependentResources()
	{
		uint32_t elementSize = 4;
		//==============================Create Tile Classification-related buffers============================================
		{
			UINT64 num_pixels = (UINT64)m_screenWidth * m_screenHeight;
			m_rayList.InitBuffer(m_pDevice, "SSSR - Ray List", &CD3DX12_RESOURCE_DESC::Buffer(num_pixels * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			m_denoiserTileList.InitBuffer(m_pDevice, "SSSR - Denoiser Tile List", &CD3DX12_RESOURCE_DESC::Buffer(num_pixels * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		//==============================Create denoising-related resources==============================
		{
			CD3DX12_RESOURCE_DESC radianceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC averageRadianceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R11G11B10_FLOAT, DivideRoundingUp(m_screenWidth, 8u), DivideRoundingUp(m_screenHeight, 8u), 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC varianceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC sampleCountDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			
			CD3DX12_RESOURCE_DESC depthHistoryDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC normalHistoryDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_normalBuffer->GetFormat(), m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC roughnessTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

			m_extractedRoughness.Init(m_pDevice, "Reflection Denoiser - Extracted Roughness Texture", &roughnessTextureDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_depthHistory.Init(m_pDevice, "Reflection Denoiser - Depth Buffer History", &depthHistoryDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_normalHistory.Init(m_pDevice, "Reflection Denoiser - Normal Buffer History", &normalHistoryDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_roughnessHistory.Init(m_pDevice, "Reflection Denoiser - Extracted Roughness History Texture", &roughnessTextureDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

			m_radiance[0].Init(m_pDevice, "Reflection Denoiser - Radiance 0", &radianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_radiance[1].Init(m_pDevice, "Reflection Denoiser - Radiance 1", &radianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_variance[0].Init(m_pDevice, "Reflection Denoiser - Variance 0", &varianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_variance[1].Init(m_pDevice, "Reflection Denoiser - Variance 1", &varianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_sampleCount[0].Init(m_pDevice, "Reflection Denoiser - Variance 0", &sampleCountDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_sampleCount[1].Init(m_pDevice, "Reflection Denoiser - Variance 1", &sampleCountDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_averageRadiance[0].Init(m_pDevice, "Reflection Denoiser - Average Radiance 0", &averageRadianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_averageRadiance[1].Init(m_pDevice, "Reflection Denoiser - Average Radiance 1", &averageRadianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_reprojectedRadiance.Init(m_pDevice, "Reflection Denoiser - Reprojected Radiance", &radianceDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
		}

		m_bufferIndex = 0;
	}

	void SSSR::SetupClassifyTilesPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_classifyTilesPass;

		const UINT srvCount = 5;
		const UINT uavCount = 5;

		D3D12_SHADER_BYTECODE shaderByteCode = {};
		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("ClassifyTiles.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}
		//==============================Allocate Descriptor Table=========================================
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
				//Descriptor Table - Sampler
				m_pResourceViewHeaps->AllocSamplerDescriptor(1, &shaderpass.descriptorTables_Sampler[i]);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange_1[2] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);
			CD3DX12_DESCRIPTOR_RANGE DescRange_2[1] = {};
			{
				//Param 2
				int rangeCount = 0;
				DescRange_2[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_2[0], D3D12_SHADER_VISIBILITY_ALL); // g_environment_map_sampler
			}

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = 0;
			descRootSignature.pStaticSamplers = nullptr;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - ClassifyTiles Rootsignature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
		}
		//==============================PipelineStates============================================
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
			descPso.CS = shaderByteCode;
			descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			descPso.pRootSignature = shaderpass.pRootSignature;
			descPso.NodeMask = 0;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - ClassifyTiles Pso");
		}
	}

	void SSSR::SetupPrepareIndirectArgsPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_prepareIndirectArgsPass;

		const UINT srvCount = 0;
		const UINT uavCount = 2;

		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("PrepareIndirectArgs.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}
		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[1] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange[1] = {};
			{
				int rangeCount = 0;
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
			}

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = 0;
			descRootSignature.pStaticSamplers = nullptr;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "PrepareIndirectArgs Rootsignature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
			//==============================PipelineStates============================================
			{
				D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
				descPso.CS = shaderByteCode;
				descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
				descPso.pRootSignature = shaderpass.pRootSignature;
				descPso.NodeMask = 0;

				ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
				CAULDRON_DX12::SetName(shaderpass.pPipeline, "PrepareIndirectArgs Pso");
			}
		}
	}

	void SSSR::SetupIntersectionPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_intersectPass;

		const UINT srvCount = 7;
		const UINT uavCount = 2;

		D3D12_SHADER_BYTECODE shaderByteCode = {};
		ID3D12Device* device = m_pDevice->GetDevice();

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("Intersect.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				//Descriptor Table - CBV_SRV_UAV
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
				//Descriptor Table - Sampler
				m_pResourceViewHeaps->AllocSamplerDescriptor(1, &shaderpass.descriptorTables_Sampler[i]);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange_1[3] = {};
			CD3DX12_DESCRIPTOR_RANGE DescRange_2[1] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);
			{
				//Param 2
				int rangeCount = 0;
				DescRange_2[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_2[0], D3D12_SHADER_VISIBILITY_ALL); // g_environment_map_sampler
			}

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = 0;
			descRootSignature.pStaticSamplers = nullptr;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "SSSR - Intersection Root Signature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
		}
		//==============================PipelineStates============================================
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
			descPso.CS = shaderByteCode;
			descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			descPso.pRootSignature = shaderpass.pRootSignature;
			descPso.NodeMask = 0;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "SSSR - Intersection Pso");
		}
	}

	void SSSR::SetupResolveTemporalPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_resolveTemporalPass;

		const UINT srvCount = 7;
		const UINT uavCount = 3;

		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("ResolveTemporal.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================

		//Descriptor Table - CBV_SRV_UAV
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
			}
		}

		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange[3] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = _countof(samplerDescs);
			descRootSignature.pStaticSamplers = samplerDescs;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - Temporal Resolve Root Signature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
		}
		//==============================PipelineStates============================================
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
			descPso.CS = shaderByteCode;
			descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			descPso.pRootSignature = shaderpass.pRootSignature;
			descPso.NodeMask = 0;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - Temporal Resolve Pso");
		}
	}

	void SSSR::SetupPrefilterPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_prefilterPass;

		const UINT srvCount = 8;
		const UINT uavCount = 3;

		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("Prefilter.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			ID3D12Device* device = m_pDevice->GetDevice();

			//Descriptor Table - CBV_SRV_UAV
			for (size_t i = 0; i < 2; i++)
			{
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[2] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange[2] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = _countof(samplerDescs);
			descRootSignature.pStaticSamplers = samplerDescs;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - Prefilter Root Signature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
		}
		//==============================PipelineStates============================================
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
			descPso.CS = shaderByteCode;
			descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			descPso.pRootSignature = shaderpass.pRootSignature;
			descPso.NodeMask = 0;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - Prefilter Pso");
		}
	}

	void SSSR::SetupReprojectPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_reprojectPass;

		const UINT srvCount = 14;
		const UINT uavCount = 4;

		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("Reproject.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			ID3D12Device* device = m_pDevice->GetDevice();

			//Descriptor Table - CBV_SRV_UAV
			for (size_t i = 0; i < 2; i++)
			{
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange[3] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = _countof(samplerDescs);
			descRootSignature.pStaticSamplers = samplerDescs;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - Reproject Root Signature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
		}
		//==============================PipelineStates============================================
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
			descPso.CS = shaderByteCode;
			descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			descPso.pRootSignature = shaderpass.pRootSignature;
			descPso.NodeMask = 0;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - Reproject Pso");
		}
	}

	void SSSR::SetupBlueNoisePass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_blueNoisePass;

		const UINT srvCount = 3;
		const UINT uavCount = 1;

		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("PrepareBlueNoiseTexture.hlsl", &defines, "main", "-enable-16bit-types -T cs_6_2 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			ID3D12Device* device = m_pDevice->GetDevice();

			//Descriptor Table - CBV_SRV_UAV
			for (size_t i = 0; i < 2; i++)
			{
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(srvCount + uavCount, &shaderpass.descriptorTables_CBV_SRV_UAV[i]);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange[3] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
				DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = RTSlot;
			descRootSignature.NumStaticSamplers = 0;
			descRootSignature.pStaticSamplers = nullptr;
			// deny uneccessary access to certain pipeline stages   
			descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ID3DBlob* pOutBlob = nullptr;
			ID3DBlob* pErrorBlob = nullptr;
			ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&shaderpass.pRootSignature))
			);
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - Prepare Blue Noise Texture Root Signature");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();
		}
		//==============================PipelineStates============================================
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
			descPso.CS = shaderByteCode;
			descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			descPso.pRootSignature = shaderpass.pRootSignature;
			descPso.NodeMask = 0;

			ThrowIfFailed(m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&shaderpass.pPipeline)));
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - Prepare Blue Noise Texture Pso");
		}
	}

	void SSSR::InitializeDescriptorTableData(const SSSRCreationInfo& input)
	{
		ID3D12Device* device = m_pDevice->GetDevice();

		for (size_t i = 0; i < 2; i++)
		{
			//==============================ClassifyTiles==========================================
			{
				auto& table = m_classifyTilesPass.descriptorTables_CBV_SRV_UAV[i];
				auto& table_sampler = m_classifyTilesPass.descriptorTables_Sampler[i];
				int tableSlot = 0;

				input.SpecularRoughness->CreateSRV(tableSlot++, &table);
				input.DepthHierarchy->CreateSRV(tableSlot++, &table);
				m_variance[1 - i].CreateSRV(tableSlot++, &table);
				input.NormalBuffer->CreateSRV(tableSlot++, &table);
				device->CopyDescriptorsSimple(1, table.GetCPU(tableSlot++), m_environmentMapSRV.GetCPU(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				m_pDevice->GetDevice()->CreateSampler(&m_environmentMapSamplerDesc, table_sampler.GetCPU(0));

				m_rayList.CreateBufferUAV(tableSlot++, nullptr, &table);
				m_rayCounter.CreateBufferUAV(tableSlot++, nullptr, &table);

				// Clear intersection result
				m_radiance[i].CreateUAV(tableSlot++, &table);
				m_extractedRoughness.CreateUAV(tableSlot++, &table);

				m_denoiserTileList.CreateBufferUAV(tableSlot++, nullptr, &table); // g_denoiser_tile_list
			}
			//==============================PrepareBlueNoiseTexture==========================================
			{
				auto& table = m_blueNoisePass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				m_blueNoiseSampler.sobolBuffer.CreateSRV(tableSlot++, &table);
				m_blueNoiseSampler.rankingTileBuffer.CreateSRV(tableSlot++, &table);
				m_blueNoiseSampler.scramblingTileBuffer.CreateSRV(tableSlot++, &table);

				m_blueNoiseTexture.CreateUAV(tableSlot++, &table);
			}
			//==============================PrepareIndirectArgs==========================================
			{
				auto& table = m_prepareIndirectArgsPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				m_rayCounter.CreateBufferUAV(tableSlot++, nullptr, &table);
				m_intersectionPassIndirectArgs.CreateBufferUAV(tableSlot++, nullptr, &table);
			}
			//==============================Intersection==========================================
			{
				auto& table = m_intersectPass.descriptorTables_CBV_SRV_UAV[i];
				auto& table_sampler = m_intersectPass.descriptorTables_Sampler[i];

				int tableSlot = 0;

				input.HDR->CreateSRV(tableSlot++, &table);
				input.DepthHierarchy->CreateSRV(tableSlot++, &table);
				input.NormalBuffer->CreateSRV(tableSlot++, &table);
				m_extractedRoughness.CreateSRV(tableSlot++, &table);
				device->CopyDescriptorsSimple(1, table.GetCPU(tableSlot++), m_environmentMapSRV.GetCPU(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				m_blueNoiseTexture.CreateSRV(tableSlot++, &table);
				m_rayList.CreateSRV(tableSlot++, &table);

				// Intersection result
				m_radiance[i].CreateUAV(tableSlot++, &table);
				m_rayCounter.CreateBufferUAV(tableSlot++, nullptr, &table);

				m_pDevice->GetDevice()->CreateSampler(&m_environmentMapSamplerDesc, table_sampler.GetCPU(0));
			}
			//==============================Reproject==========================================
			{
				auto& table = m_reprojectPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				input.DepthHierarchy->CreateSRV(tableSlot++, &table); // g_depth_buffer
				m_extractedRoughness.CreateSRV(tableSlot++, &table); // g_roughness
				input.NormalBuffer->CreateSRV(tableSlot++, &table); // g_normal
				m_depthHistory.CreateSRV(tableSlot++, &table); // g_depth_buffer_history
				m_roughnessHistory.CreateSRV(tableSlot++, &table); // g_roughness_history
				m_normalHistory.CreateSRV(tableSlot++, &table); // g_normal_history

				m_radiance[i].CreateSRV(tableSlot++, &table); // g_in_radiance
				m_radiance[1 - i].CreateSRV(tableSlot++, &table); // g_radiance_history
				input.MotionVectors->CreateSRV(tableSlot++, &table); // g_motion_vector

				m_averageRadiance[1 - i].CreateSRV(tableSlot++, &table); // g_average_radiance_history
				m_variance[1 - i].CreateSRV(tableSlot++, &table); // g_variance_history
				m_sampleCount[1 - i].CreateSRV(tableSlot++, &table); // g_sample_count_history
				m_blueNoiseTexture.CreateSRV(tableSlot++, &table);
				m_denoiserTileList.CreateSRV(tableSlot++, &table); // g_denoiser_tile_list

				m_reprojectedRadiance.CreateUAV(tableSlot++, &table); // g_out_reprojected_radiance
				m_averageRadiance[i].CreateUAV(tableSlot++, &table); // g_out_average_radiance
				m_variance[i].CreateUAV(tableSlot++, &table); // g_out_variance
				m_sampleCount[i].CreateUAV(tableSlot++, &table); // g_out_sample_count
			}
			//==============================Prefilter==========================================
			{
				auto& table = m_prefilterPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				input.DepthHierarchy->CreateSRV(tableSlot++, &table); // g_depth_buffer
				m_extractedRoughness.CreateSRV(tableSlot++, &table); // g_roughness
				input.NormalBuffer->CreateSRV(tableSlot++, &table); // g_normal
				m_averageRadiance[i].CreateSRV(tableSlot++, &table); // g_average_radiance
				m_radiance[i].CreateSRV(tableSlot++, &table); // g_in_radiance
				m_variance[i].CreateSRV(tableSlot++, &table); // g_in_variance
				m_sampleCount[i].CreateSRV(tableSlot++, &table); // g_in_sample_count
				m_denoiserTileList.CreateSRV(tableSlot++, &table); // g_denoiser_tile_list

				m_radiance[1 - i].CreateUAV(tableSlot++, &table); // g_out_radiance
				m_variance[1 - i].CreateUAV(tableSlot++, &table); // g_out_variance
				m_sampleCount[1 - i].CreateUAV(tableSlot++, &table); // g_out_sample_count
			}
			//==============================ResolveTemporal==========================================
			{
				auto& table = m_resolveTemporalPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				m_extractedRoughness.CreateSRV(tableSlot++, &table); // g_roughness
				m_averageRadiance[i].CreateSRV(tableSlot++, &table); // g_average_radiance
				m_radiance[1 - i].CreateSRV(tableSlot++, &table); // g_in_radiance
				m_reprojectedRadiance.CreateSRV(tableSlot++, &table); // g_in_reprojected_radiance
				m_variance[1 - i].CreateSRV(tableSlot++, &table); // g_in_variance
				m_sampleCount[1 - i].CreateSRV(tableSlot++, &table); // g_in_sample_count
				m_denoiserTileList.CreateSRV(tableSlot++, &table); // g_denoiser_tile_list

				m_radiance[i].CreateUAV(tableSlot++, &table); // g_out_radiance
				m_variance[i].CreateUAV(tableSlot++, &table); // g_out_variance
				m_sampleCount[i].CreateUAV(tableSlot++, &table); // g_out_sample_count
			}
		}
	}
}