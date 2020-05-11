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

#include <d3d12.h>

#include "macros.h"
#include "ffx_sssr.h"

namespace ffx_sssr
{
    /**
        The BlueNoiseSamplerD3D12 class represents a blue-noise sampler to be used for random number generation.

        \note Original implementation can be found here: https://eheitzresearch.wordpress.com/762-2/
    */
    class BlueNoiseSamplerD3D12
    {
        FFX_SSSR_NON_COPYABLE(BlueNoiseSamplerD3D12);

    public:
        BlueNoiseSamplerD3D12();
        ~BlueNoiseSamplerD3D12();

        BlueNoiseSamplerD3D12(BlueNoiseSamplerD3D12&& other) noexcept;
        BlueNoiseSamplerD3D12& BlueNoiseSamplerD3D12::operator =(BlueNoiseSamplerD3D12&& other) noexcept;

        // The Sobol sequence buffer.
        ID3D12Resource* sobol_buffer_;
        // The ranking tile buffer for sampling.
        ID3D12Resource* ranking_tile_buffer_;
        // The scrambling tile buffer for sampling.
        ID3D12Resource* scrambling_tile_buffer_;
    };
}
