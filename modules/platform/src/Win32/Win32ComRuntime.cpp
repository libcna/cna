// SPDX-License-Identifier: MS-PL

#include "Win32ComRuntime.hpp"

#include "Win32Common.hpp"

#include <objbase.h>

namespace CNA::Platform::Win32 {

    Win32ComRuntime::Win32ComRuntime()
    {
        // Apartment-threaded because that is what the shell dialog interfaces below require, and
        // what a UI thread already is in every host that has one.
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (result == RPC_E_CHANGED_MODE)
        {
            // The host owns this thread's apartment and chose differently. COM is usable; the
            // reference count is not ours to touch.
            usable_ = true;
            ownsReference_ = false;
            return;
        }

        usable_ = SUCCEEDED(result);
        ownsReference_ = usable_;
    }

    Win32ComRuntime::~Win32ComRuntime()
    {
        if (ownsReference_)
            CoUninitialize();
    }

} // namespace CNA::Platform::Win32
