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
#include <dxcapi.use.h>

#include "macros.h"

namespace sssr
{
    class Context;

    /**
        The ShaderD3D12 class is a simple helper for freeing the shader bytecode upon destruction.
    */
    class ShaderD3D12 : public D3D12_SHADER_BYTECODE
    {
        SSSR_NON_COPYABLE(ShaderD3D12);

    public:
        inline ShaderD3D12();
        inline ~ShaderD3D12();

        inline operator bool() const;

        inline ShaderD3D12(ShaderD3D12&& other) noexcept;
        inline ShaderD3D12& operator =(ShaderD3D12&& other) noexcept;
    };

    /**
        The ShaderCompilerD3D12 class is a utility for compiling Direct3D12 shader code.
    */
    class ShaderCompilerD3D12
    {
        SSSR_NON_COPYABLE(ShaderCompilerD3D12);

    public:
        ShaderCompilerD3D12(Context& context);
        ~ShaderCompilerD3D12();

        ShaderD3D12 CompileShaderFile(char const* filename, char const* profile, LPCWSTR* arguments = nullptr, std::uint32_t argument_count = 0, DxcDefine* defines = nullptr, std::uint32_t define_count = 0u);
        ShaderD3D12 CompileShaderString(char const* string, std::uint32_t string_size, char const* shader_name, char const* profile, LPCWSTR* arguments = nullptr, std::uint32_t argument_count = 0, DxcDefine* defines = nullptr, std::uint32_t define_count = 0u);

    protected:
        bool LoadShaderCompiler();
        ShaderD3D12 CompileShaderBlob(IDxcBlob* dxc_source, wchar_t const* shader_name, char const* profile, LPCWSTR* arguments = nullptr, std::uint32_t argument_count = 0, DxcDefine* defines = nullptr, std::uint32_t define_count = 0u);

        // The context to be used.
        Context& context_;
        // A helper for loading the dxcompiler library.
        dxc::DxcDllSupport dxc_dll_support_;
        // The Direct3D12 include handler.
        IDxcIncludeHandler* dxc_include_handler_;
        // The Direct3D12 shader compiler.
        IDxcCompiler2* dxc_compiler_;
        // The Direct3D12 shader library.
        IDxcLibrary* dxc_library_;
    };
}

#include "shader_compiler_d3d12.inl"
