#include "context_d3d12.h"
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

namespace ffx_sssr
{
    /**
        The constructor for the ShaderKey class.
    */
    ContextD3D12::ShaderKey::ShaderKey()
        : shader_(kShader_Count)
        , switches_(0ull)
    {
    }

    /**
        The constructor for the ShaderKey class.

        \param shader The shader to be used.
    */
    ContextD3D12::ShaderKey::ShaderKey(Shader shader)
        : shader_(shader)
        , switches_(0ull)
    {
    }

    /**
        The constructor for the ShaderKey class.

        \param shader The shader to be used.
        \param switches The set of switches to be used.
    */
    ContextD3D12::ShaderKey::ShaderKey(Shader shader, std::uint64_t switches)
        : shader_(shader)
        , switches_(switches)
    {
    }

    /**
        Compares the two shader keys.

        \param other The shader key to be comparing with.
        \return true if the shader key is less than the other one.
    */
    bool ContextD3D12::ShaderKey::operator <(ShaderKey const& other) const
    {
        if (shader_ != other.shader_)
            return (shader_ < other.shader_);
        return switches_ < other.switches_;
    }

    /**
        Gets the context.

        \return The context.
    */
    Context& ContextD3D12::GetContext()
    {
        return context_;
    }

    /**
        Gets the Direct3D12 device.

        \return The Direct3D12 device.
    */
    ID3D12Device* ContextD3D12::GetDevice() const
    {
        return device_;
    }

    /**
        Gets the context.

        \return The context.
    */
    Context const& ContextD3D12::GetContext() const
    {
        return context_;
    }

    /**
        Gets hold of the upload buffer.

        \return The upload buffer.
    */
    UploadBufferD3D12& ContextD3D12::GetUploadBuffer()
    {
        return upload_buffer_;
    }

    /**
        Gets the shader.

        \param shader The shader to be retrieved.
        \param switches The set of switches to be used.
        \return The requested shader.
    */
    ShaderD3D12 const& ContextD3D12::GetShader(Shader shader) const
    {
        FFX_SSSR_ASSERT(shader < kShader_Count);
        ShaderKey const shader_key(shader, 0ull);
        auto const it = shaders_.find(shader_key);
        FFX_SSSR_ASSERT(it != shaders_.end());
        return (*it).second;
    }

    /**
        Gets a blue noise sampler with 1 sample per pixel.

        \return The requested sampler.
    */
    inline BlueNoiseSamplerD3D12 const & ContextD3D12::GetSampler1SPP() const
    {
        FFX_SSSR_ASSERT(blue_noise_sampler_1spp_.sobol_buffer_);
        FFX_SSSR_ASSERT(blue_noise_sampler_1spp_.ranking_tile_buffer_);
        FFX_SSSR_ASSERT(blue_noise_sampler_1spp_.scrambling_tile_buffer_);
        return blue_noise_sampler_1spp_;
    }

    /**
        Gets a blue noise sampler with 2 samples per pixel.

        \return The requested sampler.
    */
    inline BlueNoiseSamplerD3D12 const & ContextD3D12::GetSampler2SPP() const
    {
        FFX_SSSR_ASSERT(blue_noise_sampler_2spp_.sobol_buffer_);
        FFX_SSSR_ASSERT(blue_noise_sampler_2spp_.ranking_tile_buffer_);
        FFX_SSSR_ASSERT(blue_noise_sampler_2spp_.scrambling_tile_buffer_);
        return blue_noise_sampler_2spp_;
    }

    /**
        Gets a valid device.

        \param context The context to be used.
        \param device The Direct3D12 device.
        \return The device.
    */
    ID3D12Device* ContextD3D12::GetValidDevice(Context& context, ID3D12Device* device)
    {
        if (!device)
            throw reflection_error(context, FFX_SSSR_STATUS_INVALID_VALUE, "No device was supplied.");
        
        D3D12_FEATURE_DATA_SHADER_MODEL supportedShaderModel = {};
        supportedShaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_2;
        HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &supportedShaderModel, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL));
        if(!SUCCEEDED(hr))
            throw reflection_error(context, FFX_SSSR_STATUS_INVALID_VALUE, "Unable to check for shader model support on provided device.");

        if(supportedShaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_2)
            throw reflection_error(context, FFX_SSSR_STATUS_INVALID_VALUE, "Device does not support shader model 6.2.");

        return device;
    }

    /**
        Gets the command list.

        \param context The context to be used.
        \param command_list The Direct3D12 command list.
        \return The command list.
    */
    ID3D12GraphicsCommandList* ContextD3D12::GetCommandList(Context& context, ID3D12GraphicsCommandList* command_list)
    {
        if (!command_list)
            throw reflection_error(context, FFX_SSSR_STATUS_INVALID_VALUE, "No command list was supplied, cannot encode device commands");
        return command_list;
    }
}
