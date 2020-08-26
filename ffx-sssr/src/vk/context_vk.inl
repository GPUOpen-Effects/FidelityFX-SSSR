#include "context_vk.h"
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
        Gets the context.

        \return The context.
    */
    Context& ContextVK::GetContext()
    {
        return context_;
    }

    /**
        Gets the Vulkan device.

        \return The Vulkan device.
    */
    VkDevice ContextVK::GetDevice() const
    {
        return device_;
    }


    /**
        Gets the Vulkan physical device.

        \return The Vulkan physical device.
    */
    inline VkPhysicalDevice ContextVK::GetPhysicalDevice() const
    {
        return physical_device_;
    }

    /**
        Gets the context.

        \return The context.
    */
    Context const& ContextVK::GetContext() const
    {
        return context_;
    }

    /**
        Gets hold of the upload buffer.

        \return The upload buffer.
    */
    UploadBufferVK& ContextVK::GetUploadBuffer()
    {
        return upload_buffer_;
    }

    /**
        Gets the shader.

        \param shader The shader to be retrieved.
        \param switches The set of switches to be used.
        \return The requested shader.
    */
    ShaderVK const& ContextVK::GetShader(Shader shader) const
    {
        FFX_SSSR_ASSERT(shader < kShader_Count);
        return shaders_[shader];
    }

    /**
        Gets a blue noise sampler with 1 sample per pixel.

        \return The requested sampler.
    */
    inline BlueNoiseSamplerVK const & ContextVK::GetSampler1SPP() const
    {
        return blue_noise_sampler_1spp_;
    }

    /**
        Gets a blue noise sampler with 2 samples per pixel.

        \return The requested sampler.
    */
    inline BlueNoiseSamplerVK const & ContextVK::GetSampler2SPP() const
    {
        return blue_noise_sampler_2spp_;
    }

    /**
        The constructor for the ShaderPass class.
    */
    ContextVK::ShaderPass::ShaderPass()
        : device_(VK_NULL_HANDLE)
        , pipeline_(VK_NULL_HANDLE)
        , pipeline_layout_(VK_NULL_HANDLE)
        , descriptor_set_layout_(VK_NULL_HANDLE)
        , bindings_count_(0)
    {
    }

    /**
        The constructor for the ShaderPass class.

        \param other The shader pass to be moved.
    */
    ContextVK::ShaderPass::ShaderPass(ShaderPass&& other) noexcept
        : device_(other.device_)
        , pipeline_(other.pipeline_)
        , pipeline_layout_(other.pipeline_layout_)
        , descriptor_set_layout_(other.descriptor_set_layout_)
        , bindings_count_(other.bindings_count_)
    {
        other.device_ = VK_NULL_HANDLE;
        other.pipeline_ = VK_NULL_HANDLE;
        other.pipeline_layout_ = VK_NULL_HANDLE;
        other.descriptor_set_layout_ = VK_NULL_HANDLE;
        other.bindings_count_ = 0;
    }

    /**
        The destructor for the ShaderPass class.
    */
    ContextVK::ShaderPass::~ShaderPass()
    {
        FFX_SSSR_ASSERT(device_);

        if (pipeline_)
        {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }

        if (pipeline_layout_)
        {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        }

        if (descriptor_set_layout_)
        {
            vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        pipeline_layout_ = VK_NULL_HANDLE;
        descriptor_set_layout_ = VK_NULL_HANDLE;
        bindings_count_ = 0;
    }

    /**
        Assigns the shader pass.

        \param other The shader pass to be moved.
        \return The assigned shader pass.
    */
    ContextVK::ShaderPass& ContextVK::ShaderPass::operator =(ShaderPass&& other) noexcept
    {
        if (this != &other)
        {
            device_ = other.device_;
            pipeline_ = other.pipeline_;
            pipeline_layout_ = other.pipeline_layout_;
            descriptor_set_layout_ = other.descriptor_set_layout_;
            bindings_count_ = other.bindings_count_;

            other.device_ = VK_NULL_HANDLE;
            other.pipeline_ = VK_NULL_HANDLE;
            other.pipeline_layout_ = VK_NULL_HANDLE;
            other.descriptor_set_layout_ = VK_NULL_HANDLE;
            other.bindings_count_ = 0;
        }

        return *this;
    }

    /**
        Checks whether the shader pass is valid.

        \return true if the shader pass is valid, false otherwise.
    */
    ContextVK::ShaderPass::operator bool() const
    {
        return (device_ && pipeline_ && pipeline_layout_ && descriptor_set_layout_);
    }
}
