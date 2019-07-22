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

#include "macros.h"

namespace sssr
{
    /**
        Checks whether the value is a power of two.

        \param value The value to be checked.
        \return true if the value is a power of two, false otherwise.
    */
    template<typename TYPE>
    static inline bool IsPowerOfTwo(TYPE value)
    {
        return !(value & (value - 1));
    }

    /**
        Aligns the input value.

        \param value The value to be aligned.
        \param alignment The required alignment.
        \return The aligned value.
    */
    template<typename TYPE>
    static inline TYPE Align(TYPE value, TYPE alignment)
    {
        SSSR_ASSERT(IsPowerOfTwo(alignment));
        return (value + alignment - 1) & (~(alignment - 1));
    }

    /**
        Performs a rounded division.

        \param value The value to be divided.
        \param divisor The divisor to be used.
        \return The rounded divided value.
    */
    template<typename TYPE>
    static inline TYPE RoundedDivide(TYPE value, TYPE divisor)
    {
        return (value + divisor - 1) / divisor;
    }


    /**
        Converts the input string.

        \param input The string to be converted.
        \return The converted string.
    */
    static inline std::wstring StringToWString(std::string const& input)
    {
        std::wstring output;

        auto const length = MultiByteToWideChar(CP_ACP,
            0u,
            input.c_str(),
            static_cast<std::int32_t>(input.length() + 1u),
            nullptr,
            0);

        output.resize(static_cast<std::size_t>(length));

        MultiByteToWideChar(CP_ACP,
            0u,
            input.c_str(),
            static_cast<std::int32_t>(input.length() + 1u),
            &output[0],
            length);

        return output;
    }
}
