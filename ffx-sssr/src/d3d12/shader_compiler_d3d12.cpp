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
#include "shader_compiler_d3d12.h"

#include <algorithm>
#include <string>
#include <vector>

#include "reflection_error.h"
#include "utils.h"

namespace ffx_sssr
{
    /**
        The constructor for the ShaderCompilerD3D12 class.

        \param context The context to be used.
    */
    ShaderCompilerD3D12::ShaderCompilerD3D12(Context& context)
        : context_(context)
        , dxc_include_handler_(nullptr)
        , dxc_compiler_(nullptr)
        , dxc_library_(nullptr)
    {
    }

    /**
        The destructor for the ShaderCompilerD3D12 class.
    */
    ShaderCompilerD3D12::~ShaderCompilerD3D12()
    {
        if (dxc_compiler_)
            dxc_compiler_->Release();
        if (dxc_library_)
            dxc_library_->Release();
        if (dxc_include_handler_)
            dxc_include_handler_->Release();

        dxc_dll_support_.Cleanup();
    }

    /**
        Compiles the shader file.

        \param filename The location of the shader file.
        \param profile The targeted shader model.
        \param defines The list of defines to be used.
        \param define_count The number of defines.
        \return The compiled shader.
    */
    ShaderD3D12 ShaderCompilerD3D12::CompileShaderFile(char const* filename, char const* profile, LPCWSTR* arguments, std::uint32_t argument_count, DxcDefine* defines, std::uint32_t define_count)
    {
        HRESULT result;
        FFX_SSSR_ASSERT(filename && profile);

        if (!LoadShaderCompiler())
        {
            return ShaderD3D12();
        }

        // Compile the shader code from source
        IDxcBlobEncoding* dxc_source;
        auto const shader_filename = StringToWString(filename);
        result = dxc_library_->CreateBlobFromFile(shader_filename.c_str(), nullptr, &dxc_source);
        if (FAILED(result))
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_OPERATION, "Could not create shader blob from %s", filename);

        ShaderD3D12 shader = CompileShaderBlob(dxc_source, shader_filename.c_str(), profile, arguments, argument_count, defines, define_count);

        dxc_source->Release();

        return shader;
    }

    ShaderD3D12 ShaderCompilerD3D12::CompileShaderString(char const * string, std::uint32_t string_size, char const* shader_name, char const * profile, LPCWSTR * arguments, std::uint32_t argument_count, DxcDefine * defines, std::uint32_t define_count)
    {
        HRESULT result;
        FFX_SSSR_ASSERT(string && profile);

        if (!LoadShaderCompiler())
        {
            return ShaderD3D12();
        }

        IDxcBlobEncoding* dxc_source;
        result = dxc_library_->CreateBlobWithEncodingFromPinned((LPBYTE)string, string_size, 0, &dxc_source);
        if (FAILED(result))
            throw reflection_error(context_, FFX_SSSR_STATUS_INVALID_OPERATION, "Could not create blob with encoding from pinned for %s", shader_name);

        auto const wc_shader_name = StringToWString(shader_name);

        ShaderD3D12 shader = CompileShaderBlob(dxc_source, wc_shader_name.c_str(), profile, arguments, argument_count, defines, define_count);

        dxc_source->Release();

        return shader;
    }

    bool ShaderCompilerD3D12::LoadShaderCompiler()
    {
        // Load shader compiler
        if (!dxc_dll_support_.IsEnabled())
        {
            HRESULT result = dxc_dll_support_.Initialize();
            if (FAILED(result))
                throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to initialize dxcompiler.dll support");

            result = dxc_dll_support_.CreateInstance(CLSID_DxcCompiler, &dxc_compiler_);
            if (FAILED(result))
                throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to create DXC compiler instance");

            result = dxc_dll_support_.CreateInstance(CLSID_DxcLibrary, &dxc_library_);
            if (FAILED(result))
                throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to create DXC library instance");

            result = dxc_library_->CreateIncludeHandler(&dxc_include_handler_);
            if (FAILED(result))
                throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to create DXC include handler");
        }
        else if (!dxc_compiler_ || !dxc_library_)
        {
            return false;  // failed to create DXC instances
        }

        return true;
    }

    ShaderD3D12 ShaderCompilerD3D12::CompileShaderBlob(IDxcBlob * dxc_source, wchar_t const * shader_name, char const * profile, LPCWSTR * arguments, std::uint32_t argument_count, DxcDefine * defines, std::uint32_t define_count)
    {
        HRESULT result;

        std::vector<DxcDefine> resolved_defines;
        resolved_defines.reserve(define_count);

        for (uint32_t i = 0; i < define_count; ++i)
        {
            if (defines[i].Name != nullptr)
            {
                resolved_defines.push_back(defines[i]);
                if (resolved_defines.back().Value == nullptr)
                {
                    resolved_defines.back().Value = L"1";
                }
            }
        }

        ShaderD3D12 shader;
        IDxcOperationResult* dxc_result;
        auto const target_profile = StringToWString(profile);
        result = dxc_compiler_->Compile(dxc_source,
            shader_name,
            L"main",
            target_profile.c_str(),
            arguments,
            argument_count,
            resolved_defines.data(),
            static_cast<uint32_t>(resolved_defines.size()),
            dxc_include_handler_,
            &dxc_result);

        // Check for compilation errors
        if (FAILED(result))
            throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Failed to compile D3D12 shader source code");
        if (FAILED(dxc_result->GetStatus(&result)) || FAILED(result))
        {
            IDxcBlobEncoding* dxc_error;
            dxc_result->GetErrorBuffer(&dxc_error);
            std::string const error(static_cast<char const*>(dxc_error->GetBufferPointer()));
            dxc_result->Release();
            dxc_error->Release();
            throw reflection_error(context_, FFX_SSSR_STATUS_INTERNAL_ERROR, "Unable to compile shader file:\r\n> %s", error.c_str());
        }

        // Get hold of the program blob
        IDxcBlob* dxc_program = nullptr;
        dxc_result->GetResult(&dxc_program);
        FFX_SSSR_ASSERT(dxc_program != nullptr);
        dxc_result->Release();

        // Retrieve the shader bytecode
        shader.BytecodeLength = dxc_program->GetBufferSize();
        auto const shader_bytecode = malloc(shader.BytecodeLength);
        FFX_SSSR_ASSERT(shader_bytecode != nullptr);  // out of memory
        memcpy(shader_bytecode, dxc_program->GetBufferPointer(), shader.BytecodeLength);
        shader.pShaderBytecode = shader_bytecode;
        dxc_program->Release();

        return shader;
    }
}
