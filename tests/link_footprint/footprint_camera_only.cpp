// SPDX-License-Identifier: MS-PL
// Link-footprint consumer: a program that uses the platform's camera and not its tray. It must
// link the camera implementation and not the tray's (LinkFootprint_CameraOnly,
// docs/link-size-architecture.md). It is linked, not run: with no argument main returns at once.
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    (void)argv;
    if (argc < 2)
    {
        return 0;
    }
    CNA::Platform::IPlatformCameraProvider* camera = CNA::Platform::GetCurrentPlatform().GetCamera();
    std::printf("camera %s\n", camera != nullptr ? "available" : "unsupported");
    return 0;
}
