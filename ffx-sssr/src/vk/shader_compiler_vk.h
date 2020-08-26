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

#include <vulkan/vulkan.h>

#include <Unknwn.h>
#include <dxcapi.use.h>

#include "macros.h"

namespace ffx_sssr
{
    class Context;

    /**
        The ShaderVK class is a simple helper for freeing the shader bytecode upon destruction.
    */
    class ShaderVK
    {
        FFX_SSSR_NON_COPYABLE(ShaderVK);

    public:
        inline ShaderVK();
        inline ~ShaderVK();

        inline operator bool() const;

        inline ShaderVK(ShaderVK&& other) noexcept;
        inline ShaderVK& operator =(ShaderVK&& other) noexcept;

        const void* pShaderBytecode;
        SIZE_T BytecodeLength;
    };

    /**
        The ShaderCompilerVK class is a utility for compiling Vulkan shader code.
    */
    class ShaderCompilerVK
    {
        FFX_SSSR_NON_COPYABLE(ShaderCompilerVK);

    public:
        ShaderCompilerVK(Context& context);
        ~ShaderCompilerVK();

        ShaderVK CompileShaderFile(char const* filename, char const* profile, LPCWSTR* arguments = nullptr, std::uint32_t argument_count = 0, DxcDefine* defines = nullptr, std::uint32_t define_count = 0u);
        ShaderVK CompileShaderString(char const* string, std::uint32_t string_size, char const* shader_name, char const* profile, LPCWSTR* arguments = nullptr, std::uint32_t argument_count = 0, DxcDefine* defines = nullptr, std::uint32_t define_count = 0u);

    protected:
        bool LoadShaderCompiler();
        ShaderVK CompileShaderBlob(IDxcBlob* dxc_source, wchar_t const* shader_name, char const* profile, LPCWSTR* arguments = nullptr, std::uint32_t argument_count = 0, DxcDefine* defines = nullptr, std::uint32_t define_count = 0u);

        // The context to be used.
        Context& context_;
        // A helper for loading the dxcompiler library.
        dxc::DxcDllSupport dxc_dll_support_;
        // The Vulkan include handler.
        IDxcIncludeHandler* dxc_include_handler_;
        // The Vulkan shader compiler.
        IDxcCompiler2* dxc_compiler_;
        // The Vulkan shader library.
        IDxcLibrary* dxc_library_;
    };
}

#include "shader_compiler_vk.inl"
