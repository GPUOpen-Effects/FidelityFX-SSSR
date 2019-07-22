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
#include "sampler_d3d12.h"

namespace sssr
{
    /**
        The constructor for the SamplerD3D12 class.
    */
    BlueNoiseSamplerD3D12::BlueNoiseSamplerD3D12()
        : sobol_buffer_(nullptr)
        , ranking_tile_buffer_(nullptr)
        , scrambling_tile_buffer_(nullptr)
    {
    }

    /**
        The constructor for the SamplerD3D12 class.

        \param other The sampler to be moved.
    */
    BlueNoiseSamplerD3D12::BlueNoiseSamplerD3D12(BlueNoiseSamplerD3D12&& other) noexcept
        : sobol_buffer_(other.sobol_buffer_)
        , ranking_tile_buffer_(other.ranking_tile_buffer_)
        , scrambling_tile_buffer_(other.scrambling_tile_buffer_)
    {
        other.sobol_buffer_ = nullptr;
        other.ranking_tile_buffer_ = nullptr;
        other.scrambling_tile_buffer_ = nullptr;
    }

    /**
        The destructor for the SamplerD3D12 class.
    */
    BlueNoiseSamplerD3D12::~BlueNoiseSamplerD3D12()
    {
        if (sobol_buffer_)
            sobol_buffer_->Release();
        if (ranking_tile_buffer_)
            ranking_tile_buffer_->Release();
        if (scrambling_tile_buffer_)
            scrambling_tile_buffer_->Release();
    }

    /**
        Assigns the sampler.

        \param other The sampler to be moved.
        \return The assigned sampler.
    */
    BlueNoiseSamplerD3D12& BlueNoiseSamplerD3D12::operator =(BlueNoiseSamplerD3D12&& other) noexcept
    {
        if (this != &other)
        {
            sobol_buffer_ = other.sobol_buffer_;
            ranking_tile_buffer_ = other.ranking_tile_buffer_;
            scrambling_tile_buffer_ = other.scrambling_tile_buffer_;

            other.sobol_buffer_ = nullptr;
            other.ranking_tile_buffer_ = nullptr;
            other.scrambling_tile_buffer_ = nullptr;
        }

        return *this;
    }
}
