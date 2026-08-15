// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/WindowDescription.hpp"

namespace CNA::Platform {

    const std::string& ToString(const WindowRenderIntent intent)
    {
        static const std::string none = "None";
        static const std::string openGl = "OpenGl";
        static const std::string vulkan = "Vulkan";
        static const std::string metal = "Metal";

        switch (intent)
        {
            case WindowRenderIntent::None:   return none;
            case WindowRenderIntent::OpenGl: return openGl;
            case WindowRenderIntent::Vulkan: return vulkan;
            case WindowRenderIntent::Metal:  return metal;
        }
        return none;
    }

    const std::string& ToString(const WindowFullscreenMode mode)
    {
        static const std::string windowed = "Windowed";
        static const std::string borderless = "BorderlessFullscreen";
        static const std::string exclusive = "ExclusiveFullscreen";

        switch (mode)
        {
            case WindowFullscreenMode::Windowed:             return windowed;
            case WindowFullscreenMode::BorderlessFullscreen: return borderless;
            case WindowFullscreenMode::ExclusiveFullscreen:  return exclusive;
        }
        return windowed;
    }

} // namespace CNA::Platform
