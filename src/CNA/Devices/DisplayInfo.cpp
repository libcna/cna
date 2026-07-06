// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/DisplayInfo.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_video.h>

#include "Microsoft/Xna/Framework/GameWindow.hpp"

using Microsoft::Xna::Framework::GameWindow;
using Microsoft::Xna::Framework::Rectangle;

namespace CNA::Devices
{
    float DisplayInfo::getContentScaleProperty(const GameWindow& window)
    {
        SDL_Window* sdlWindow = window.GetNativeSdlWindowEXT();
        if (sdlWindow == nullptr)
        {
            return 0.0f;
        }

        return SDL_GetWindowDisplayScale(sdlWindow);
    }

    Rectangle DisplayInfo::getSafeAreaProperty(const GameWindow& window)
    {
        SDL_Window* sdlWindow = window.GetNativeSdlWindowEXT();
        if (sdlWindow == nullptr)
        {
            return Rectangle::Empty;
        }

        SDL_Rect rect{};
        if (!SDL_GetWindowSafeArea(sdlWindow, &rect))
        {
            return Rectangle::Empty;
        }

        return Rectangle(rect.x, rect.y, rect.w, rect.h);
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
