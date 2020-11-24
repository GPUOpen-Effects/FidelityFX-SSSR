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
#include "Base\ShaderCompilerHelper.h"
#include "Utils.h"

namespace _1spp
{
#include "../../../samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
}

/*
	The available blue noise sampler with 2spp sampling mode.
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
		m_frameCountBeforeReuse = 0;
		m_denoisingElapsedGpuTicks = 0;
		m_isPerformanceCountersEnabled = false;
		m_tileClassificationElapsedGpuTicks = 0;
		m_intersectionElapsedGpuTicks = 0;
		m_denoisingElapsedGpuTicks = 0;
		m_timestampFrameIndex = 0;

		m_pTimestampQueryBuffer = nullptr;
		m_pTimestampQueryHeap = nullptr;

		m_environmentMapSamplerDesc = {};
	}

	void SSSR_SAMPLE_DX12::SSSR::OnCreate(Device* pDevice, StaticResourceViewHeap& cpuVisibleHeap, ResourceViewHeaps& resourceHeap, UploadHeap& uploadHeap, DynamicBufferRing& constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters)
	{
		m_pDevice = pDevice;
		m_pConstantBufferRing = &constantBufferRing;
		m_pCpuVisibleHeap = &cpuVisibleHeap;
		m_pResourceViewHeaps = &resourceHeap;
		m_pUploadHeap = &uploadHeap;
		m_frameCountBeforeReuse = frameCountBeforeReuse;
		m_isPerformanceCountersEnabled = enablePerformanceCounters;

		m_uploadHeapBuffers.OnCreate(pDevice, 1024 * 1024);

		cpuVisibleHeap.AllocDescriptor(1, &m_environmentMapSRV);

		CreateResources();
		SetupClassifyTilesPass(true);
		SetupPrepareIndirectArgsPass(true);
		SetupIntersectionPass(true);
		SetupResolveSpatialPass(true);
		SetupResolveTemporalPass(true);
		SetupBlurPass(true);
		SetupPerformanceCounters();
	}

	void SSSR::OnCreateWindowSizeDependentResources(const SSSRCreationInfo& input)
	{
		assert(input.outputWidth > 0);
		assert(input.outputHeight > 0);
		assert(input.HDR != nullptr);
		assert(input.DepthHierarchy != nullptr);
		assert(input.MotionVectors != nullptr);
		assert(input.NormalBuffer != nullptr);
		assert(input.NormalHistoryBuffer != nullptr);
		assert(input.SpecularRoughness != nullptr);
		assert(input.SkyDome != nullptr);

		m_screenWidth = input.outputWidth;
		m_screenHeight = input.outputHeight;

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

		m_ClassifyTilesPass.OnDestroy();
		m_PrepareIndirectArgsPass.OnDestroy();
		m_IntersectPass.OnDestroy();
		m_BlurPass.OnDestroy();
		m_ResolveSpatialPass.OnDestroy();
		m_ResolveTemporalPass.OnDestroy();

		m_rayCounter.OnDestroy();
		m_intersectionPassIndirectArgs.OnDestroy();
		m_blueNoiseSampler.OnDestroy();
		
		if (m_pCommandSignature)
		{
			m_pCommandSignature->Release();
		}
		if (m_pTimestampQueryHeap)
		{
			m_pTimestampQueryHeap->Release();
		}
		if (m_pTimestampQueryBuffer)
		{
			m_pTimestampQueryBuffer->Release();
		}
	}

	void SSSR::OnDestroyWindowSizeDependentResources()
	{
		m_rayList.OnDestroy();
		m_temporalDenoiserResult[0].OnDestroy();
		m_temporalDenoiserResult[1].OnDestroy();
		m_roughnessTexture[0].OnDestroy();
		m_roughnessTexture[1].OnDestroy();
		m_rayLengths.OnDestroy();
		m_temporalVarianceMask.OnDestroy();
		m_tileMetaDataMask.OnDestroy();
		m_outputBuffer.OnDestroy();
	}

	void SSSR_SAMPLE_DX12::SSSR::Draw(ID3D12GraphicsCommandList* pCommandList, const SSSRConstants& sssrConstants, bool showIntersectResult)
	{
		UserMarker marker(pCommandList, "FidelityFX SSSR");

		QueryTimestamps(pCommandList);

		//Set Constantbuffer data
		D3D12_GPU_VIRTUAL_ADDRESS constantbufferAddress = m_pConstantBufferRing->AllocConstantBuffer(sizeof(SSSRConstants), (void*)&sssrConstants);

		//Render
		ID3D12DescriptorHeap* descriptorHeaps[] = { m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap() };
		pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Ensure that the ray list is in UA state
		std::vector<D3D12_RESOURCE_BARRIER> tile_ray_list_barriers = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_rayList.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(m_tileMetaDataMask.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(m_temporalDenoiserResult[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(m_roughnessTexture[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(m_temporalVarianceMask.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};

		pCommandList->ResourceBarrier((UINT)tile_ray_list_barriers.size(), &tile_ray_list_barriers[0]);

		{
			UserMarker marker(pCommandList, "Denoiser");
			{
				UserMarker marker(pCommandList, "ClassifyTiles");

				pCommandList->SetComputeRootSignature(m_ClassifyTilesPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_ClassifyTilesPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_ClassifyTilesPass.pPipeline);
				uint32_t dim_x = RoundedDivide(m_screenWidth, 8u);
				uint32_t dim_y = RoundedDivide(m_screenHeight, 8u);
				pCommandList->Dispatch(dim_x, dim_y, 1);
			}
		}

		// Ensure that the tile classification pass finished
		std::vector<D3D12_RESOURCE_BARRIER> classification_results_barriers = {
				CD3DX12_RESOURCE_BARRIER::UAV(m_rayCounter.GetResource()),
				CD3DX12_RESOURCE_BARRIER::Transition(m_rayList.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(m_intersectionPassIndirectArgs.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(m_tileMetaDataMask.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(m_roughnessTexture[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};

		pCommandList->ResourceBarrier((UINT)classification_results_barriers.size(), &classification_results_barriers[0]);

		{
			UserMarker marker(pCommandList, "PrepareIndirectArgs");

			pCommandList->SetComputeRootSignature(m_PrepareIndirectArgsPass.pRootSignature);
			pCommandList->SetComputeRootDescriptorTable(0, m_PrepareIndirectArgsPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
			pCommandList->SetPipelineState(m_PrepareIndirectArgsPass.pPipeline);
			pCommandList->Dispatch(1, 1, 1);
		}

		// Query the amount of time spent in the classifyTIles pass
		if (m_isPerformanceCountersEnabled)
		{
			auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

			assert(timestamp_queries.size() == 1ull && timestamp_queries[0] == TIMESTAMP_QUERY_INIT);

			pCommandList->EndQuery(m_pTimestampQueryHeap,
				D3D12_QUERY_TYPE_TIMESTAMP,
				GetTimestampQueryIndex());

			timestamp_queries.push_back(TIMESTAMP_QUERY_TILE_CLASSIFICATION);
		}

		// Ensure that the arguments are written
		std::vector<D3D12_RESOURCE_BARRIER> indirect_arguments_barriers = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_intersectionPassIndirectArgs.GetResource(),	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
		};
		pCommandList->ResourceBarrier((UINT)indirect_arguments_barriers.size(), &indirect_arguments_barriers[0]);

		{
			UserMarker marker(pCommandList, "Intersection pass");

			pCommandList->SetComputeRootSignature(m_IntersectPass.pRootSignature);
			pCommandList->SetComputeRootDescriptorTable(0, m_IntersectPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
			pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
			pCommandList->SetComputeRootDescriptorTable(2, m_IntersectPass.descriptorTables_Sampler[0].GetGPU());
			pCommandList->SetPipelineState(m_IntersectPass.pPipeline);
			pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), 0, nullptr, 0);
		}

		// Query the amount of time spent in the intersection pass
		if (m_isPerformanceCountersEnabled)
		{
			auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

			assert(timestamp_queries.size() == 2ull && timestamp_queries[1] == TIMESTAMP_QUERY_TILE_CLASSIFICATION);

			pCommandList->EndQuery(m_pTimestampQueryHeap,
				D3D12_QUERY_TYPE_TIMESTAMP,
				GetTimestampQueryIndex());

			timestamp_queries.push_back(TIMESTAMP_QUERY_INTERSECTION);
		}

		if (showIntersectResult)
		{
			//Copy the intersection result to the output buffer
			std::vector<D3D12_RESOURCE_BARRIER> barriers_copy_begin = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_temporalDenoiserResult[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
				CD3DX12_RESOURCE_BARRIER::Transition(m_temporalVarianceMask.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			pCommandList->ResourceBarrier((UINT)barriers_copy_begin.size(), &barriers_copy_begin[0]);

			CopyToTexture(pCommandList, m_temporalDenoiserResult[m_bufferIndex].GetResource(), m_outputBuffer.GetResource(), m_screenWidth, m_screenHeight);

			std::vector<D3D12_RESOURCE_BARRIER> barriers_copy_end = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_temporalDenoiserResult[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};

			pCommandList->ResourceBarrier((UINT)barriers_copy_end.size(), &barriers_copy_end[0]);
		}
		else
		{
			// Ensure that the arguments are written
			std::vector<D3D12_RESOURCE_BARRIER> intersection_barriers = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_temporalDenoiserResult[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};

			// Ensure that the intersection pass finished
			pCommandList->ResourceBarrier(intersection_barriers.size(), &intersection_barriers[0]);
			{
				UserMarker marker(pCommandList, "Spatial pass");
				pCommandList->SetComputeRootSignature(m_ResolveSpatialPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_ResolveSpatialPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_ResolveSpatialPass.pPipeline);
				pCommandList->Dispatch(RoundedDivide(m_screenWidth, 8u), RoundedDivide(m_screenHeight, 8u), 1);
				// Ensure that the spatial denoising pass finished. We don't have the resource for the final result available, thus we have to wait for any UAV access to finish.
				std::vector<D3D12_RESOURCE_BARRIER> spatial_barriers = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_temporalDenoiserResult[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_temporalVarianceMask.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_rayLengths.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCommandList->ResourceBarrier((UINT)spatial_barriers.size(), &spatial_barriers[0]);
			}

			// Temporal denoiser passes
			{
				UserMarker marker(pCommandList, "Temporal pass");
				pCommandList->SetComputeRootSignature(m_ResolveTemporalPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_ResolveTemporalPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_ResolveTemporalPass.pPipeline);
				pCommandList->Dispatch(RoundedDivide(m_screenWidth, 8u), RoundedDivide(m_screenHeight, 8u), 1);
				// Ensure that the temporal denoising pass finished
				std::vector<D3D12_RESOURCE_BARRIER> temporal_barriers = {
					CD3DX12_RESOURCE_BARRIER::UAV(m_temporalVarianceMask.GetResource()),
					CD3DX12_RESOURCE_BARRIER::Transition(m_temporalDenoiserResult[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_rayLengths.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_outputBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				};
				pCommandList->ResourceBarrier((UINT)temporal_barriers.size(), &temporal_barriers[0]);
			}

			// Blur pass
			{
				UserMarker marker(pCommandList, "Blur pass");
				pCommandList->SetComputeRootSignature(m_BlurPass.pRootSignature);
				pCommandList->SetComputeRootDescriptorTable(0, m_BlurPass.descriptorTables_CBV_SRV_UAV[m_bufferIndex].GetGPU());
				pCommandList->SetComputeRootConstantBufferView(1, constantbufferAddress);
				pCommandList->SetPipelineState(m_BlurPass.pPipeline);
				pCommandList->Dispatch(RoundedDivide(m_screenWidth, 8u), RoundedDivide(m_screenHeight, 8u), 1);
			}

			// Query the amount of time spent in the denoiser passes
			if (m_isPerformanceCountersEnabled)
			{
				auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

				assert(timestamp_queries.size() == 3ull && timestamp_queries[2] == TIMESTAMP_QUERY_INTERSECTION);

				pCommandList->EndQuery(m_pTimestampQueryHeap,
					D3D12_QUERY_TYPE_TIMESTAMP,
					GetTimestampQueryIndex());

				timestamp_queries.push_back(TIMESTAMP_QUERY_DENOISING);
			}

		}

		// Resolve the timestamp query data
		if (m_isPerformanceCountersEnabled)
		{
			auto const start_index = m_timestampFrameIndex * TIMESTAMP_QUERY_COUNT;

			pCommandList->ResolveQueryData(m_pTimestampQueryHeap,
				D3D12_QUERY_TYPE_TIMESTAMP,
				start_index,
				static_cast<UINT>(m_timestampQueries[m_timestampFrameIndex].size()),
				m_pTimestampQueryBuffer,
				start_index * sizeof(std::uint64_t));

			m_timestampFrameIndex = (m_timestampFrameIndex + 1u) % m_frameCountBeforeReuse;
		}
		m_bufferIndex = 1 - m_bufferIndex;
	}

	Texture* SSSR::GetOutputTexture()
	{
		return &m_outputBuffer;
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

	void SSSR::Recompile()
	{
		m_pDevice->GPUFlush();
		m_ClassifyTilesPass.DestroyPipeline();
		m_PrepareIndirectArgsPass.DestroyPipeline();
		m_IntersectPass.DestroyPipeline();
		m_BlurPass.DestroyPipeline();
		m_ResolveSpatialPass.DestroyPipeline();
		m_ResolveTemporalPass.DestroyPipeline();

		SetupClassifyTilesPass(false);
		SetupPrepareIndirectArgsPass(false);
		SetupIntersectionPass(false);
		SetupResolveSpatialPass(false);
		SetupResolveTemporalPass(false);
		SetupBlurPass(false);
	}

	void SSSR::CreateResources()
	{
		uint32_t elementSize = 4;
		//==============================Create Tile Classification-related buffers============================================
		{
			m_rayCounter.InitBuffer(m_pDevice, "SSSR - Ray Counter", &CD3DX12_RESOURCE_DESC::Buffer(2ull * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		//==============================Create PrepareIndirectArgs-related buffers============================================
		{
			m_intersectionPassIndirectArgs.InitBuffer(m_pDevice, "SSSR - Intersect Indirect Args", &CD3DX12_RESOURCE_DESC::Buffer(3ull * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
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
			auto const& sampler_state = g_blue_noise_sampler_state;
			BlueNoiseSamplerD3D12& sampler = m_blueNoiseSampler;
			sampler.sobolBuffer.InitFromMem(m_pDevice, "SSSR - Sobol Buffer", &m_uploadHeapBuffers, &sampler_state.sobol_buffer_, _countof(sampler_state.sobol_buffer_), sizeof(std::int32_t));
			sampler.rankingTileBuffer.InitFromMem(m_pDevice, "SSSR - Ranking Tile Buffer", &m_uploadHeapBuffers, &sampler_state.ranking_tile_buffer_, _countof(sampler_state.ranking_tile_buffer_), sizeof(std::int32_t));
			sampler.scramblingTileBuffer.InitFromMem(m_pDevice, "SSSR - Scrambling Tile Buffer", &m_uploadHeapBuffers, &sampler_state.scrambling_tile_buffer_, _countof(sampler_state.scrambling_tile_buffer_), sizeof(std::int32_t));
			m_uploadHeapBuffers.FlushAndFinish();
		}
	}

	void SSSR::CreateWindowSizeDependentResources()
	{
		//===================================Create Output Buffer============================================
		{
			CD3DX12_RESOURCE_DESC reflDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R11G11B10_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			m_outputBuffer.Init(m_pDevice, "Reflection Denoiser - OutputBuffer", &reflDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
		}
		uint32_t elementSize = 4;
		//==============================Create Tile Classification-related buffers============================================
		{
			UINT64 num_pixels = (UINT64)m_screenWidth * m_screenHeight;
			m_rayList.InitBuffer(m_pDevice, "SSSR - Ray List", &CD3DX12_RESOURCE_DESC::Buffer(num_pixels * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			UINT64 num_tiles = (UINT64)(RoundedDivide(m_screenWidth, 8u) * RoundedDivide(m_screenHeight, 8u));
			// one uint per tile
			m_tileMetaDataMask.InitBuffer(m_pDevice, "Reflection Denoiser - Tile Meta Data Mask", &CD3DX12_RESOURCE_DESC::Buffer(num_tiles * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			num_tiles *= 2; // one bit per pixel
			m_temporalVarianceMask.InitBuffer(m_pDevice, "Reflection Denoiser - Temporal Variance Mask", &CD3DX12_RESOURCE_DESC::Buffer(num_tiles * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		//==============================Create denoising-related resources==============================
		{
			CD3DX12_RESOURCE_DESC temporalDenoiserResult_Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R11G11B10_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC rayLengths_Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC temporalVariance_Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CD3DX12_RESOURCE_DESC roughnessTexture_Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, m_screenWidth, m_screenHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

			m_roughnessTexture[0].Init(m_pDevice, "Reflection Denoiser - Extracted Roughness Texture 0", &roughnessTexture_Desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_roughnessTexture[1].Init(m_pDevice, "Reflection Denoiser - Extracted Roughness Texture 1", &roughnessTexture_Desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_temporalDenoiserResult[0].Init(m_pDevice, "Reflection Denoiser - Temporal Denoised Result 0", &temporalDenoiserResult_Desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_temporalDenoiserResult[1].Init(m_pDevice, "Reflection Denoiser - Temporal Denoised Result 1", &temporalDenoiserResult_Desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_rayLengths.Init(m_pDevice, "Reflection Denoiser - Ray Lengths", &rayLengths_Desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
		}

		m_bufferIndex = 0;
	}

	void SSSR::SetupClassifyTilesPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_ClassifyTilesPass;
		D3D12_SHADER_BYTECODE shaderByteCode = {};
		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("ClassifyTiles.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);
		}
		//==============================Allocate Descriptor Table=========================================
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
				auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(7, &table);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[2] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange_1[2] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, 2);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = &RTSlot[0];
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
		ShaderPass& shaderpass = m_PrepareIndirectArgsPass;
		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("PrepareIndirectArgs.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);
		}
		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{

			for (size_t i = 0; i < 2; i++)
			{
				shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
				auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(2, &table);
			}
		}
		//==============================RootSignature============================================
		{
			CD3DX12_ROOT_PARAMETER RTSlot[1] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange_1[1] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = &RTSlot[0];
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
		ShaderPass& shaderpass = m_IntersectPass;
		D3D12_SHADER_BYTECODE shaderByteCode = {};
		ID3D12Device* device = m_pDevice->GetDevice();

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("Intersect.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				//Descriptor Table - CBV_SRV_UAV
				{
					shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
					auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
					m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(12, &table);
				}
				//Descriptor Table - Sampler
				{
					shaderpass.descriptorTables_Sampler.emplace_back();
					auto& table = shaderpass.descriptorTables_Sampler.back();
					m_pResourceViewHeaps->AllocSamplerDescriptor(1, &table);
				}
			}
		}
		//==============================RootSignature============================================
		{
			D3D12_STATIC_SAMPLER_DESC sampler_descs[] = { InitLinearSampler(0) }; // g_linear_sampler

			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange_1[3] = {};
			CD3DX12_DESCRIPTOR_RANGE DescRange_2[1] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 9, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 9);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);
			{
				//Param 2
				int rangeCount = 0;
				DescRange_2[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, 0, 0);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_2[0], D3D12_SHADER_VISIBILITY_ALL); // g_environment_map_sampler
			}

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = &RTSlot[0];
			descRootSignature.NumStaticSamplers = _countof(sampler_descs);
			descRootSignature.pStaticSamplers = sampler_descs;
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

	void SSSR::SetupResolveSpatialPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_ResolveSpatialPass;
		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("ResolveSpatial.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
				auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(6, &table);
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
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 5);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = &RTSlot[0];
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
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - Spatial Resolve Root Signature");

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
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - Spatial Resolve Pso");
		}
	}

	void SSSR::SetupResolveTemporalPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_ResolveTemporalPass;
		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("ResolveTemporal.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================

		//Descriptor Table - CBV_SRV_UAV
		if (allocateDescriptorTable)
		{
			for (size_t i = 0; i < 2; i++)
			{
				shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
				auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(12, &table);
			}
		}

		//Descriptor Table - Sampler
		{
			shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
			auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
		}
		//==============================RootSignature============================================
		{
			D3D12_STATIC_SAMPLER_DESC SamplerDesc = {};
			SamplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			SamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			SamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			SamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			SamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			SamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			SamplerDesc.MinLOD = 0.0f;
			SamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			SamplerDesc.MipLODBias = 0;
			SamplerDesc.MaxAnisotropy = 1;
			SamplerDesc.ShaderRegister = 0;
			SamplerDesc.RegisterSpace = 0;
			SamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

			int parameterCount = 0;
			CD3DX12_DESCRIPTOR_RANGE DescRange_1[3] = {};
			{
				//Param 0
				int rangeCount = 0;
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 10);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = &RTSlot[0];
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

	void SSSR::SetupBlurPass(bool allocateDescriptorTable)
	{
		ShaderPass& shaderpass = m_BlurPass;
		D3D12_SHADER_BYTECODE shaderByteCode = {};

		//==============================Compile Shaders============================================
		{
			DefineList defines;
			CompileShaderFromFile("BlurReflections.hlsl", &defines, "main", "-T cs_6_0 /Zi /Zss", &shaderByteCode);
		}

		//==============================DescriptorTable==========================================
		if (allocateDescriptorTable)
		{
			ID3D12Device* device = m_pDevice->GetDevice();

			//Descriptor Table - CBV_SRV_UAV
			for (size_t i = 0; i < 2; i++)
			{
				shaderpass.descriptorTables_CBV_SRV_UAV.emplace_back();
				auto& table = shaderpass.descriptorTables_CBV_SRV_UAV.back();
				m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(4, &table);
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
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0);
				DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 3);
				RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
			}
			//Param 1
			RTSlot[parameterCount++].InitAsConstantBufferView(0);

			CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
			descRootSignature.NumParameters = parameterCount;
			descRootSignature.pParameters = &RTSlot[0];
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
			CAULDRON_DX12::SetName(shaderpass.pRootSignature, "Reflection Denoiser - Blur Root Signature");

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
			CAULDRON_DX12::SetName(shaderpass.pPipeline, "Reflection Denoiser - Blur Pso");
		}
	}

	void SSSR::InitializeDescriptorTableData(const SSSRCreationInfo& input)
	{
		ID3D12Device* device = m_pDevice->GetDevice();
		Texture* normal_buffers[] = { input.NormalBuffer, input.NormalHistoryBuffer };

		for (size_t i = 0; i < 2; i++)
		{
			//==============================ClassifyTilesPass==========================================
			{
				auto& table = m_ClassifyTilesPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				input.SpecularRoughness->CreateSRV(tableSlot++, &table);
				m_temporalVarianceMask.CreateSRV(tableSlot++, &table);
				m_rayList.CreateBufferUAV(tableSlot++, nullptr, &table);
				m_rayCounter.CreateBufferUAV(tableSlot++, nullptr, &table);
				m_temporalDenoiserResult[i].CreateUAV(tableSlot++, &table);
				m_tileMetaDataMask.CreateBufferUAV(tableSlot++, nullptr, &table);
				m_roughnessTexture[i].CreateUAV(tableSlot++, &table);
			}
			//==============================PrepareIndirectArgsPass==========================================
			{
				auto& table = m_PrepareIndirectArgsPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				m_rayCounter.CreateBufferUAV(tableSlot++, nullptr, &table);
				m_intersectionPassIndirectArgs.CreateBufferUAV(tableSlot++, nullptr, &table);
			}
			//==============================IntersectionPass==========================================
			{
				auto& table = m_IntersectPass.descriptorTables_CBV_SRV_UAV[i];
				auto& table_sampler = m_IntersectPass.descriptorTables_Sampler[i];

				BlueNoiseSamplerD3D12& sampler = m_blueNoiseSampler;

				int tableSlot = 0;

				input.HDR->CreateSRV(tableSlot++, &table);
				input.DepthHierarchy->CreateSRV(tableSlot++, &table);
				normal_buffers[input.pingPongNormal ? i : 0]->CreateSRV(tableSlot++, &table);
				m_roughnessTexture[i].CreateSRV(tableSlot++, &table);
				device->CopyDescriptorsSimple(1, table.GetCPU(tableSlot++), m_environmentMapSRV.GetCPU(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // g_lit_scene
				sampler.sobolBuffer.CreateSRV(tableSlot++, &table);
				sampler.rankingTileBuffer.CreateSRV(tableSlot++, &table);
				sampler.scramblingTileBuffer.CreateSRV(tableSlot++, &table);
				m_rayList.CreateSRV(tableSlot++, &table);
				m_temporalDenoiserResult[i].CreateUAV(tableSlot++, &table);
				m_rayLengths.CreateUAV(tableSlot++, &table);
				m_rayCounter.CreateBufferUAV(tableSlot++, nullptr, &table);

				m_pDevice->GetDevice()->CreateSampler(&m_environmentMapSamplerDesc, table_sampler.GetCPU(0));
			}
			//==============================ResolveSpatial==========================================
			{
				auto& table = m_ResolveSpatialPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				input.DepthHierarchy->CreateSRV(tableSlot++, &table);
				normal_buffers[input.pingPongNormal ? i : 0]->CreateSRV(tableSlot++, &table);
				m_roughnessTexture[i].CreateSRV(tableSlot++, &table);
				m_temporalDenoiserResult[i].CreateSRV(tableSlot++, &table);
				m_tileMetaDataMask.CreateSRV(tableSlot++, &table);
				m_outputBuffer.CreateUAV(tableSlot++, &table);
			}
			//==============================ResolveTemporal==========================================
			{
				auto& table = m_ResolveTemporalPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				normal_buffers[input.pingPongNormal ? i : 0]->CreateSRV(tableSlot++, &table);
				m_roughnessTexture[i].CreateSRV(tableSlot++, &table);
				normal_buffers[input.pingPongNormal ? 1 - i : 1]->CreateSRV(tableSlot++, &table);
				m_roughnessTexture[1 - i].CreateSRV(tableSlot++, &table);
				input.DepthHierarchy->CreateSRV(tableSlot++, &table);
				input.MotionVectors->CreateSRV(tableSlot++, &table);
				m_temporalDenoiserResult[1 - i].CreateSRV(tableSlot++, &table);
				m_rayLengths.CreateSRV(tableSlot++, &table);
				m_outputBuffer.CreateSRV(tableSlot++, &table);
				m_tileMetaDataMask.CreateSRV(tableSlot++, &table);
				m_temporalDenoiserResult[i].CreateUAV(tableSlot++, &table);
				m_temporalVarianceMask.CreateBufferUAV(tableSlot++, nullptr, &table);
			}
			//==============================BlurPass==========================================
			{
				auto& table = m_BlurPass.descriptorTables_CBV_SRV_UAV[i];
				int tableSlot = 0;

				m_roughnessTexture[i].CreateSRV(tableSlot++, &table);
				m_temporalDenoiserResult[i].CreateSRV(tableSlot++, &table);
				m_tileMetaDataMask.CreateSRV(tableSlot++, &table);
				m_outputBuffer.CreateUAV(tableSlot++, &table);
			}
		}
	}

	void SSSR::SetupPerformanceCounters()
	{
		// Create timestamp querying resources if enabled
		if (m_isPerformanceCountersEnabled)
		{
			ID3D12Device* device = m_pDevice->GetDevice();

			auto const query_heap_size = TIMESTAMP_QUERY_COUNT * m_frameCountBeforeReuse * sizeof(std::uint64_t);

			D3D12_QUERY_HEAP_DESC query_heap_desc = {};
			query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			query_heap_desc.Count = static_cast<UINT>(query_heap_size);

			ThrowIfFailed(device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&m_pTimestampQueryHeap)));

			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
			heap_properties.CreationNodeMask = 1u;
			heap_properties.VisibleNodeMask = 1u;

			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Width = static_cast<UINT64>(query_heap_size);
			resource_desc.Height = 1u;
			resource_desc.DepthOrArraySize = 1u;
			resource_desc.MipLevels = 1u;
			resource_desc.SampleDesc.Count = 1u;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			ThrowIfFailed(device->CreateCommittedResource(&heap_properties,
				D3D12_HEAP_FLAG_NONE,
				&resource_desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_pTimestampQueryBuffer)));

			m_pTimestampQueryBuffer->SetName(L"TimestampQueryBuffer");
			m_timestampQueries.resize(m_frameCountBeforeReuse);
			for (auto& timestamp_queries : m_timestampQueries)
			{
				timestamp_queries.reserve(TIMESTAMP_QUERY_COUNT);
			}
		}
	}

	void SSSR::QueryTimestamps(ID3D12GraphicsCommandList* pCommandList)
	{
		// Query timestamp value prior to resolving the reflection view
		if (m_isPerformanceCountersEnabled)
		{
			auto& timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

			if (!timestamp_queries.empty())
			{
				std::uint64_t* data;

				// Reset performance counters
				m_tileClassificationElapsedGpuTicks = 0ull;
				m_denoisingElapsedGpuTicks = 0ull;
				m_intersectionElapsedGpuTicks = 0ull;

				auto const start_index = m_timestampFrameIndex * TIMESTAMP_QUERY_COUNT;

				D3D12_RANGE read_range = {};
				read_range.Begin = start_index * sizeof(std::uint64_t);
				read_range.End = (start_index + timestamp_queries.size()) * sizeof(std::uint64_t);

				m_pTimestampQueryBuffer->Map(0u,
					&read_range,
					reinterpret_cast<void**>(&data));

				for (auto i = 0u, j = 1u; j < timestamp_queries.size(); ++i, ++j)
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
						assert(false && "unrecognized timestamp query");
						break;
					}
				}

				m_pTimestampQueryBuffer->Unmap(0u, nullptr);
			}

			timestamp_queries.clear();

			pCommandList->EndQuery(m_pTimestampQueryHeap,
				D3D12_QUERY_TYPE_TIMESTAMP,
				GetTimestampQueryIndex());

			timestamp_queries.push_back(TIMESTAMP_QUERY_INIT);
		}
	}

	uint32_t SSSR::GetTimestampQueryIndex() const
	{
		return m_timestampFrameIndex * TIMESTAMP_QUERY_COUNT + static_cast<uint32_t>(m_timestampQueries[m_timestampFrameIndex].size());
	}

}