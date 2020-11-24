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
#include "BufferDX12.h"
namespace SSSR_SAMPLE_DX12
{
	/**
		The BlueNoiseSamplerD3D12 struct represents a blue-noise sampler to be used for random number generation.

		\note Original implementation can be found here: https://eheitzresearch.wordpress.com/762-2/
	*/
	struct BlueNoiseSamplerD3D12
	{
		// The Sobol sequence buffer.
		BufferDX12 sobolBuffer;
		// The ranking tile buffer for sampling.
		BufferDX12 rankingTileBuffer;
		// The scrambling tile buffer for sampling.
		BufferDX12 scramblingTileBuffer;

		void OnDestroy();
	};
}