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
#include "ffx_sssr.h"

#include "context.h"

/**
    A define for starting a try block.
*/
#define FFX_SSSR_TRY                              \
    try

/**
    A define for ending a try block.

    \param ERROR The error callback.
*/
#define FFX_SSSR_CATCH(ERROR)                     \
    catch (ffx_sssr::reflection_error const& error) \
    {                                       \
        ERROR();                            \
        return error.error_;                \
    }                                       \
    catch (std::bad_alloc const&)           \
    {                                       \
        ERROR();                            \
        return FFX_SSSR_STATUS_OUT_OF_MEMORY;     \
    }                                       \
    catch (...)                             \
    {                                       \
        ERROR();                            \
        return FFX_SSSR_STATUS_INTERNAL_ERROR;    \
    }

namespace
{
    /**
        The APICall class is an RAII helper to mark the entry/exit points of the FFX_SSSR library API calls.
    */
    class APICall
    {
        FFX_SSSR_NON_COPYABLE(APICall);

    public:
        /**
            The constructor for the APICall class.

            \param context The context to be used.
            \param api_call The API call that was requested.
        */
        inline APICall(ffx_sssr::Context& context, char const* api_call)
            : context_(context)
        {
            context_.SetAPICall(api_call);
        }

        /**
            The destructor for the APICall class.
        */
        inline ~APICall()
        {
            context_.SetAPICall(nullptr);
        }

    protected:
        // The context being in use.
        ffx_sssr::Context& context_;
    };

    /**
        A define for marking the entry/exit points of the FFX_SSSR library API calls.

        \param CTX The context being used.
        \param API_CALL The API call that was requested.
    */
    #define FFX_SSSR_API_CALL(CTX, API_CALL)  \
        APICall const _api_call_##API_CALL(*CTX, #API_CALL)
}

FfxSssrStatus ffxSssrCreateContext(const FfxSssrCreateContextInfo* pCreateContextInfo, FfxSssrContext* outContext)
{
    if (!pCreateContextInfo || !outContext)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    if (pCreateContextInfo->apiVersion != FFX_SSSR_API_VERSION)
    {
        return FFX_SSSR_STATUS_INCOMPATIBLE_API;
    }

    ffx_sssr::Context* context;

    FFX_SSSR_TRY
    {
        context = new ffx_sssr::Context(*pCreateContextInfo);

        if (!context)
        {
            return FFX_SSSR_STATUS_OUT_OF_MEMORY;
        }

        *outContext = reinterpret_cast<FfxSssrContext>(context);
    }
    FFX_SSSR_CATCH([](){})

    context->SetAPICall(nullptr);

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrDestroyContext(FfxSssrContext context)
{
    if (!context)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx)
    {
        return FFX_SSSR_STATUS_OK;    // nothing to destroy
    }

    ctx->SetAPICall("ffxSssrDestroyContext");

    delete ctx;

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrCreateReflectionView(FfxSssrContext context, const FfxSssrCreateReflectionViewInfo* pCreateReflectionViewInfo, FfxSssrReflectionView* outReflectionView)
{
    std::uint64_t reflection_view_id = 0ull;

    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !pCreateReflectionViewInfo || !outReflectionView)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrCreateReflectionView);

    FFX_SSSR_TRY
    {
        ctx->CreateObject<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id);
        ctx->CreateReflectionView(reflection_view_id, *pCreateReflectionViewInfo);

        *outReflectionView = reinterpret_cast<FfxSssrReflectionView>(reflection_view_id);
    }
    FFX_SSSR_CATCH([&]()
    {
        if (reflection_view_id)
        {
            ctx->DestroyObject(reflection_view_id);
        }
    })

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrDestroyReflectionView(FfxSssrContext context, FfxSssrReflectionView reflectionView)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    if (!reflectionView)
    {
        return FFX_SSSR_STATUS_OK;    // nothing to delete
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrDestroyReflectionView);

    FFX_SSSR_TRY
    {
        ctx->DestroyObject(reflection_view_id);
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrEncodeResolveReflectionView(FfxSssrContext context, FfxSssrReflectionView reflectionView, const FfxSssrResolveReflectionViewInfo* pResolveReflectionViewInfo)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !pResolveReflectionViewInfo)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrEncodeResolveReflectionView);

    FFX_SSSR_TRY
    {
        ctx->ResolveReflectionView(reflection_view_id, *pResolveReflectionViewInfo);
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrAdvanceToNextFrame(FfxSssrContext context)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrAdvanceToNextFrame);

    FFX_SSSR_TRY
    {
        ctx->AdvanceToNextFrame();
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}


FfxSssrStatus ffxSssrReflectionViewGetTileClassificationElapsedTime(FfxSssrContext context, FfxSssrReflectionView reflectionView, uint64_t* outTileClassificationElapsedTime)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !outTileClassificationElapsedTime)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrReflectionViewGetTileClassificationElapsedTime);

    FFX_SSSR_TRY
    {
        ctx->GetReflectionViewTileClassificationElapsedTime(reflection_view_id, *outTileClassificationElapsedTime);
    }
        FFX_SSSR_CATCH([]() {})

        return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrReflectionViewGetIntersectionElapsedTime(FfxSssrContext context, FfxSssrReflectionView reflectionView, uint64_t* outIntersectionElapsedTime)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !outIntersectionElapsedTime)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrReflectionViewGetIntersectionElapsedTime);

    FFX_SSSR_TRY
    {
        ctx->GetReflectionViewIntersectionElapsedTime(reflection_view_id, *outIntersectionElapsedTime);
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrReflectionViewGetDenoisingElapsedTime(FfxSssrContext context, FfxSssrReflectionView reflectionView, uint64_t* outDenoisingElapsedTime)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !outDenoisingElapsedTime)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrReflectionViewGetDenoisingElapsedTime);

    FFX_SSSR_TRY
    {
        ctx->GetReflectionViewDenoisingElapsedTime(reflection_view_id, *outDenoisingElapsedTime);
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrReflectionViewGetCameraParameters(FfxSssrContext context, FfxSssrReflectionView reflectionView, float* outViewMatrix, float* outProjectionMatrix)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !outViewMatrix || !outProjectionMatrix)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrReflectionViewGetCameraParameters);

    FFX_SSSR_TRY
    {
        ffx_sssr::matrix4 reflection_view_view_matrix, reflection_view_projection_matrix;
        ctx->GetReflectionViewViewMatrix(reflection_view_id, reflection_view_view_matrix);
        ctx->GetReflectionViewProjectionMatrix(reflection_view_id, reflection_view_projection_matrix);

        for (auto row = 0u; row < 4u; ++row)
        {
            for (auto col = 0u; col < 4u; ++col)
            {
                outViewMatrix[4u * row + col] = reflection_view_view_matrix.m[row][col];
                outProjectionMatrix[4u * row + col] = reflection_view_projection_matrix.m[row][col];
            }
        }
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}

FfxSssrStatus ffxSssrReflectionViewSetCameraParameters(FfxSssrContext context, FfxSssrReflectionView reflectionView, const float* pViewMatrix, const float* pProjectionMatrix)
{
    auto const ctx = reinterpret_cast<ffx_sssr::Context*>(context);

    if (!ctx || !pViewMatrix || !pProjectionMatrix)
    {
        return FFX_SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<ffx_sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return FFX_SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    FFX_SSSR_API_CALL(ctx, ffxSssrReflectionViewSetCameraParameters);

    FFX_SSSR_TRY
    {
        ffx_sssr::matrix4 reflection_view_view_matrix, reflection_view_projection_matrix;
        for (auto row = 0u; row < 4u; ++row)
        {
            for (auto col = 0u; col < 4u; ++col)
            {
                reflection_view_view_matrix.m[row][col] = pViewMatrix[4u * row + col];
                reflection_view_projection_matrix.m[row][col] = pProjectionMatrix[4u * row + col];
            }
        }
        ctx->SetReflectionViewViewMatrix(reflection_view_id, reflection_view_view_matrix);
        ctx->SetReflectionViewProjectionMatrix(reflection_view_id, reflection_view_projection_matrix);
    }
    FFX_SSSR_CATCH([](){})

    return FFX_SSSR_STATUS_OK;
}