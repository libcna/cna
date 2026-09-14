// SPDX-License-Identifier: MS-PL

#include "Win32DpiSupport.hpp"

namespace CNA::Platform::Win32 {

    namespace {

        /// MDT_EFFECTIVE_DPI. Spelled numerically so <shellscalingapi.h> -- which a MinGW sysroot
        /// need not ship -- is not a build requirement for a call that is resolved at run time
        /// anyway.
        constexpr int kMonitorDpiTypeEffective = 0;

        unsigned int DeviceContextDpi()
        {
            const HDC screen = GetDC(nullptr);
            if (screen == nullptr)
                return kDefaultDpi;
            const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
            ReleaseDC(nullptr, screen);
            return dpi > 0 ? static_cast<unsigned int>(dpi) : kDefaultDpi;
        }

    } // namespace

    Win32DpiSupport::Win32DpiSupport()
    {
        // GetModuleHandleW rather than LoadLibraryW: both modules are already loaded into every
        // GUI process, so this takes no reference and there is nothing to free.
        if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            getDpiForWindow_ =
                reinterpret_cast<GetDpiForWindowFn>(
                    reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
            getDpiForSystem_ =
                reinterpret_cast<GetDpiForSystemFn>(
                    reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForSystem")));
            adjustWindowRectExForDpi_ =
                reinterpret_cast<AdjustWindowRectExForDpiFn>(
                    reinterpret_cast<void*>(GetProcAddress(user32, "AdjustWindowRectExForDpi")));
        }

        // shcore.dll is not loaded by default, and this is the one place a reference is taken. It
        // is never freed: the pointer lives for the process lifetime in a function-local static,
        // so unloading the module would leave a dangling entry point behind.
        if (const HMODULE shcore = LoadLibraryW(L"shcore.dll"))
        {
            getDpiForMonitor_ =
                reinterpret_cast<GetDpiForMonitorFn>(
                    reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor")));
        }
    }

    const Win32DpiSupport& Win32DpiSupport::Get()
    {
        static const Win32DpiSupport instance;
        return instance;
    }

    unsigned int Win32DpiSupport::GetWindowDpi(const HWND window) const
    {
        if (window != nullptr && getDpiForWindow_ != nullptr)
        {
            if (const UINT dpi = getDpiForWindow_(window); dpi != 0)
                return dpi;
        }
        if (window != nullptr)
        {
            if (const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST))
                return GetMonitorDpi(monitor);
        }
        return GetSystemDpi();
    }

    unsigned int Win32DpiSupport::GetMonitorDpi(const HMONITOR monitor) const
    {
        if (monitor != nullptr && getDpiForMonitor_ != nullptr)
        {
            UINT dpiX = 0;
            UINT dpiY = 0;
            if (SUCCEEDED(getDpiForMonitor_(monitor, kMonitorDpiTypeEffective, &dpiX, &dpiY)) &&
                dpiX != 0)
            {
                return dpiX;
            }
        }
        return GetSystemDpi();
    }

    unsigned int Win32DpiSupport::GetSystemDpi() const
    {
        if (getDpiForSystem_ != nullptr)
        {
            if (const UINT dpi = getDpiForSystem_(); dpi != 0)
                return dpi;
        }
        return DeviceContextDpi();
    }

    bool Win32DpiSupport::AdjustWindowRect(RECT& rect, const DWORD style, const DWORD exStyle,
                                          const bool hasMenu, const unsigned int dpi) const
    {
        if (adjustWindowRectExForDpi_ != nullptr)
        {
            if (adjustWindowRectExForDpi_(&rect, style, hasMenu ? TRUE : FALSE, exStyle,
                                          dpi != 0 ? dpi : kDefaultDpi) != FALSE)
            {
                return true;
            }
        }
        return AdjustWindowRectEx(&rect, style, hasMenu ? TRUE : FALSE, exStyle) != FALSE;
    }

    float Win32DpiSupport::ToDisplayScale(const unsigned int dpi)
    {
        if (dpi == 0)
            return 1.0f;
        return static_cast<float>(dpi) / static_cast<float>(kDefaultDpi);
    }

} // namespace CNA::Platform::Win32
