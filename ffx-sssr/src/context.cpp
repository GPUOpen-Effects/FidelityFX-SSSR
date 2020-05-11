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
#include "context.h"

#ifndef FFX_SSSR_NO_D3D12
    #include "ffx_sssr_d3d12.h"
    #include "d3d12/context_d3d12.h"
#endif // FFX_SSSR_NO_D3D12

namespace ffx_sssr
{
    /**
        The constructor for the Context class.

        \param create_context_info The context creation information.
    */
    Context::Context(FfxSssrCreateContextInfo const& create_context_info)
        : frame_index_(0u)
        , frame_count_before_reuse_(create_context_info.frameCountBeforeMemoryReuse)
        , logging_function_(create_context_info.pLoggingCallbacks ? create_context_info.pLoggingCallbacks->pfnLogging : nullptr)
        , logging_function_user_data_(create_context_info.pLoggingCallbacks ? create_context_info.pLoggingCallbacks->pUserData : nullptr)
        , api_call_("ffxSssrCreateContext")
        , reflection_view_id_dispenser_(create_context_info.maxReflectionViewCount)
        , reflection_view_view_matrices_(create_context_info.maxReflectionViewCount)
        , reflection_view_projection_matrices_(create_context_info.maxReflectionViewCount)
    {
        // Create platform-specific context(s)
#ifndef FFX_SSSR_NO_D3D12
        if (create_context_info.pD3D12CreateContextInfo)
        {
            if (!create_context_info.pD3D12CreateContextInfo->pDevice)
            {
                throw reflection_error(*this, FFX_SSSR_STATUS_INVALID_VALUE, "pDevice must not be nullptr, cannot create Direct3D12 context");
            }
            
            context_d3d12_ = std::make_unique<ContextD3D12>(*this, create_context_info);
        }
#endif // FFX_SSSR_NO_D3D12
    }

    /**
        The destructor for the Context class.
    */
    Context::~Context()
    {
    }

    /**
        Destroys the object.

        \param object_id The identifier of the object to be destroyed.
    */
    void Context::DestroyObject(std::uint64_t object_id)
    {
        if (!IsObjectValid(object_id))
        {
            return; // object was already destroyed
        }

        auto const resource_type = GetResourceType(object_id);

        switch (resource_type)
        {
        case kResourceType_ReflectionView:
            {
                reflection_view_view_matrices_.Erase(ID(object_id));
                reflection_view_projection_matrices_.Erase(ID(object_id));

#ifndef FFX_SSSR_NO_D3D12
                if (context_d3d12_)
                    context_d3d12_->reflection_views_.Erase(ID(object_id));
#endif // FFX_SSSR_NO_D3D12

                reflection_view_id_dispenser_.FreeId(object_id);
            }
            break;
        default:
            {
                FFX_SSSR_ASSERT(0);   // should never happen
            }
            break;
        }
    }

    /**
        Checks whether the object is valid.

        \param object_id The identifier of the object to be checked.
        \return true if the object is still valid, false otherwise.
    */
    bool Context::IsObjectValid(std::uint64_t object_id) const
    {
        auto const resource_type = GetResourceType(object_id);

        switch (resource_type)
        {
        case kResourceType_ReflectionView:
            {
                if (reflection_view_id_dispenser_.IsValid(object_id))
                {
                    return true;
                }
            }
            break;
        default:
            {
                FFX_SSSR_ASSERT(0);   // should never happen
            }
            break;
        }

        return false;
    }

    /**
        Creates the reflection view.

        \param reflection_view_id The identifier of the reflection view object.
        \param create_reflection_view_info The reflection view creation information.
    */
    void Context::CreateReflectionView(std::uint64_t reflection_view_id, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info)
    {
#ifndef FFX_SSSR_NO_D3D12
        if (context_d3d12_ && create_reflection_view_info.pD3D12CreateReflectionViewInfo)
            context_d3d12_->CreateReflectionView(reflection_view_id, create_reflection_view_info);
#endif // FFX_SSSR_NO_D3D12
    }

    /**
        Resolves the reflection view.

        \param reflection_view_id The identifier of the reflection view object.
        \param resolve_reflection_view_info The reflection view resolve information.
    */
    void Context::ResolveReflectionView(std::uint64_t reflection_view_id, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info)
    {
        FFX_SSSR_ASSERT(reflection_view_view_matrices_.At(ID(reflection_view_id)));   // not created properly?
        FFX_SSSR_ASSERT(reflection_view_projection_matrices_.At(ID(reflection_view_id)));

#ifndef FFX_SSSR_NO_D3D12
        context_d3d12_->ResolveReflectionView(reflection_view_id, resolve_reflection_view_info);
#endif // FFX_SSSR_NO_D3D12
    }

    /**
        Gets the number of GPU ticks spent in the tile classification pass when resolving the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent in the tile classification pass when resolving the view.
    */
    void Context::GetReflectionViewTileClassificationElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

#ifndef FFX_SSSR_NO_D3D12
        if (context_d3d12_)
            context_d3d12_->GetReflectionViewTileClassificationElapsedTime(reflection_view_id, elapsed_time);
#endif // FFX_SSSR_NO_D3D12
    }

    /**
        Gets the number of GPU ticks spent resolving the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent resolving the view.
    */
    void Context::GetReflectionViewIntersectionElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

#ifndef FFX_SSSR_NO_D3D12
        if (context_d3d12_)
            context_d3d12_->GetReflectionViewIntersectionElapsedTime(reflection_view_id, elapsed_time);
#endif // FFX_SSSR_NO_D3D12
    }

    /**
        Gets the number of GPU ticks spent denoising for the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param elapsed_time The number of GPU ticks spent denoising.
    */
    void Context::GetReflectionViewDenoisingElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

#ifndef FFX_SSSR_NO_D3D12
        if (context_d3d12_)
            context_d3d12_->GetReflectionViewDenoisingElapsedTime(reflection_view_id, elapsed_time);
#endif // FFX_SSSR_NO_D3D12
    }
}
