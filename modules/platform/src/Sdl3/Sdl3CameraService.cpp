// SPDX-License-Identifier: MS-PL
// EnableCameraService() for the SDL3 platform.
//
// Link boundary: the one place that names the SDL camera implementation for the platform, reached
// only by a program that uses the camera. The tray has its own (Sdl3TrayService.cpp), and whether
// either is supported is answered without them (Sdl3ServiceSupport.cpp;
// docs/link-size-architecture.md).
#include "CNA/Platform/OptionalServices.hpp"
#include "Sdl3Camera.hpp"
#include "Sdl3Platform.hpp"

#include <memory>

namespace CNA::Platform {

    void EnableCameraService()
    {
        Sdl3::Sdl3Platform::InstallCamera(
            []() -> std::unique_ptr<IPlatformCameraProvider>
            { return std::make_unique<Sdl3::Sdl3CameraProvider>(); });
    }
}
