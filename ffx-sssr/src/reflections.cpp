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
#define SSSR_TRY                              \
    try

/**
    A define for ending a try block.

    \param ERROR The error callback.
*/
#define SSSR_CATCH(ERROR)                     \
    catch (sssr::reflection_error const& error) \
    {                                       \
        ERROR();                            \
        return error.error_;                \
    }                                       \
    catch (std::bad_alloc const&)           \
    {                                       \
        ERROR();                            \
        return SSSR_STATUS_OUT_OF_MEMORY;     \
    }                                       \
    catch (...)                             \
    {                                       \
        ERROR();                            \
        return SSSR_STATUS_INTERNAL_ERROR;    \
    }

namespace
{
    /**
        The APICall class is an RAII helper to mark the entry/exit points of the FFX_SSSR library API calls.
    */
    class APICall
    {
        SSSR_NON_COPYABLE(APICall);

    public:
        /**
            The constructor for the APICall class.

            \param context The context to be used.
            \param api_call The API call that was requested.
        */
        inline APICall(sssr::Context& context, char const* api_call)
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
        sssr::Context& context_;
    };

    /**
        A define for marking the entry/exit points of the FFX_SSSR library API calls.

        \param CTX The context being used.
        \param API_CALL The API call that was requested.
    */
    #define SSSR_API_CALL(CTX, API_CALL)  \
        APICall const _api_call_##API_CALL(*CTX, #API_CALL)
}

SssrStatus sssrCreateContext(const SssrCreateContextInfo* pCreateContextInfo, SssrContext* outContext)
{
    if (!pCreateContextInfo || !outContext)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    if (pCreateContextInfo->apiVersion != SSSR_API_VERSION)
    {
        return SSSR_STATUS_INCOMPATIBLE_API;
    }

    sssr::Context* context;

    SSSR_TRY
    {
        context = new sssr::Context(*pCreateContextInfo);

        if (!context)
        {
            return SSSR_STATUS_OUT_OF_MEMORY;
        }

        *outContext = reinterpret_cast<SssrContext>(context);
    }
    SSSR_CATCH([](){})

    context->SetAPICall(nullptr);

    return SSSR_STATUS_OK;
}

SssrStatus sssrDestroyContext(SssrContext context)
{
    if (!context)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx)
    {
        return SSSR_STATUS_OK;    // nothing to destroy
    }

    ctx->SetAPICall("sssrDestroyContext");

    delete ctx;

    return SSSR_STATUS_OK;
}

SssrStatus sssrCreateReflectionView(SssrContext context, const SssrCreateReflectionViewInfo* pCreateReflectionViewInfo, SssrReflectionView* outReflectionView)
{
    std::uint64_t reflection_view_id = 0ull;

    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !pCreateReflectionViewInfo || !outReflectionView)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    SSSR_API_CALL(ctx, sssrCreateReflectionView);

    SSSR_TRY
    {
        ctx->CreateObject<sssr::kResourceType_ReflectionView>(reflection_view_id);
        ctx->CreateReflectionView(reflection_view_id, *pCreateReflectionViewInfo);

        *outReflectionView = reinterpret_cast<SssrReflectionView>(reflection_view_id);
    }
    SSSR_CATCH([&]()
    {
        if (reflection_view_id)
        {
            ctx->DestroyObject(reflection_view_id);
        }
    })

    return SSSR_STATUS_OK;
}

SssrStatus sssrDestroyReflectionView(SssrContext context, SssrReflectionView reflectionView)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    if (!reflectionView)
    {
        return SSSR_STATUS_OK;    // nothing to delete
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrDestroyReflectionView);

    SSSR_TRY
    {
        ctx->DestroyObject(reflection_view_id);
    }
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}

SssrStatus sssrEncodeResolveReflectionView(SssrContext context, SssrReflectionView reflectionView, const SssrResolveReflectionViewInfo* pResolveReflectionViewInfo)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !pResolveReflectionViewInfo)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrEncodeResolveReflectionView);

    SSSR_TRY
    {
        ctx->ResolveReflectionView(reflection_view_id, *pResolveReflectionViewInfo);
    }
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}

SssrStatus sssrAdvanceToNextFrame(SssrContext context)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    SSSR_API_CALL(ctx, sssrAdvanceToNextFrame);

    SSSR_TRY
    {
        ctx->AdvanceToNextFrame();
    }
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}


SssrStatus sssrReflectionViewGetTileClassificationElapsedTime(SssrContext context, SssrReflectionView reflectionView, uint64_t* outTileClassificationElapsedTime)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !outTileClassificationElapsedTime)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrReflectionViewGetTileClassificationElapsedTime);

    SSSR_TRY
    {
        ctx->GetReflectionViewTileClassificationElapsedTime(reflection_view_id, *outTileClassificationElapsedTime);
    }
        SSSR_CATCH([]() {})

        return SSSR_STATUS_OK;
}

SssrStatus sssrReflectionViewGetIntersectionElapsedTime(SssrContext context, SssrReflectionView reflectionView, uint64_t* outIntersectionElapsedTime)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !outIntersectionElapsedTime)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrReflectionViewGetIntersectionElapsedTime);

    SSSR_TRY
    {
        ctx->GetReflectionViewIntersectionElapsedTime(reflection_view_id, *outIntersectionElapsedTime);
    }
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}

SssrStatus sssrReflectionViewGetDenoisingElapsedTime(SssrContext context, SssrReflectionView reflectionView, uint64_t* outDenoisingElapsedTime)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !outDenoisingElapsedTime)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrReflectionViewGetDenoisingElapsedTime);

    SSSR_TRY
    {
        ctx->GetReflectionViewDenoisingElapsedTime(reflection_view_id, *outDenoisingElapsedTime);
    }
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}

SssrStatus sssrReflectionViewGetCameraParameters(SssrContext context, SssrReflectionView reflectionView, float* outViewMatrix, float* outProjectionMatrix)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !outViewMatrix || !outProjectionMatrix)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrReflectionViewGetCameraParameters);

    SSSR_TRY
    {
        sssr::matrix4 reflection_view_view_matrix, reflection_view_projection_matrix;
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
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}

SssrStatus sssrReflectionViewSetCameraParameters(SssrContext context, SssrReflectionView reflectionView, const float* pViewMatrix, const float* pProjectionMatrix)
{
    auto const ctx = reinterpret_cast<sssr::Context*>(context);

    if (!ctx || !pViewMatrix || !pProjectionMatrix)
    {
        return SSSR_STATUS_INVALID_VALUE;
    }

    auto const reflection_view_id = reinterpret_cast<std::uint64_t>(reflectionView);

    if (!ctx->IsOfType<sssr::kResourceType_ReflectionView>(reflection_view_id) || !ctx->IsObjectValid(reflection_view_id))
    {
        return SSSR_STATUS_INVALID_VALUE; // not a valid reflection view
    }

    SSSR_API_CALL(ctx, sssrReflectionViewSetCameraParameters);

    SSSR_TRY
    {
        sssr::matrix4 reflection_view_view_matrix, reflection_view_projection_matrix;
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
    SSSR_CATCH([](){})

    return SSSR_STATUS_OK;
}