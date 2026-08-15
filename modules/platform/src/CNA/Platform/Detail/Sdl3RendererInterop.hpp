// SPDX-License-Identifier: MS-PL
#pragma once

// Transitional renderer-only SDL3 edge for PLAT-58.
//
// The common GraphicsRendererCreateArgs contract carries only WindowId and native value types.
// Renderer families that still use SDL internally resolve that stable id here, after the common
// boundary has already been crossed. Later family migrations remove their dependency on this
// private implementation header; it is deliberately outside the public IPlatform include tree.

#include "CNA/Platform/PlatformEvent.hpp"

#include <SDL3/SDL_video.h>

namespace CNA::Platform::Detail
{
    /** @brief Resolves a renderer surface id at the SDL3-specific renderer edge. */
    [[nodiscard]] inline SDL_Window* ResolveSdl3RendererWindow(const WindowId windowId)
    {
        if (windowId == 0)
        {
            return nullptr;
        }
        return SDL_GetWindowFromID(static_cast<SDL_WindowID>(windowId));
    }
}
