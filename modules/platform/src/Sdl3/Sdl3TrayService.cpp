// SPDX-License-Identifier: MS-PL
// EnableTrayService() for the SDL3 platform.
//
// Link boundary: the one place that names the SDL tray implementation for the platform, reached
// only by a program that uses the tray. The camera has its own (Sdl3CameraService.cpp), and
// whether either is supported is answered without them (Sdl3ServiceSupport.cpp;
// docs/link-size-architecture.md).
#include "CNA/Platform/OptionalServices.hpp"
#include "Sdl3Platform.hpp"
#include "Sdl3Tray.hpp"

#include <memory>

namespace CNA::Platform {

    void EnableTrayService()
    {
        Sdl3::Sdl3Platform::InstallTray(
            []() -> std::unique_ptr<IPlatformTray> { return std::make_unique<Sdl3::Sdl3Tray>(); });
    }
}
