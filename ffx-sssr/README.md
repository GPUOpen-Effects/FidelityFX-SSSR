# FidelityFX SSSR

The **FidelityFX SSSR** library provides the means to render stochastic screen space reflections for the use in real-time applications.
A full sample running the library can be found on the [FidelityFX SSSR GitHub page](https://github.com/GPUOpen-Effects/FidelityFX-SSSR.git).

The library supports D3D12 and Vulkan.

## Prerequisits

The library relies on [dxcompiler.dll](https://github.com/microsoft/DirectXShaderCompiler) to generate DXIL/SPIRV from HLSL at runtime.
Use the version built for SPIRV from the [DirectXShaderCompiler GitHub repository](https://github.com/microsoft/DirectXShaderCompiler) or the one that comes with the [Vulkan SDK 1.2.141.2 (or later)](https://www.lunarg.com/vulkan-sdk/) if you are planning to use the Vulkan version of **FidelityFX SSSR**.

## Device Creation

Vulkan version only: The library relies on [VK_EXT_subgroup_size_control](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_subgroup_size_control.html) for optimal performance on RDNA. Make sure the extension is enabled at device creation by adding `VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME` to `ppEnabledExtensionNames` if it is available.
Also enable `subgroupSizeControl` in `VkPhysicalDeviceSubgroupSizeControlFeaturesEXT` and chain it into the `pNext` chain of `VkDeviceCreateInfo` if the extension name is available. It is fine to run **FidelityFX SSSR** if the extension is not supported.

## Context - Initialization and Shutdown

First the header files must be included. This is `ffx_sssr.h` for Graphics API independent definitions and `ffx_sssr_d3d12.h` for D3D12 specific definitions:

```C++
#include "ffx_sssr.h"
#include "ffx_sssr_d3d12.h"
```

or `ffx_sssr_vk.h` for Vulkan specific definitions:

```C++
#include "ffx_sssr.h"
#include "ffx_sssr_vk.h"
```

Then a context must be created. This usually is done only once per device. Depending on the preferred API, populate either  `FfxSssrD3D12CreateContextInfo` or `FfxSssrVkCreateContextInfo`.

```C++
FfxSssrD3D12CreateContextInfo d3d12ContextInfo = {};
d3d12ContextInfo.pDevice = myDevice;
d3d12ContextInfo.pUploadCommandList = myCommandList;

FfxSssrVkCreateContextInfo vkContextInfo = {};
vkContextInfo.device = myDeviceHandle;
vkContextInfo.physicalDevice = myPhysicalDeviceHandle;
vkContextInfo.uploadCommandBuffer = myUploadCommandBufferHandle;

FfxSssrLoggingCallbacks loggingCallbacks = {};
loggingCallbacks.pUserData = myUserData;
loggingCallbacks.pfnLogging = myLoggingFunction;

FfxSssrCreateContextInfo contextInfo = {};
contextInfo.apiVersion = FFX_SSSR_API_VERSION;
contextInfo.maxReflectionViewCount = myMaxViewCount;
contextInfo.frameCountBeforeMemoryReuse = myMaxFrameCountInFlight;
contextInfo.uploadBufferSize = 8 * 1024 * 1024;
contextInfo.pLoggingCallbacks = &loggingCallbacks;
contextInfo.pD3D12CreateContextInfo = &d3d12ContextInfo;
contextInfo.pVkCreateContextInfo = &vkContextInfo;
```

The library requires certain input textures from the application to create a reflection view.
Thus, the context requires user specified unpack functions (HLSL SM 6.0) to access the individual attributes. It is recommended to keep these snippets as small as possible to achieve good performance.
The function headers have to match in order for the shaders to compile. The `FFX_SSSR_*_TEXTURE_FORMAT` macros hold the definitions provided in the `p*TextureFormat` members of `FfxSssrCreateContextInfo`. The snippets provided below shall serve as a starting point:

```C++
contextInfo.pRoughnessTextureFormat = L"float4";
contextInfo.pUnpackRoughnessSnippet = L"float FfxSssrUnpackRoughness(FFX_SSSR_ROUGHNESS_TEXTURE_FORMAT packed) { return packed.w; }";
contextInfo.pNormalsTextureFormat = L"float4";
contextInfo.pUnpackNormalsSnippet = L"float3 FfxSssrUnpackNormals(FFX_SSSR_NORMALS_TEXTURE_FORMAT packed) { return 2 * packed.xyz - 1; }";
contextInfo.pSceneTextureFormat = L"float4";
contextInfo.pUnpackSceneRadianceSnippet = L"float3 FfxSssrUnpackSceneRadiance(FFX_SSSR_SCENE_TEXTURE_FORMAT packed) { return packed.xyz; }";
contextInfo.pDepthTextureFormat = L"float";
contextInfo.pUnpackDepthSnippet = L"float FfxSssrUnpackDepth(FFX_SSSR_DEPTH_TEXTURE_FORMAT packed) { return packed.x; }";
contextInfo.pMotionVectorFormat = L"float2";
contextInfo.pUnpackMotionVectorsSnippet = L"float2 FfxSssrUnpackMotionVectors(FFX_SSSR_MOTION_VECTOR_TEXTURE_FORMAT packed) { return packed.xy; }";
```

After that the context can be created:

```C++
FfxSssrContext myContext;
FfxSssrStatus status = ffxSssrCreateContext(&contextInfo, &myContext);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```
 
Finally, submit the command list provided to the `pUploadCommandList` member of `FfxSssrCreateContextInfoD3D12` to the queue of your choice to upload the internal resources to the GPU. The same is required on Vulkan for the `uploadCommandBuffer` member of `FfxSssrVkCreateContextInfo`.

Once there is no need to render reflections anymore the context should be destroyed to free internal resources:

```C++
FfxSssrStatus status = ffxSssrDestroyContext(myContext);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

## Reflection View - Creation and Update

Reflection views represent the abstraction for the first bounce of indirect light from reflective surfaces as seen from a given camera.

`FfxSssrReflectionView` resources can be created as such. Depending on the API fill either `FfxSssrD3D12CreateReflectionViewInfo` or `FfxSssrVkCreateReflectionViewInfo`:

```C++
FfxSssrD3D12CreateReflectionViewInfo d3d12ReflectionViewInfo = {};
d3d12ReflectionViewInfo.depthBufferHierarchySRV;
d3d12ReflectionViewInfo.motionBufferSRV;
d3d12ReflectionViewInfo.normalBufferSRV;
d3d12ReflectionViewInfo.roughnessBufferSRV;
d3d12ReflectionViewInfo.normalHistoryBufferSRV;
d3d12ReflectionViewInfo.roughnessHistoryBufferSRV;
d3d12ReflectionViewInfo.outputBufferUAV;
d3d12ReflectionViewInfo.sceneFormat;
d3d12ReflectionViewInfo.sceneSRV;
d3d12ReflectionViewInfo.environmentMapSRV;
d3d12ReflectionViewInfo.pEnvironmentMapSamplerDesc;

FfxSssrVkCreateReflectionViewInfo vkReflectionViewInfo = {};
vkReflectionViewInfo.depthBufferHierarchySRV;
vkReflectionViewInfo.motionBufferSRV;
vkReflectionViewInfo.normalBufferSRV;
vkReflectionViewInfo.roughnessBufferSRV;
vkReflectionViewInfo.normalHistoryBufferSRV;
vkReflectionViewInfo.roughnessHistoryBufferSRV;
vkReflectionViewInfo.reflectionViewUAV;
vkReflectionViewInfo.sceneFormat;
vkReflectionViewInfo.sceneSRV;
vkReflectionViewInfo.environmentMapSRV;
vkReflectionViewInfo.environmentMapSampler;
vkReflectionViewInfo.uploadCommandBuffer;

FfxSssrCreateReflectionViewInfo reflectionViewInfo = {};
reflectionViewInfo.flags = FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS | FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_NORMAL_BUFFERS | FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_ROUGHNESS_BUFFERS;
reflectionViewInfo.outputWidth = width;
reflectionViewInfo.outputHeight = height;
reflectionViewInfo.pD3D12CreateReflectionViewInfo = &d3d12ReflectionViewInfo;
reflectionViewInfo.pVkCreateReflectionViewInfo = &vkReflectionViewInfo;

FfxSssrReflectionView myReflectionView;
FfxSssrStatus status = ffxSssrCreateReflectionView(myContext, &reflectionViewInfo, &myReflectionView);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

On D3D12 all SRVs and UAVs must be allocated from a CPU accessible descriptor heap as they are copied into the descriptor tables of the library. `FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS` can be used if the application intends to query for timings later. The `FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_*` flags  should be set if the normal or roughness surfaces are written in an alternating fashion. Don't set the flags if the surfaces are copied each frame.

The reflection view depends on the screen size. It is recommended to destroy the reflection view on resize and create a new one:

```C++
FfxSssrStatus status = ffxSssrDestroyReflectionView(myContext, myReflectionView);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

Finally, the camera properties can be specified via the view and projection matrices. Each matrix is defined in row-major layout (i.e. the last 4 values in the float array of the view matrix are expected to be `(0, 0, 0, 1)` == the last row of the matrix):

```C++
FfxSssrStatus status = ffxSssrReflectionViewSetCameraParameters(myContext, myReflectionView, &myViewMatrix, &myProjectionMatrix);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

## Reflection View - Resolve

Calling `ffxSssrEncodeResolveReflectionView` dispatches the actual shaders that perform the hierarchical tracing through the depth buffer and optionally also dispatches the denoising passes if the `FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE` flag is set. Depending on the API populate either `FfxSssrD3D12CommandEncodeInfo` or `FfxSssrVkCommandEncodeInfo`:

```C++
FfxSssrD3D12CommandEncodeInfo d3d12EncodeInfo = {};
d3d12EncodeInfo.pCommandList = myCommandList;

FfxSssrVkCommandEncodeInfo vkEncodeInfo = {};
vkEncodeInfo.commandBuffer = myCommandBufferHandle;

FfxSssrResolveReflectionViewInfo resolveInfo = {};
resolveInfo.flags = FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE | FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_ENABLE_VARIANCE_GUIDED_TRACING;
resolveInfo.temporalStabilityScale = 0.99f;
resolveInfo.maxTraversalIterations = 128;
resolveInfo.mostDetailedDepthHierarchyMipLevel = 1;
resolveInfo.depthBufferThickness = 0.015f;
resolveInfo.minTraversalOccupancy = 4;
resolveInfo.samplesPerQuad = FFX_SSSR_RAY_SAMPLES_PER_QUAD_1;
resolveInfo.roughnessThreshold = 0.2f;
resolveInfo.pD3D12CommandEncodeInfo = &d3d12EncodeInfo;
resolveInfo.pVkCommandEncodeInfo = &vkEncodeInfo;
FfxSssrStatus status = ffxSssrEncodeResolveReflectionView(myContext, myReflectionView, &resolveInfo);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```
* Enabling `FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE` runs the libraries denoisers. Omit that flag if denoising is not required.
* Enabling `FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_ENABLE_VARIANCE_GUIDED_TRACING` counteracts temporal instabilities by shooting more rays in temporally unstable regions. 
* `resolveInfo.temporalStabilityScale` serves as a mean to trade noise with temporal stability (implies more ghosting). 
* `resolveInfo.maxTraversalIterations` limits the maximum number of intersections with the depth buffer hierarchy
* `resolveInfo.mostDetailedDepthHierarchyMipLevel` limits the most detailed mipmap for depth buffer lookups when tracing non-mirror reflection rays.
* `resolveInfo.depthBufferThickness` configures the accepted hit distance behind the depth buffer in view space.
* `resolveInfo.minTraversalOccupancy` limits the number of threads in the depth traversal loop. If less than that number of threads remain present they exit the intersection loop early even if they did not find a depth buffer intersection yet. This only affects non-mirror reflection rays. 
* `resolveInfo.samplesPerQuad` serves as a starting point how many rays are spawned in glossy regions. The only supported values are `FFX_SSSR_RAY_SAMPLES_PER_QUAD_1`, `FFX_SSSR_RAY_SAMPLES_PER_QUAD_2` and `FFX_SSSR_RAY_SAMPLES_PER_QUAD_4`. The use of `FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_ENABLE_VARIANCE_GUIDED_TRACING` dynamically bumps this up to a maximum of `4` to enforce convergence on a per pixel basis.
* `resolveInfo.roughnessThreshold` determines the roughness value below which reflection rays are spawned. Any roughness values higher are considered not reflective and the reflection view will contain `(0, 0, 0, 0)`.

When resolving a reflection view, the following operations take place:

- Reflect the view rays at the surface normal and spawn reflection rays from the depth buffer. 
- Glossy reflections are supported by randomly jittering the ray based on surface roughness. 
- The resulting radiance information is denoised using spatio-temporal filtering.
- The shading values are written out to the output buffer supplied at creation time.

Note that the application is responsible for issuing the required barrier to synchronize the writes to the output buffer.

## Reflection View - Performance Profiling

It is possible to query profiling information by enabling the `FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS` flag when creating a reflection view:

```
d3d12ReflectionViewInfo.flags |= FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS;
```

This enables the scheduling of GPU timestamp queries to track the amount of time spent in the individual passes (these are tile classification, intersection and denoising).

Note that these flags add additional runtime overhead and should be used for debugging/profiling purposes only. Set the flag to `0` to disable any timestamp queries.

The profiling information can then be queried as below for the tile classification pass:

```C++
uint64_t tileClassificationTime;
FfxSssrStatus status = ffxSssrReflectionViewGetTileClassificationElapsedTime(myContext, myReflectionView, &tileClassificationTime);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

For the intersection pass:

```C++
uint64_t intersectionTime;
FfxSssrStatus status = ffxSssrReflectionViewGetIntersectionElapsedTime(myContext, myReflectionView, &intersectionTime);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

And the same for the denoising passes:

```C++
uint64_t denoisingTime;
FfxSssrStatus status = ffxSssrReflectionViewGetDenoisingElapsedTime(myContext, myReflectionView, &denoisingTime);
if (status != FFX_SSSR_STATUS_OK) {
    // Error handling
}
```

The retrieved times are expressed in GPU ticks and can be converted using the timestamp frequency of the queue used to execute the encoded command list on D3D12 (`GetTimestampFrequency`). On Vulkan the `timestampPeriod` member of `VkPhysicalDeviceLimits` can be used to convert the times from GPU ticks to nanoseconds.

## Frame management

The **FidelityFX SSSR** library manages its own upload buffer internally that is used as a ring to transfer constant buffer data from the CPU to the GPU. The user must specify the number of frames the library should wait for before it can safely start re-using memory blocks:

```
contextInfo.frameCountBeforeMemoryReuse = myMaxFrameCountInFlight;
```

Finally, frame boundaries must be signalled to the library as such:

```C++
FfxSssrStatus status = ffxSssrAdvanceToNextFrame(myContext);
if (status != FFX_SSSR_STATUS_OK)
{
    // Error handling
}
```

Note that `ffxSssrAdvanceToNextFrame()` can be called either at the beginning or the end of a frame, but should not be called in the middle of performing work for a given frame.

## Limitations

The library assumes that the depth buffer values range from `0 --> 1` with 0 being at the near plane and 1 being at the far plane. This implies that the depth buffer hierarchy is built with the `minimum` operator. 