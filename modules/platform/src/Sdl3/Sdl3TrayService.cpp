// SPDX-License-Identifier: MS-PL
// IPlatform::GetTray() in an SDL3 build.
//
// Link boundary: the one place that names the SDL tray implementation for the platform, reached
// only by a program that calls GetTray(). The camera has its own (Sdl3CameraService.cpp), and
// whether either is supported is answered without them (Sdl3ServiceSupport.cpp;
// docs/link-size-architecture.md).
#include "CNA/Platform/IPlatform.hpp"
#include "Sdl3Platform.hpp"
#include "Sdl3Tray.hpp"

#include <memory>

namespace CNA::Platform {

    namespace {

        std::unique_ptr<IPlatformTray> CreateTray()
        {
            return std::make_unique<Sdl3::Sdl3Tray>();
        }
    }

    IPlatformTray* IPlatform::GetTray()
    {
        // The SDL3 platform creates its tray through this; any other platform answers alone.
        Sdl3::Sdl3Platform::InstallTray(&CreateTray);
        return GetTrayService();
    }
}
