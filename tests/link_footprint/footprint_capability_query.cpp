// SPDX-License-Identifier: MS-PL
// Link-footprint consumer: a program that asks whether the platform supports a camera and a tray
// -- through the whole capability set, one capability at a time, and CNA::Devices -- but uses
// neither. Reporting support must not link the camera or the tray implementation, which only
// enabling the service does (LinkFootprint_CapabilityQuery, docs/link-size-architecture.md).
// It is linked, not run: with no argument main returns at once.
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformCapabilities.hpp"
#if defined(CNA_DEVICES)
#include "CNA/Devices/Camera.hpp"
#include "CNA/Devices/SystemTray.hpp"
#endif

#include <cstdio>

int main(int argc, char** argv)
{
    (void)argv;
    if (argc < 2)
    {
        return 0;
    }
    using CNA::Platform::PlatformCapability;
    CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
    const CNA::Platform::PlatformCapabilities capabilities = platform.GetCapabilities();
    std::printf("camera %d/%d, tray %d/%d\n", capabilities.camera,
                platform.Supports(PlatformCapability::Camera), capabilities.tray,
                platform.Supports(PlatformCapability::Tray));
#if defined(CNA_DEVICES)
    std::printf("devices: camera %d, tray %d\n", CNA::Devices::Camera::getIsSupportedProperty(),
                CNA::Devices::SystemTray::getIsSupportedProperty());
#endif
    return 0;
}
