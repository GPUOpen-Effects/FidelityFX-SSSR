#include "float3.h"
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

namespace sssr
{
    /**
        The constructor for the tfloat3 class.
    */
    template<typename TYPE>
    tfloat3<TYPE>::tfloat3()
        : x(static_cast<TYPE>(0))
        , y(static_cast<TYPE>(0))
        , z(static_cast<TYPE>(0))
        , w(static_cast<TYPE>(0))
    {
    }

    /**
        The constructor for the tfloat3 class.

        \param v The value for initializing the vector.
    */
    template<typename TYPE>
    tfloat3<TYPE>::tfloat3(TYPE v)
        : x(v)
        , y(v)
        , z(v)
        , w(v)
    {
    }

    /**
        The constructor for the tfloat3 class.

        \param x The vector X component.
        \param y The vector Y component.
        \param z The vector Z component.
        \param w The vector W component.
    */
    template<typename TYPE>
    tfloat3<TYPE>::tfloat3(TYPE x, TYPE y, TYPE z, TYPE w)
        : x(x)
        , y(y)
        , z(z)
        , w(w)
    {
    }

    /**
        The constructor for the tfloat3 class.

        \param other The vector to be constructing from.
    */
    template<typename TYPE>
    template<typename OTHER_TYPE>
    tfloat3<TYPE>::tfloat3(tfloat3<OTHER_TYPE> const& other)
        : x(static_cast<TYPE>(other.x))
        , y(static_cast<TYPE>(other.y))
        , z(static_cast<TYPE>(other.z))
        , w(static_cast<TYPE>(other.w))
    {
    }

    /**
        Gets the negative vector.

        \return The negative vector.
    */
    template<typename TYPE>
    tfloat3<TYPE> tfloat3<TYPE>::operator -() const
    {
        return tfloat3<TYPE>(-x, -y, -z);
    }

    /**
        Divides each component by the provided number.

        \return The resulting vector.
    */
    template<typename TYPE>
    inline tfloat3<TYPE> tfloat3<TYPE>::operator/(TYPE f) const
    {
        return tfloat3<TYPE>(x / f, y / f, z / f, w / f);
    }

    /**
        Gets the given vector component.

        \param i The index of the vector component.
        \return The requested vector component.
    */
    template<typename TYPE>
    TYPE& tfloat3<TYPE>::operator [](std::uint32_t i)
    {
        return *(&x + i);
    }

    /**
        Gets the given vector component.

        \param i The index of the vector component.
        \return The requested vector component.
    */
    template<typename TYPE>
    TYPE tfloat3<TYPE>::operator [](std::uint32_t i) const
    {
        return *(&x + i);
    }

    /**
        Calculates the squared norm of the vector.

        \return The squared norm of the vector.
    */
    template<typename TYPE>
    TYPE tfloat3<TYPE>::sqnorm() const
    {
        return x * x + y * y + z * z;
    }

    /**
        Calculates the norm of the vector.

        \return The norm of the vector.
    */
    template<typename TYPE>
    TYPE tfloat3<TYPE>::norm() const
    {
        return std::sqrt(sqnorm());
    }

    /**
        Normalizes the input vector.

        \param v The vector to be normalized.
        \return The normalized vector.
    */
    template<typename TYPE>
    tfloat3<TYPE> tfloat3<TYPE>::normalize(tfloat3 const& v)
    {
        auto result = v;
        auto const norm_inv = static_cast<TYPE>(1) / v.norm();
        result.x *= norm_inv;
        result.y *= norm_inv;
        result.z *= norm_inv;
        result.w *= norm_inv;
        return result;
    }
}
