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
#include "reflection_error.h"

#include "context.h"

namespace ffx_sssr
{
    /**
        The constructor for the reflection_error class.
    */
    reflection_error::reflection_error()
        : error_(FFX_SSSR_STATUS_INTERNAL_ERROR)
    {
    }

    /**
        The constructor for the reflection_error class.

        \param error The error code for this exception.
    */
    reflection_error::reflection_error(FfxSssrStatus error)
        : error_(error)
    {
    }

    /**
        The constructor for the reflection_error class.

        \param context The context to be used.
        \param error The error code for this exception.
    */
    reflection_error::reflection_error(const Context& context, FfxSssrStatus error)
        : error_(error)
    {
        (void)&context;
    }

    /**
        The constructor for the reflection_error class.

        \param context The context to be used.
        \param error The error code for this exception.
        \param format The format for the error message.
        \param ... The content of the error message.
    */
    reflection_error::reflection_error(const Context& context, FfxSssrStatus error, char const* format, ...)
        : error_(error)
    {
        va_list args;
        va_start(args, format);
        context.Error(error, format, args);
        va_end(args);
    }
}
