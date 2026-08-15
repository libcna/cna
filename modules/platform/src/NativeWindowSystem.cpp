// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/NativeWindowSystem.hpp"

namespace CNA::Platform {

    const std::string& ToString(const NativeWindowSystem system)
    {
        static const std::string unknown  = "Unknown";
        static const std::string win32    = "Win32";
        static const std::string x11      = "X11";
        static const std::string wayland  = "Wayland";
        static const std::string cocoa    = "Cocoa";
        static const std::string android  = "Android";
        static const std::string web      = "Web";
        static const std::string headless = "Headless";
        static const std::string terminal = "Terminal";

        // Exhaustive switch with no default arm: adding a NativeWindowSystem value without
        // naming it here is a compiler diagnostic rather than a silent "Unknown".
        switch (system)
        {
            case NativeWindowSystem::Unknown:  return unknown;
            case NativeWindowSystem::Win32:    return win32;
            case NativeWindowSystem::X11:      return x11;
            case NativeWindowSystem::Wayland:  return wayland;
            case NativeWindowSystem::Cocoa:    return cocoa;
            case NativeWindowSystem::Android:  return android;
            case NativeWindowSystem::Web:      return web;
            case NativeWindowSystem::Headless: return headless;
            case NativeWindowSystem::Terminal: return terminal;
        }

        return unknown;
    }

} // namespace CNA::Platform
