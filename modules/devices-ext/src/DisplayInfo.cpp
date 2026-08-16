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

        return platformWindow->GetDisplayScale();
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
