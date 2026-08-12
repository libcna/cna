// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/NativeWindowHandle.hpp"

namespace CNA::Platform {

    bool TryGetWin32(const NativeWindowHandle& handle, Win32NativeWindow& out)
    {
        if (handle.system != NativeWindowSystem::Win32 || handle.window == nullptr)
        {
            return false;
        }
        out.hwnd = handle.window;
        return true;
    }

    bool TryGetX11(const NativeWindowHandle& handle, X11NativeWindow& out)
    {
        // windowId, not window: an X11 Window is an XID, and XID 0 (None) is never a valid
        // drawable, so zero is the correct "unset" test here rather than a null pointer check.
        if (handle.system != NativeWindowSystem::X11 || handle.display == nullptr || handle.windowId == 0)
        {
            return false;
        }
        out.display = handle.display;
        out.window = handle.windowId;
        return true;
    }

    bool TryGetWayland(const NativeWindowHandle& handle, WaylandNativeWindow& out)
    {
        if (handle.system != NativeWindowSystem::Wayland || handle.display == nullptr || handle.surface == nullptr)
        {
            return false;
        }
        out.display = handle.display;
        out.surface = handle.surface;
        return true;
    }

    bool TryGetCocoa(const NativeWindowHandle& handle, CocoaNativeWindow& out)
    {
        if (handle.system != NativeWindowSystem::Cocoa || handle.window == nullptr)
        {
            return false;
        }
        out.window = handle.window;
        return true;
    }

    bool TryGetAndroid(const NativeWindowHandle& handle, AndroidNativeWindow& out)
    {
        if (handle.system != NativeWindowSystem::Android || handle.window == nullptr)
        {
            return false;
        }
        out.window = handle.window;
        return true;
    }

    bool HasNativeWindow(const NativeWindowHandle& handle)
    {
        switch (handle.system)
        {
            case NativeWindowSystem::Win32:
            case NativeWindowSystem::Cocoa:
            case NativeWindowSystem::Android:
                return handle.window != nullptr;
            case NativeWindowSystem::X11:
                return handle.display != nullptr && handle.windowId != 0;
            case NativeWindowSystem::Wayland:
                return handle.display != nullptr && handle.surface != nullptr;
            case NativeWindowSystem::Unknown:
            case NativeWindowSystem::Web:
            case NativeWindowSystem::Headless:
            case NativeWindowSystem::Terminal:
                // Web draws to a host-selected canvas; Headless and Terminal have no graphical
                // window. None exposes a native handle a renderer could consume.
                return false;
        }
        return false;
    }

    std::string Describe(const NativeWindowHandle& handle)
    {
        std::string result = ToString(handle.system);

        std::string fields;
        const auto append = [&fields](const char* name) {
            if (!fields.empty())
            {
                fields += ", ";
            }
            fields += name;
            fields += "=set";
        };

        if (handle.display != nullptr) { append("display"); }
        if (handle.window != nullptr) { append("window"); }
        if (handle.surface != nullptr) { append("surface"); }
        if (handle.windowId != 0) { append("windowId"); }

        if (!fields.empty())
        {
            result += "(" + fields + ")";
        }
        return result;
    }

} // namespace CNA::Platform
