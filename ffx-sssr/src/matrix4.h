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

#include "float3.h"

namespace sssr
{
    /**
        The tmatrix4 class represents a generic 4x4 matrix.
    */
    template<typename TYPE>
    class tmatrix4
    {
    public:
        inline tmatrix4();
        template<typename OTHER_TYPE>
        inline tmatrix4(tmatrix4<OTHER_TYPE> const& other);

        inline tmatrix4 transpose() const;
        inline tmatrix4 operator -() const;

        inline tmatrix4& operator +=(tmatrix4 const& other);
        inline tmatrix4& operator -=(tmatrix4 const& other);
        inline tmatrix4& operator *=(tmatrix4 const& other);
        inline tmatrix4& operator *=(TYPE value);

        static inline tmatrix4 inverse(tmatrix4 const& m);

        // The underlying matrix data.
        union
        {
            TYPE m[4][4];
            struct
            {
                TYPE m00, m01, m02, m03;
                TYPE m10, m11, m12, m13;
                TYPE m20, m21, m22, m23;
                TYPE m30, m31, m32, m33;
            };
        };
    };

    /**
        A type definition for a single precision floating-point 4x4 matrix.
    */
    typedef tmatrix4<float> matrix4;

    /**
        A type definition for a double precision floating-point 4x4 matrix.
    */
    typedef tmatrix4<double> dmatrix4;
}

#include "matrix4.inl"
