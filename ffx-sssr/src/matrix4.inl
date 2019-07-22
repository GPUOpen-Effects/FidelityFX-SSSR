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
        The constructor for the tmatrix4 class.
    */
    template<typename TYPE>
    tmatrix4<TYPE>::tmatrix4()
        : m00(static_cast<TYPE>(1)), m01(static_cast<TYPE>(0)), m02(static_cast<TYPE>(0)), m03(static_cast<TYPE>(0))
        , m10(static_cast<TYPE>(0)), m11(static_cast<TYPE>(1)), m12(static_cast<TYPE>(0)), m13(static_cast<TYPE>(0))
        , m20(static_cast<TYPE>(0)), m21(static_cast<TYPE>(0)), m22(static_cast<TYPE>(1)), m23(static_cast<TYPE>(0))
        , m30(static_cast<TYPE>(0)), m31(static_cast<TYPE>(0)), m32(static_cast<TYPE>(0)), m33(static_cast<TYPE>(1))
    {
    }

    /**
        The constructor for the tmatrix4 class.

        \param other The matrix to be constructing from.
    */
    template<typename TYPE>
    template<typename OTHER_TYPE>
    tmatrix4<TYPE>::tmatrix4(tmatrix4<OTHER_TYPE> const& other)
        : m00(static_cast<TYPE>(other.m00)), m01(static_cast<TYPE>(other.m01)), m02(static_cast<TYPE>(other.m02)), m03(static_cast<TYPE>(other.m03))
        , m10(static_cast<TYPE>(other.m10)), m11(static_cast<TYPE>(other.m11)), m12(static_cast<TYPE>(other.m12)), m13(static_cast<TYPE>(other.m13))
        , m20(static_cast<TYPE>(other.m20)), m21(static_cast<TYPE>(other.m21)), m22(static_cast<TYPE>(other.m22)), m23(static_cast<TYPE>(other.m23))
        , m30(static_cast<TYPE>(other.m30)), m31(static_cast<TYPE>(other.m31)), m32(static_cast<TYPE>(other.m32)), m33(static_cast<TYPE>(other.m33))
    {
    }

    /**
        Transposes the matrix.

        \return The transposed matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE> tmatrix4<TYPE>::transpose() const
    {
        tmatrix4<TYPE> result;
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
                result.m[j][i] = m[i][j];
        return result;
    }

    /**
        Negates the matrix.

        \return The negated matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE> tmatrix4<TYPE>::operator -() const
    {
        tmatrix4<TYPE> result = *this;
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
                result.m[i][j] = -m[i][j];
        return result;
    }

    /**
        Adds the matrices.

        \param other The matrix to be added.
        \return The updated matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE>& tmatrix4<TYPE>::operator +=(tmatrix4 const& other)
    {
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
                m[i][j] += other.m[i][j];
        return *this;
    }

    /**
        Subtracts the matrices.

        \param other The matrices to be subtracted.
        \return The updated matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE>& tmatrix4<TYPE>::operator -=(tmatrix4 const& other)
    {
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
                m[i][j] -= other.m[i][j];
        return *this;
    }

    /**
        Multiplies the matrices.

        \param other The matrices to be multiplied.
        \return The updated matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE>& tmatrix4<TYPE>::operator *=(tmatrix4 const& other)
    {
        tmatrix4<TYPE> temp;
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
            {
                temp.m[i][j] = static_cast<TYPE>(0);
                for (auto k = 0u; k < 4u; ++k)
                    temp.m[i][j] += m[i][k] * other.m[k][j];
            }
        *this = temp;
        return *this;
    }

    /**
        Multiplies the matrix.

        \param value The value to be multiplied with.
        \return The updated matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE>& tmatrix4<TYPE>::operator *=(TYPE value)
    {
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
                m[i][j] *= value;
        return *this;
    }

    /**
        Inverts the matrix.

        \param m The matrix to be inverted.
        \return The inverted matrix.
    */
    template<typename TYPE>
    tmatrix4<TYPE> tmatrix4<TYPE>::inverse(tmatrix4 const& m)
    {
        int indxc[4], indxr[4];
        int ipiv[4] = { 0, 0, 0, 0 };
        TYPE minv[4][4];
        tmatrix4<TYPE> temp = m;
        memcpy(minv, &temp.m[0][0], 4 * 4 * sizeof(TYPE));
        for (int i = 0; i < 4; i++) {
            int irow = -1, icol = -1;
            TYPE big = static_cast<TYPE>(0);

            // Choose pivot
            for (int j = 0; j < 4; j++) {
                if (ipiv[j] != 1) {
                    for (int k = 0; k < 4; k++) {
                        if (ipiv[k] == 0) {
                            if (std::fabs(minv[j][k]) >= big) {
                                big = std::fabs(minv[j][k]);
                                irow = j;
                                icol = k;
                            }
                        }
                        else if (ipiv[k] > 1)
                            return tmatrix4<TYPE>();
                    }
                }
            }
            ++ipiv[icol];

            // Swap rows _irow_ and _icol_ for pivot
            if (irow != icol) {
                for (int k = 0; k < 4; ++k)
                    std::swap(minv[irow][k], minv[icol][k]);
            }
            indxr[i] = irow;
            indxc[i] = icol;
            if (minv[icol][icol] == 0.)
                return matrix4();

            // Set $m[icol][icol]$ to one by scaling row _icol_ appropriately
            TYPE pivinv = static_cast<TYPE>(1) / minv[icol][icol];
            minv[icol][icol] = 1.f;
            for (int j = 0; j < 4; j++)
                minv[icol][j] *= pivinv;

            // Subtract this row from others to zero out their columns
            for (int j = 0; j < 4; j++) {
                if (j != icol) {
                    TYPE save = minv[j][icol];
                    minv[j][icol] = 0;
                    for (int k = 0; k < 4; k++)
                        minv[j][k] -= minv[icol][k] * save;
                }
            }
        }

        // Swap columns to reflect permutation
        for (int j = 3; j >= 0; j--) {
            if (indxr[j] != indxc[j]) {
                for (int k = 0; k < 4; k++)
                    std::swap(minv[k][indxr[j]], minv[k][indxc[j]]);
            }
        }

        tmatrix4<TYPE> result;
        memcpy(&result.m[0][0], minv, 4 * 4 * sizeof(TYPE));

        return result;
    }

    /**
        Adds the two matrices.

        \param m1 The LHS matrix.
        \param m2 The RHS matrix.
        \return The resulting matrix.
    */
    template<typename TYPE>
    inline tmatrix4<TYPE> operator +(tmatrix4<TYPE> const& m1, tmatrix4<TYPE> const& m2)
    {
        auto result = m1;
        return result += m2;
    }

    /**
        Subtraces the two matrices.

        \param m1 The LHS matrix.
        \param m2 The RHS matrix.
        \return The resulting matrix.
    */
    template<typename TYPE>
    inline tmatrix4<TYPE> operator -(tmatrix4<TYPE> const& m1, tmatrix4<TYPE> const& m2)
    {
        auto result = m1;
        return result -= m2;
    }

    /**
        Multiplies the two matrices.

        \param m1 The LHS matrix.
        \param m2 The RHS matrix.
        \return The resulting matrix.
    */
    template<typename TYPE>
    inline tmatrix4<TYPE> operator *(tmatrix4<TYPE> const& m1, tmatrix4<TYPE> const& m2)
    {
        tmatrix4<TYPE> result;
        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
            {
                result.m[i][j] = static_cast<TYPE>(0);
                for (auto k = 0u; k < 4u; ++k)
                    result.m[i][j] += m1.m[i][k] * m2.m[k][j];
            }
        return result;
    }

    /**
        Multiplies the matrix.

        \param m The LHS matrix.
        \param c The RHS value.
        \return The resulting matrix.
    */
    template<typename TYPE>
    inline tmatrix4<TYPE> operator *(tmatrix4<TYPE> const& m, TYPE c)
    {
        auto result = m;
        return result *= c;
    }

    /**
        Multiplies the matrix.

        \param c The LHS value.
        \param m The RHS matrix.
        \return The resulting matrix.
    */
    template<typename TYPE>
    inline tmatrix4<TYPE> operator *(TYPE c, tmatrix4<TYPE> const& m)
    {
        auto result = m;
        return result *= c;
    }

    /**
        Multiplies the vector.

        \param m The LHS matrix.
        \param v The RHS vector.
        \return The resulting vector.
    */
    template<typename TYPE>
    inline tfloat3<TYPE> operator *(tmatrix4<TYPE> const& m, tfloat3<TYPE> const& v)
    {
        tfloat3<TYPE> result;

        for (auto i = 0u; i < 4u; ++i)
            for (auto j = 0u; j < 4u; ++j)
                result[i] += m.m[i][j] * v[j];

        return result;
    }
}
