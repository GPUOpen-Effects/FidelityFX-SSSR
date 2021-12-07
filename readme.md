# FidelityFX SSSR 

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
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Stochastic Screen Space Reflections (SSSR)

Stochastic Screen Space Reflections (SSSR) is a highly optimized hierarchical screen space traversal kernel for reflections. To support glossy reflections the ray directions are randomly jittered and the result ist denoised to provide a temporally and spatially stable image. This directory contains the source code for SSSR as well as a sample demonstrating usage and integration of the library. The directory structure is as follows:

- ffx-sssr contains the [SSSR library](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/tree/master/ffx-sssr)
- sample contains the [SSSR sample](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/tree/master/sample)

You can find the binaries for FidelityFX SSSR in the release section on GitHub. 