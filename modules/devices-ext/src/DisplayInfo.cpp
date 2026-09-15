// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/DisplayInfo.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"

using Microsoft::Xna::Framework::GameWindow;
using Microsoft::Xna::Framework::Rectangle;

namespace CNA::Devices
{
    float DisplayInfo::getContentScaleProperty(const GameWindow& window)
    {
        const CNA::Platform::IPlatformWindow* platformWindow =
            window.getPlatformWindowInternal();
        if (platformWindow == nullptr)
        {
            return 0.0f;
        }

        // The window's pixel density combined with its display's content scale, as documented.
        // A platform states high density one way or the other -- as pixel density (macOS,
        // Wayland) or as the display's content scale (Windows, X11, where a window's pixels are
        // its logical units) -- so the larger of the two is the scale to size content by; a
        // product would double-count a backend reporting the same density both ways.
        // (plans/plan_x11.md X11-0156: X11 reported the session's Xft.dpi as the window's display
        // scale, which it is not; the content scale now comes from the display.)
        float scale = platformWindow->GetDisplayScale();
        if (CNA::Platform::HasCurrentPlatform())
        {
            if (const CNA::Platform::IPlatformDisplays* displays =
                    CNA::Platform::GetCurrentPlatform().GetDisplays())
            {
                CNA::Platform::DisplayInfo display;
                if (displays->TryGetDisplayForWindow(*platformWindow, display) &&
                    display.contentScale > scale)
                {
                    scale = display.contentScale;
                }
            }
        }
        return scale;
    }

    Rectangle DisplayInfo::getSafeAreaProperty(const GameWindow& window)
    {
        const CNA::Platform::IPlatformWindow* platformWindow =
            window.getPlatformWindowInternal();
        if (platformWindow == nullptr)
        {
            return Rectangle::Empty;
        }

        CNA::Platform::IPlatformDisplays* displays =
            CNA::Platform::GetCurrentPlatform().GetDisplays();
        CNA::Platform::WindowBounds safeArea;
        if (displays == nullptr ||
            !displays->TryGetSafeAreaForWindow(*platformWindow, safeArea))
        {
            return Rectangle::Empty;
        }

        return Rectangle(safeArea.x, safeArea.y, safeArea.width, safeArea.height);
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
