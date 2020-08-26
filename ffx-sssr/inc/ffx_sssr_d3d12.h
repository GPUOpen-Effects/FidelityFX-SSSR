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

#include <dxgi.h>
#include <d3d12.h>

/**
    The parameters for creating a Direct3D12 context.
*/
typedef struct FfxSssrD3D12CreateContextInfo
{
    ID3D12Device* pDevice;
    ID3D12GraphicsCommandList* pUploadCommandList; ///< Command list to upload static resources. The application has to synchronize to make sure the uploads are done.
} FfxSssrD3D12CreateContextInfo;

/**
    The parameters for creating a Direct3D12 reflection view.
*/
typedef struct FfxSssrD3D12CreateReflectionViewInfo
{
    DXGI_FORMAT sceneFormat; ///< The format of the sceneSRV to allow creating matching internal resources.
    D3D12_CPU_DESCRIPTOR_HANDLE sceneSRV; ///< The rendered scene without reflections. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE depthBufferHierarchySRV; ///< Full downsampled depth buffer. Each lower detail mip containing the minimum values of the higher detailed mip. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE motionBufferSRV; ///< The per pixel motion vectors. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE normalBufferSRV; ///< The surface normals in world space. Each channel mapped to [0, 1]. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE roughnessBufferSRV; ///< Perceptual roughness squared per pixel. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE normalHistoryBufferSRV; ///< Last frames normalBufferSRV. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE roughnessHistoryBufferSRV; ///< Last frames roughnessHistoryBufferSRV. The descriptor handle must be allocated on a heap allowing CPU reads.
    D3D12_CPU_DESCRIPTOR_HANDLE environmentMapSRV; ///< Environment cube map serving as a fallback for ray misses. The descriptor handle must be allocated on a heap allowing CPU reads.
    const D3D12_SAMPLER_DESC * pEnvironmentMapSamplerDesc; ///< Description for the environment map sampler.
    D3D12_CPU_DESCRIPTOR_HANDLE reflectionViewUAV; ///< The fully resolved reflection view. Make sure to synchronize for UAV writes. The descriptor handle must be allocated on a heap allowing CPU reads.
} FfxSssrD3D12CreateReflectionViewInfo;

/**
    \brief The parameters for encoding Direct3D12 device commands.
*/
typedef struct FfxSssrD3D12CommandEncodeInfo
{
    ID3D12GraphicsCommandList* pCommandList; ///< The Direct3D12 command list to be used for command encoding.
} FfxSssrD3D12CommandEncodeInfo;
