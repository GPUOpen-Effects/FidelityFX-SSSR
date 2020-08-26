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

#include <stdint.h>

#define FFX_SSSR_MAKE_VERSION(a,b,c) (((a) << 22) | ((b) << 12) | (c))

#define FFX_SSSR_API_VERSION FFX_SSSR_MAKE_VERSION(1, 1, 0)

#define FFX_SSSR_STATIC_LIBRARY

#ifndef FFX_SSSR_STATIC_LIBRARY
#ifdef WIN32
#ifdef EXPORT_API
#define FFX_SSSR_API __declspec(dllexport)
#else
#define FFX_SSSR_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#ifdef EXPORT_API
#define FFX_SSSR_API __attribute__((visibility ("default")))
#else
#define FFX_SSSR_API 
#endif
#endif
#else
#define FFX_SSSR_API 
#endif

typedef uint32_t FfxSssrFlags;

#define FFX_SSSR_DEFINE_HANDLE(object) typedef struct object##_T* object;

FFX_SSSR_DEFINE_HANDLE(FfxSssrContext)
FFX_SSSR_DEFINE_HANDLE(FfxSssrReflectionView)

/*!
    Forward declarations.
*/
typedef struct FfxSssrD3D12CreateContextInfo FfxSssrD3D12CreateContextInfo;
typedef struct FfxSssrD3D12CreateReflectionViewInfo FfxSssrD3D12CreateReflectionViewInfo;
typedef struct FfxSssrD3D12CommandEncodeInfo FfxSssrD3D12CommandEncodeInfo;
typedef struct FfxSssrVkCreateContextInfo FfxSssrVkCreateContextInfo;
typedef struct FfxSssrVkCreateReflectionViewInfo FfxSssrVkCreateReflectionViewInfo;
typedef struct FfxSssrVkCommandEncodeInfo FfxSssrVkCommandEncodeInfo;

/**
    The return codes for the API functions.
*/
enum FfxSssrStatus
{
    FFX_SSSR_STATUS_OK = 0,

    FFX_SSSR_STATUS_INVALID_VALUE     = -1,
    FFX_SSSR_STATUS_INVALID_OPERATION = -2,
    FFX_SSSR_STATUS_OUT_OF_MEMORY     = -3,
    FFX_SSSR_STATUS_INCOMPATIBLE_API  = -4,
    FFX_SSSR_STATUS_INTERNAL_ERROR    = -5
};

/** 
    The minimum number of ray samples per quad for variable rate tracing.
*/
enum FfxSssrRaySamplesPerQuad
{
    FFX_SSSR_RAY_SAMPLES_PER_QUAD_1,
    FFX_SSSR_RAY_SAMPLES_PER_QUAD_2,
    FFX_SSSR_RAY_SAMPLES_PER_QUAD_4
};

/**
    The available flags for creating a reflection view.
*/
enum FfxSssrCreateReflectionViewFlagBits
{
    FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS    = 1 << 0, ///< Set this flag if the application wishes to retrieve timing results. Don't set this flag in release builds.
    FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_NORMAL_BUFFERS = 1 << 1, ///< Set this flag if the application writes to alternate surfaces. Don't set this flag to signal that the application copies the provided normal surfaces each frame. 
    FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_PING_PONG_ROUGHNESS_BUFFERS = 1 << 2 ///< Set this flag if the application writes to alternate surfaces. Don't set this flag to signal that the application copies the provided roughness surfaces each frame. 
};
typedef FfxSssrFlags FfxSssrCreateReflectionViewFlags;

/**
    The available flags for resolving a reflection view.
*/
enum FfxSssrResolveReflectionViewFlagBits
{
    FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_DENOISE = 1 << 0, ///< Run denoiser passes on intersection results.
    FFX_SSSR_RESOLVE_REFLECTION_VIEW_FLAG_ENABLE_VARIANCE_GUIDED_TRACING = 1 << 1, ///< Enforces shooting a ray for temporally unstable pixels.
};
typedef FfxSssrFlags FfxSssrResolveReflectionViewFlags;

/**
    The callback function for logging.

    \param pMessage The message to be logged.
*/
typedef void (*PFN_ffxSssrLoggingFunction)(const char* pMessage, void* pUserData);

/**
    The callback information for logging.
*/
typedef struct FfxSssrLoggingCallbacks
{
    void* pUserData;
    PFN_ffxSssrLoggingFunction pfnLogging;
} FfxSssrLoggingCallbacks;

/**
    The parameters for creating a context.
*/
typedef struct FfxSssrCreateContextInfo
{
    uint32_t apiVersion;
    uint32_t maxReflectionViewCount;
    uint32_t frameCountBeforeMemoryReuse;
    size_t uploadBufferSize;
    const FfxSssrLoggingCallbacks* pLoggingCallbacks; ///< Can be null.
    const wchar_t* pRoughnessTextureFormat; ///< Used in the HLSL files to define the format of the resource containing surface roughness.
    const wchar_t* pUnpackRoughnessSnippet; ///< Used in the HLSL files to unpack the roughness from the provided resource.
    const wchar_t* pNormalsTextureFormat; ///< Used in the HLSL files to define the format of the resource containing the normals.
    const wchar_t* pUnpackNormalsSnippet; ///< Used in the HLSL files to unpack the normals from the provided resource.
    const wchar_t* pSceneTextureFormat; ///< Used in the HLSL files to define the format of the resource containing the rendered scene.
    const wchar_t* pUnpackSceneRadianceSnippet; ///< Used in the HLSL files to unpack the rendered scene from the provided resource.
    const wchar_t* pDepthTextureFormat; ///< Used in the HLSL files to define the format of the resource containing depth.
    const wchar_t* pUnpackDepthSnippet; ///< Used in the HLSL files to unpack the depth values from the provided resource.
    const wchar_t* pMotionVectorFormat; ///< Used in the HLSL files to define the format of the resource containing the motion vectors.
    const wchar_t* pUnpackMotionVectorsSnippet; ///< Used in the HLSL files to unpack the motion vectors from the provided resource.
    union
    {
        const FfxSssrD3D12CreateContextInfo* pD3D12CreateContextInfo;
        const FfxSssrVkCreateContextInfo* pVkCreateContextInfo;
    };
} FfxSssrCreateContextInfo;

/**
    The parameters for creating a reflection view.
*/
typedef struct FfxSssrCreateReflectionViewInfo
{
    FfxSssrCreateReflectionViewFlags flags;
    uint32_t outputWidth;
    uint32_t outputHeight;
    union
    {
        const FfxSssrD3D12CreateReflectionViewInfo* pD3D12CreateReflectionViewInfo;
        const FfxSssrVkCreateReflectionViewInfo* pVkCreateReflectionViewInfo;
    };
} FfxSssrCreateReflectionViewInfo;

/**
    The parameters for resolving a reflection view.
*/
typedef struct FfxSssrResolveReflectionViewInfo
{
    FfxSssrResolveReflectionViewFlags flags;
    float temporalStabilityScale; ///< Value between 0 and 1. High values prioritize temporal stability wheras low values avoid ghosting.
    uint32_t maxTraversalIterations; ///< Maximum number of iterations to find the intersection with the depth buffer.
    uint32_t mostDetailedDepthHierarchyMipLevel; ///< Applies only to non-mirror reflections. Mirror reflections always use 0 as most detailed mip.
    uint32_t minTraversalOccupancy; ///< Minimum number of threads per wave to keep the intersection kernel running.
    float depthBufferThickness; ///< Unit in view space. Any intersections further behind the depth buffer are rejected as invalid hits.
    FfxSssrRaySamplesPerQuad samplesPerQuad; ///< Number of samples per 4 pixels in denoised regions. Mirror reflections are not affected by this.
    float roughnessThreshold; ///< Shoot reflection rays for roughness values that are lower than this threshold.
    union
    {
        const FfxSssrD3D12CommandEncodeInfo* pD3D12CommandEncodeInfo; ///< A pointer to the Direct3D12 command encoding parameters.
        const FfxSssrVkCommandEncodeInfo* pVkCommandEncodeInfo; ///< A pointer to the Vulkan command encoding parameters.
    };
} FfxSssrResolveReflectionViewInfo;

// API functions
#ifdef __cplusplus
extern "C"
{
#endif
    /**
        Creates a new context.

        \param pCreateContextInfo The context creation information.
        \param outContext The context.
        \return The corresponding error code.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrCreateContext(const FfxSssrCreateContextInfo* pCreateContextInfo, FfxSssrContext* outContext);

    /**
        Destroys the context.

        \param context The context to be destroyed.
        \return The corresponding error code.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrDestroyContext(FfxSssrContext context);

    /**
        Creates a new reflection view.

        \param context The context to be used.
        \param pCreateReflectionViewInfo The reflection view creation information.
        \param outReflectionView The reflection view resource.
        \return The corresponding error code.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrCreateReflectionView(FfxSssrContext context, const FfxSssrCreateReflectionViewInfo* pCreateReflectionViewInfo, FfxSssrReflectionView* outReflectionView);

    /**
        Destroys the reflection view.

        \param context The context to be used.
        \param reflectionView The reflection view resource.
        \return The corresponding error code.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrDestroyReflectionView(FfxSssrContext context, FfxSssrReflectionView reflectionView);

    /**
        Encodes the command(s) for resolving the given reflection view.

        \param context The context to be used.
        \param reflectionView The resource for the reflection view.
        \param pResolveReflectionViewInfo The reflection view information.
        \return The corresponding error code.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrEncodeResolveReflectionView(FfxSssrContext context, FfxSssrReflectionView reflectionView, const FfxSssrResolveReflectionViewInfo* pResolveReflectionViewInfo);

    /**
        Advances the frame index.

        \param context The context to be used.
        \return The corresponding error code.

        \note Please call this once a frame so the library is able to safely re-use memory blocks after frameCountBeforeMemoryReuse frames have passed.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrAdvanceToNextFrame(FfxSssrContext context);

    /**
        Gets the number of GPU ticks spent in the tile classification pass.

        \param context The context to be used.
        \param reflectionView The resource for the reflection view.
        \param outTileClassificationElapsedTime The number of GPU ticks spent in the tile classification pass.
        \return The corresponding error code.

        \note This method will only function if the reflection view was created with the FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS flag.
              Also, note that it will actually return the time that was spent in the tile classification pass frameCountBeforeMemoryReuse frames ago.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrReflectionViewGetTileClassificationElapsedTime(FfxSssrContext context, FfxSssrReflectionView reflectionView, uint64_t* outTileClassificationElapsedTime);


    /**
        Gets the number of GPU ticks spent intersecting reflection rays with the depth buffer.

        \param context The context to be used.
        \param reflectionView The resource for the reflection view.
        \param outIntersectionElapsedTime The number of GPU ticks spent intersecting reflection rays with the depth buffer.
        \return The corresponding error code.

        \note This method will only function if the reflection view was created with the FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS flag.
              Also, note that it will actually return the time that was spent resolving frameCountBeforeMemoryReuse frames ago.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrReflectionViewGetIntersectionElapsedTime(FfxSssrContext context, FfxSssrReflectionView reflectionView, uint64_t* outIntersectionElapsedTime);

    /**
        Gets the number of GPU ticks spent denoising.

        \param context The context to be used.
        \param reflectionView The resource for the reflection view.
        \param outDenoisingElapsedTime The number of GPU ticks spent denoising.
        \return The corresponding error code.

        \note This method will only function if the reflection view was created with the FFX_SSSR_CREATE_REFLECTION_VIEW_FLAG_ENABLE_PERFORMANCE_COUNTERS flag.
              Also, note that it will actually return the time that was spent denoising frameCountBeforeMemoryReuse frames ago.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrReflectionViewGetDenoisingElapsedTime(FfxSssrContext context, FfxSssrReflectionView reflectionView, uint64_t* outDenoisingElapsedTime);

    /**
        Gets the view and projection matrices for the reflection view.

        \param context The context to be used.
        \param reflectionView The resource for the reflection view.
        \param outViewMatrix The output value for the view matrix.
        \param outProjectionMatrix The output value for the projection matrix.
        \return The corresponding error code.

        \note The output matrices will be 4x4 row-major matrices.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrReflectionViewGetCameraParameters(FfxSssrContext context, FfxSssrReflectionView reflectionView, float* outViewMatrix, float* outProjectionMatrix);

    /**
        Sets the view and projection matrices for the reflection view.

        \param context The context to be used.
        \param reflectionView The resource for the reflection view.
        \param pViewMatrix The input value for the view matrix.
        \param pProjectionMatrix The input value for the projection matrix.
        \return The corresponding error code.

        \note The input matrices are expected to be 4x4 row-major matrices.
    */
    FFX_SSSR_API FfxSssrStatus ffxSssrReflectionViewSetCameraParameters(FfxSssrContext context, FfxSssrReflectionView reflectionView, const float* pViewMatrix, const float* pProjectionMatrix);

#ifdef __cplusplus
}
#endif
