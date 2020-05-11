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

#include <cassert>
#include <cstdint>

#ifdef _MSC_VER

    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #undef max
    #undef min

#endif // _MSC_VER

/**
    Gets the size of a static array.

    \return The size of the static array.
*/
#define FFX_SSSR_ARRAY_SIZE(ARRAY)    \
    static_cast<std::uint32_t>(sizeof(ARRAY) / sizeof(*(ARRAY)))

/**
    Makes the type non-copyable.

    \param TYPE - The type to be made non-copyable.
*/
#define FFX_SSSR_NON_COPYABLE(TYPE)   \
    TYPE(TYPE const&) = delete; \
    TYPE& operator =(TYPE const&) = delete

#ifdef _MSC_VER

    /**
        A macro to start a do while loop.
    */
    #define FFX_SSSR_MULTI_LINE_MACRO_BEGIN                                               \
        __pragma(warning(push))                                                     \
        __pragma(warning(disable:4127)) /* conditional expression is constant */    \
        __pragma(warning(disable:4390)) /* empty controlled statement found   */    \
        do                                                                          \
        {

    /**
        A macro to end a do while loop.
    */
    #define FFX_SSSR_MULTI_LINE_MACRO_END     \
        }                               \
        while (0)                       \
        __pragma(warning(pop))

    /**
        Triggers a breakpoint.
    */
    #define FFX_SSSR_BREAKPOINT               \
        FFX_SSSR_MULTI_LINE_MACRO_BEGIN       \
            if (IsDebuggerPresent())    \
            {                           \
                DebugBreak();           \
            }                           \
        FFX_SSSR_MULTI_LINE_MACRO_END

#else // _MSC_VER

    /**
        A macro to start a do while loop.
    */
    #define FFX_SSSR_MULTI_LINE_MACRO_BEGIN   \
        do                              \
        {

    /**
        A macro to end a do while loop.
    */
    #define FFX_SSSR_MULTI_LINE_MACRO_END     \
        }                               \
        while (0)

    /**
        Triggers a breakpoint.
    */
    #define FFX_SSSR_BREAKPOINT           \
        FFX_SSSR_MULTI_LINE_MACRO_BEGIN   \
            assert(0);              \
        FFX_SSSR_MULTI_LINE_MACRO_END

#endif // _MSC_VER

#ifdef _DEBUG

    /**
        Defines a condition breakpoint that only triggers if the expression evaluates to false.

        \param expr The expression to evaluate.
    */
    #define FFX_SSSR_ASSERT(expr)         \
        FFX_SSSR_MULTI_LINE_MACRO_BEGIN   \
            if (!(expr))            \
            {                       \
                FFX_SSSR_BREAKPOINT;      \
            }                       \
        FFX_SSSR_MULTI_LINE_MACRO_END

#else // _DEBUG

    /**
        Ignores the breakpoint condition in a Release build.

        \param expr The expression to be ignored.
    */
    #define FFX_SSSR_ASSERT(expr)         \
        FFX_SSSR_MULTI_LINE_MACRO_BEGIN   \
            sizeof(expr);           \
        FFX_SSSR_MULTI_LINE_MACRO_END

#endif // _DEBUG
