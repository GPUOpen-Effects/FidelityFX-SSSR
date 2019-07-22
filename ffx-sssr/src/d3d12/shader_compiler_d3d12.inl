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

namespace sssr
{
    /**
        The constructor for the ShaderD3D12 class.
    */
    ShaderD3D12::ShaderD3D12()
    {
        memset(this, 0, sizeof(*this));
    }

    /**
        The destructor for the ShaderD3D12 class.
    */
    ShaderD3D12::~ShaderD3D12()
    {
        free(const_cast<void*>(pShaderBytecode));
    }

    /**
        The constructor for the ShaderD3D12 class.

        \param other The shader to be moved.
    */
    ShaderD3D12::ShaderD3D12(ShaderD3D12&& other) noexcept
    {
        pShaderBytecode = other.pShaderBytecode;
        BytecodeLength = other.BytecodeLength;

        other.pShaderBytecode = nullptr;
    }

    /**
        Assigns the shader.

        \param other The shader to be moved.
        \return The assigned shader.
    */
    ShaderD3D12& ShaderD3D12::operator =(ShaderD3D12&& other) noexcept
    {
        if (this != &other)
        {
            pShaderBytecode = other.pShaderBytecode;
            BytecodeLength = other.BytecodeLength;

            other.pShaderBytecode = nullptr;
        }

        return *this;
    }

    /**
        Checks whether the shader is valid.

        \return true if the shader is valid, false otherwise.
    */
    ShaderD3D12::operator bool() const
    {
        return pShaderBytecode != nullptr;
    }
}
