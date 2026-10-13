// SPDX-License-Identifier: MS-PL
// SystemTray::getIsSupportedProperty().
//
// Link boundary: asking whether the platform supports it must not link the platform's tray
// service, which the rest of SystemTray enables (SystemTray.cpp; docs/link-size-architecture.md).
#include "CNA/Devices/SystemTray.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

namespace CNA::Devices
{
    bool SystemTray::getIsSupportedProperty()
    {
        return CNA::Platform::GetCurrentPlatform().Supports(
            CNA::Platform::PlatformCapability::Tray);
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
