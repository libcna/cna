// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/ObjectDisposedException.hpp"

namespace CNA::Internal::Media
{
    /// XNA's media library types refuse every member except IsDisposed and Dispose once disposed,
    /// naming their own type (GetType().Name) with FrameworkResources.ObjectDisposedException.
    inline void ThrowIfDisposed(const bool isDisposed, const char* const typeName)
    {
        if (isDisposed)
        {
            throw System::ObjectDisposedException(typeName, "This object has already been disposed.");
        }
    }
}
