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

#include <memory>
#include <string>

#include "float3.h"
#include "memory.h"
#include "matrix4.h"
#include "reflection_error.h"
#include "resources.h"

namespace ffx_sssr
{
    class ContextD3D12;

    /**
        The Context class encapsulates the data for a single execution context.

        \note An object identifier possesses the following structure:
         - top 16 bits: resource identifier (see kResourceType_Xxx).
         - next 16 bits: generational identifier (so deleting twice does not crash).
         - bottom 32 bits: object index (for looking up attached components).
    */
    class Context
    {
        FFX_SSSR_NON_COPYABLE(Context);

    public:
        Context(FfxSssrCreateContextInfo const& create_context_info);
        ~Context();

        inline std::uint32_t& GetFrameIndex();
        inline std::uint32_t GetFrameIndex() const;
        inline std::uint32_t GetFrameCountBeforeReuse() const;

        template<ResourceType RESOURCE_TYPE>
        void CreateObject(std::uint64_t& object_id);
        void DestroyObject(std::uint64_t object_id);

        template<ResourceType RESOURCE_TYPE>
        bool IsOfType(std::uint64_t object_id) const;
        bool IsObjectValid(std::uint64_t object_id) const;

        template<ResourceType RESOURCE_TYPE>
        inline std::uint32_t GetObjectCount() const;
        template<ResourceType RESOURCE_TYPE>
        inline std::uint32_t GetMaxObjectCount() const;

        inline ContextD3D12* GetContextD3D12();
        inline ContextD3D12 const* GetContextD3D12() const;

        void CreateReflectionView(std::uint64_t reflection_view_id, FfxSssrCreateReflectionViewInfo const& create_reflection_view_info);
        void ResolveReflectionView(std::uint64_t reflection_view_id, FfxSssrResolveReflectionViewInfo const& resolve_reflection_view_info);

        inline char const* GetAPICall() const;
        inline void SetAPICall(char const* api_call);

        inline static char const* GetErrorName(FfxSssrStatus error);
        inline void Error(FfxSssrStatus error, char const* format, ...);
        inline void Error(FfxSssrStatus error, char const* format, va_list args);
        inline void AdvanceToNextFrame();

        void GetReflectionViewTileClassificationElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;
        void GetReflectionViewIntersectionElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;
        void GetReflectionViewDenoisingElapsedTime(std::uint64_t reflection_view_id, std::uint64_t& elapsed_time) const;

        inline void GetReflectionViewViewMatrix(std::uint64_t reflection_view_id, matrix4& view_matrix) const;
        inline void SetReflectionViewViewMatrix(std::uint64_t reflection_view_id, matrix4 const& view_matrix);
        inline void GetReflectionViewProjectionMatrix(std::uint64_t reflection_view_id, matrix4& projection_matrix) const;
        inline void SetReflectionViewProjectionMatrix(std::uint64_t reflection_view_id, matrix4 const& projection_matrix);

    protected:
        friend class ContextD3D12;

        static inline ResourceType GetResourceType(std::uint64_t object_id);
        static inline void SetResourceType(std::uint64_t& object_id, ResourceType resource_type);

        inline bool CreateObject(std::uint64_t& object_id, ResourceType resource_type, IdDispenser& id_dispenser);

        // The index of the current frame.
        std::uint32_t frame_index_;
        // The number of frames before memory can be re-used.
        std::uint32_t const frame_count_before_reuse_;
        // The logging function to be used to print out messages.
        PFN_ffxSssrLoggingFunction logging_function_;
        // The user data to be supplied to the logging function.
        void* logging_function_user_data_;
        // The API call that is currently being executed.
        char const* api_call_;

#ifndef FFX_SSSR_NO_D3D12
        // The Direct3D12 context object.
        std::unique_ptr<ContextD3D12> context_d3d12_;
#endif // FFX_SSSR_NO_D3D12

        // The list of reflection view identifiers.
        IdDispenser reflection_view_id_dispenser_;

        // The array of per reflection view view matrices.
        SparseArray<matrix4> reflection_view_view_matrices_;
        // The array of per reflection view projection matrices.
        SparseArray<matrix4> reflection_view_projection_matrices_;
    };
}

#include "context.inl"
