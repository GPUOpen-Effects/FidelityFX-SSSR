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

/**
    The parameters for creating a Vulkan context.
*/
typedef struct FfxSssrVkCreateContextInfo
{
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandBuffer uploadCommandBuffer; ///< Vulkan command buffer to upload static resources. The application has to begin the command buffer and has to handle synchronization to make sure the uploads are done.
} FfxSssrVkCreateContextInfo;

/**
    The parameters for creating a Vulkan reflection view.
*/
typedef struct FfxSssrVkCreateReflectionViewInfo
{
    VkFormat sceneFormat; ///< The format of the sceneSRV to allow creating matching internal resources.
    VkImageView sceneSRV; ///< The rendered scene without reflections.
    VkImageView depthBufferHierarchySRV; ///< Full downsampled depth buffer. Each lower detail mip containing the minimum values of the higher detailed mip.
    VkImageView motionBufferSRV; ///< The per pixel motion vectors.
    VkImageView normalBufferSRV; ///< The surface normals in world space. Each channel mapped to [0, 1].
    VkImageView roughnessBufferSRV; ///< Perceptual roughness squared per pixel.
    VkImageView normalHistoryBufferSRV; ///< Last frames normalBufferSRV.
    VkImageView roughnessHistoryBufferSRV; ///< Last frames roughnessHistoryBufferSRV.
    VkSampler environmentMapSampler; ///< Environment map sampler used when looking up the fallback for ray misses.
    VkImageView environmentMapSRV; ///< Environment map serving as a fallback for ray misses.
    VkImageView reflectionViewUAV; ///< The fully resolved reflection view. Make sure to synchronize for UAV writes.
    VkCommandBuffer uploadCommandBuffer; ///< Vulkan command buffer to upload static resources. The application has to begin the command buffer and has to handle synchronization to make sure the uploads are done.
} FfxSssrVkCreateReflectionViewInfo;

/**
    \brief The parameters for encoding Vulkan device commands.
*/
typedef struct FfxSssrVkCommandEncodeInfo
{
    VkCommandBuffer commandBuffer; ///< The Vulkan command buffer to be used for command encoding.
} FfxSssrVkCommandEncodeInfo;
