// SPDX-License-Identifier: MS-PL
// Link-footprint consumer: a program that uses the platform's tray and not its camera. It must
// link the tray implementation and not the camera's (LinkFootprint_TrayOnly,
// docs/link-size-architecture.md). It is linked, not run: with no argument main returns at once.
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/OptionalServices.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    (void)argv;
    if (argc < 2)
    {
        return 0;
    }
    CNA::Platform::EnableTrayService();
    CNA::Platform::IPlatformTray* tray = CNA::Platform::GetCurrentPlatform().GetTray();
    std::printf("tray %s\n", tray != nullptr ? "available" : "unsupported");
    return 0;
}
