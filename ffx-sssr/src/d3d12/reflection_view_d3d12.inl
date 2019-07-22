#include "reflection_view_d3d12.h"
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
        The constructor for the ShaderPass class.
    */
    ReflectionViewD3D12::ShaderPass::ShaderPass()
        : pipeline_state_(nullptr)
        , root_signature_(nullptr)
    {
    }

    /**
        The constructor for the ShaderPass class.

        \param other The shader pass to be moved.
    */
    ReflectionViewD3D12::ShaderPass::ShaderPass(ShaderPass&& other) noexcept
        : pipeline_state_(other.pipeline_state_)
        , root_signature_(other.root_signature_)
    {
        other.pipeline_state_ = nullptr;
        other.root_signature_ = nullptr;
    }

    /**
        The destructor for the ShaderPass class.
    */
    ReflectionViewD3D12::ShaderPass::~ShaderPass()
    {
        if (pipeline_state_)
            pipeline_state_->Release();
        pipeline_state_ = nullptr;

        if (root_signature_)
            root_signature_->Release();
        root_signature_ = nullptr;
    }

    /**
        Assigns the shader pass.

        \param other The shader pass to be moved.
        \return The assigned shader pass.
    */
    ReflectionViewD3D12::ShaderPass& ReflectionViewD3D12::ShaderPass::operator =(ShaderPass&& other) noexcept
    {
        if (this != &other)
        {
            pipeline_state_ = other.pipeline_state_;
            root_signature_ = other.root_signature_;

            other.pipeline_state_ = nullptr;
            other.root_signature_ = nullptr;
        }

        return *this;
    }

    /**
        Releases the shader pass.
    */
    inline void ReflectionViewD3D12::ShaderPass::SafeRelease()
    {
        if (pipeline_state_)
            pipeline_state_->Release();
        pipeline_state_ = nullptr;

        if (root_signature_)
            root_signature_->Release();
        root_signature_ = nullptr;
    }

    /**
        Checks whether the shader pass is valid.

        \return true if the shader pass is valid, false otherwise.
    */
    ReflectionViewD3D12::ShaderPass::operator bool() const
    {
        return (pipeline_state_ && root_signature_);
    }
}
