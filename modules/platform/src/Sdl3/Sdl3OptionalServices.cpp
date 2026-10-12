// SPDX-License-Identifier: MS-PL
// EnableCameraService() and EnableTrayService() for the SDL3 platform.
//
// Link boundary: the one place that names the SDL camera and tray implementations for the
// platform, reached only by a program that uses CNA::Devices::Camera or SystemTray. Until then
// the platform neither links them nor starts SDL's camera subsystem (docs/link-size-architecture.md).
#include "CNA/Platform/OptionalServices.hpp"
#include "Sdl3Camera.hpp"
#include "Sdl3Platform.hpp"
#include "Sdl3Tray.hpp"

#include <memory>

namespace CNA::Platform {

    void EnableCameraService()
    {
        Sdl3::Sdl3Platform::InstallCamera(
            &Sdl3::Sdl3CameraProvider::IsSupported,
            []() -> std::unique_ptr<IPlatformCameraProvider>
            { return std::make_unique<Sdl3::Sdl3CameraProvider>(); });
    }

    void EnableTrayService()
    {
        Sdl3::Sdl3Platform::InstallTray(
            &Sdl3::Sdl3Tray::IsSupported,
            []() -> std::unique_ptr<IPlatformTray> { return std::make_unique<Sdl3::Sdl3Tray>(); });
    }
}
