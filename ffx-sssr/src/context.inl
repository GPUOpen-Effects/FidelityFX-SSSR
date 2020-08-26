#include "context.h"
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
        Creates a new reflection view.

        \param object_id The identifier of the new reflection view.
    */
    template<>
    inline void Context::CreateObject<kResourceType_ReflectionView>(std::uint64_t& object_id)
    {
        if (!CreateObject(object_id, kResourceType_ReflectionView, reflection_view_id_dispenser_))
        {
            throw reflection_error(*this, FFX_SSSR_STATUS_OUT_OF_MEMORY, "Unable to create a new reflection view resource");
        }

        // Populate the default reflection view properties
        matrix4 const identity_matrix;
        SetReflectionViewViewMatrix(object_id, identity_matrix);
        SetReflectionViewProjectionMatrix(object_id, identity_matrix);
    }

    /**
        Creates a new object.

        \param object_id The identifier of the new object.
    */
    template<ResourceType RESOURCE_TYPE>
    void Context::CreateObject(std::uint64_t& object_id)
    {
        (void)object_id;

        static_assert(0, "An unsupported resource type was supplied");
    }

    /**
        Gets the index of the current frame.

        \return The index of the current frame.
    */
    std::uint32_t& Context::GetFrameIndex()
    {
        return frame_index_;
    }

    /**
        Gets the index of the current frame.

        \return The index of the current frame.
    */
    std::uint32_t Context::GetFrameIndex() const
    {
        return frame_index_;
    }

    /**
        Gets the number of frames before memory can be re-used.

        \return The number of frames before memory can be re-used.
    */
    std::uint32_t Context::GetFrameCountBeforeReuse() const
    {
        return frame_count_before_reuse_;
    }

    /**
        Checks whether the object is of the given type.

        \param object_id The identifier of the object to be checked.
        \return true if the object is of the given type, false otherwise.
    */
    template<ResourceType RESOURCE_TYPE>
    bool Context::IsOfType(std::uint64_t object_id) const
    {
        return (GetResourceType(object_id) == RESOURCE_TYPE ? true : false);
    }

    /**
        Gets the number of objects for the given type.

        \return The number of created objects.
    */
    template<ResourceType RESOURCE_TYPE>
    std::uint32_t Context::GetObjectCount() const
    {
        switch (RESOURCE_TYPE)
        {
        case kResourceType_ReflectionView:
            return reflection_view_id_dispenser_.GetIdCount();
        default:
            {
                FFX_SSSR_ASSERT(0);   // should never happen
            }
            break;
        }

        return 0u;
    }

    /**
        Gets the maximum number of objects for the given type.

        \return The maximum number of objects.
    */
    template<ResourceType RESOURCE_TYPE>
    std::uint32_t Context::GetMaxObjectCount() const
    {
        switch (RESOURCE_TYPE)
        {
        case kResourceType_ReflectionView:
            return reflection_view_id_dispenser_.GetMaxIdCount();
        default:
            {
                FFX_SSSR_ASSERT(0);   // should never happen
            }
            break;
        }

        return 0u;
    }

    /**
        Gets the Direct3D12 context.

        \return The Direct3D12 context.
    */
    ContextD3D12* Context::GetContextD3D12()
    {
#ifdef FFX_SSSR_D3D12
        return context_d3d12_.get();
#endif // FFX_SSSR_D3D12

        return nullptr;
    }

    /**
        Gets the Direct3D12 context.

        \return The Direct3D12 context.
    */
    ContextD3D12 const* Context::GetContextD3D12() const
    {
#ifdef FFX_SSSR_D3D12
        return context_d3d12_.get();
#endif // FFX_SSSR_D3D12

        return nullptr;
    }

    /**
        Gets the Vulkan context.

        \return The Vulkan context.
    */
    inline ContextVK * Context::GetContextVK()
    {
#ifdef FFX_SSSR_VK
        return context_vk_.get();
#endif // FFX_SSSR_VK

        return nullptr;
    }

    /**
        Gets the Vulkan context.

        \return The Vulkan context.
    */
    inline ContextVK const * Context::GetContextVK() const
    {
#ifdef FFX_SSSR_VK
        return context_vk_.get();
#endif // FFX_SSSR_VK

        return nullptr;
    }

    /**
        Gets the current API call.

        \return The current API call.
    */
    char const* Context::GetAPICall() const
    {
        return (api_call_ ? api_call_ : "<unknown>");
    }

    /**
        Sets the current API call.

        \param api_call The current API call.
    */
    void Context::SetAPICall(char const* api_call)
    {
        api_call_ = api_call;
    }

    /**
        Gets the error name.

        \param error The error code to be queried.
        \return The name corresponding to the error code.
    */
    char const* Context::GetErrorName(FfxSssrStatus error)
    {
        switch (error)
        {
        case FFX_SSSR_STATUS_OK:
            return "OK";
        case FFX_SSSR_STATUS_INVALID_VALUE:
            return "Invalid value";
        case FFX_SSSR_STATUS_INVALID_OPERATION:
            return "Invalid operation";
        case FFX_SSSR_STATUS_OUT_OF_MEMORY:
            return "Out of memory";
        case FFX_SSSR_STATUS_INCOMPATIBLE_API:
            return "Incompatible API";
        case FFX_SSSR_STATUS_INTERNAL_ERROR:
            return "Internal error";
        default:
            break;
        }

        return "<unknown>";
    }

    /**
        Signals the error.

        \param error The error to be signalled.
        \param format The format for the error message.
        \param ... The content of the error message.
    */
    void Context::Error(FfxSssrStatus error, char const* format, ...) const
    {
        va_list args;
        va_start(args, format);
        Error(error, format, args);
        va_end(args);
    }

    /**
        Signals the error.

        \param error The error to be signalled.
        \param format The format for the error message.
        \param args The content of the error message.
    */
    void Context::Error(FfxSssrStatus error, char const* format, va_list args) const
    {
        char buffer[2048], message[2048];

        if (logging_function_)
        {
            snprintf(buffer, sizeof(buffer), "%s: %s (%d: %s)", GetAPICall(), format, static_cast<std::int32_t>(error), GetErrorName(error));
            vsnprintf(message, sizeof(message), buffer, args);
            logging_function_(message, logging_function_user_data_);
        }
    }

    /**
        Advances the frame index.
    */
    void Context::AdvanceToNextFrame()
    {
        ++frame_index_;
    }

    /**
        Gets the view matrix for the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param view_matrix The output value for the view matrix.
    */
    void Context::GetReflectionViewViewMatrix(std::uint64_t reflection_view_id, matrix4& view_matrix) const
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

        auto const reflection_view_view_matrix = reflection_view_view_matrices_.At(ID(reflection_view_id));

        FFX_SSSR_ASSERT(reflection_view_view_matrix); // should never happen

        view_matrix = *reflection_view_view_matrix;
    }

    /**
        Sets the view matrix for the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param view_matrix The input value for the view matrix.
    */
    void Context::SetReflectionViewViewMatrix(std::uint64_t reflection_view_id, matrix4 const& view_matrix)
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

        reflection_view_view_matrices_.Insert(ID(reflection_view_id), view_matrix);
    }

    /**
        Gets the projection matrix for the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param projection_matrix The output value for the projection matrix.
    */
    void Context::GetReflectionViewProjectionMatrix(std::uint64_t reflection_view_id, matrix4& projection_matrix) const
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

        auto const reflection_view_projection_matrix = reflection_view_projection_matrices_.At(ID(reflection_view_id));

        FFX_SSSR_ASSERT(reflection_view_projection_matrix);   // should never happen

        projection_matrix = *reflection_view_projection_matrix;
    }

    /**
        Sets the projection matrix for the reflection view.

        \param reflection_view_id The identifier for the reflection view object.
        \param projection_matrix The input value for the projection matrix.
    */
    void Context::SetReflectionViewProjectionMatrix(std::uint64_t reflection_view_id, matrix4 const& projection_matrix)
    {
        FFX_SSSR_ASSERT(IsOfType<kResourceType_ReflectionView>(reflection_view_id) && IsObjectValid(reflection_view_id));

        reflection_view_projection_matrices_.Insert(ID(reflection_view_id), projection_matrix);
    }

    /**
        Decodes the resource type from the object identifier.

        \param object_id The object identifier to be decoded.
        \return The resource type corresponding to the object.
    */
    ResourceType Context::GetResourceType(std::uint64_t object_id)
    {
        auto const resource_type = static_cast<std::uint32_t>(object_id >> 48);

        return static_cast<ResourceType>(std::min(resource_type - 1u, static_cast<std::uint32_t>(kResourceType_Count)));
    }

    /**
        Encodes the resource type into the object identifier.

        \param object_id The object identifier to be encoded.
        \param resource_type The resource type for the object.
    */
    void Context::SetResourceType(std::uint64_t& object_id, ResourceType resource_type)
    {
        FFX_SSSR_ASSERT(resource_type < kResourceType_Count);

        object_id |= ((static_cast<std::uint64_t>(resource_type) + 1ull) << 48);
    }

    /**
        Creates a new object.

        \param object_id The identifier of the new object.
        \param resource_type The resource type of the new object.
        \param id_dispenser The dispenser for allocating the object identifier.
        \return true if the object was created properly, false otherwise.
    */
    bool Context::CreateObject(std::uint64_t& object_id, ResourceType resource_type, IdDispenser& id_dispenser)
    {
        FFX_SSSR_ASSERT(resource_type < kResourceType_Count);

        if (!id_dispenser.AllocateId(object_id))
        {
            return false;
        }

        SetResourceType(object_id, resource_type);

        return true;
    }
}
