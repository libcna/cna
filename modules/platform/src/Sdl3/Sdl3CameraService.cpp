// SPDX-License-Identifier: MS-PL
// IPlatform::GetCamera() in an SDL3 build.
//
// Link boundary: the one place that names the SDL camera implementation for the platform, reached
// only by a program that calls GetCamera(). The tray has its own (Sdl3TrayService.cpp), and whether
// either is supported is answered without them (Sdl3ServiceSupport.cpp;
// docs/link-size-architecture.md).
#include "CNA/Platform/IPlatform.hpp"
#include "Sdl3Camera.hpp"
#include "Sdl3Platform.hpp"

#include <memory>

namespace CNA::Platform {

    namespace {

        std::unique_ptr<IPlatformCameraProvider> CreateCameraProvider()
        {
            return std::make_unique<Sdl3::Sdl3CameraProvider>();
        }
    }

    IPlatformCameraProvider* IPlatform::GetCamera()
    {
        // The SDL3 platform creates its provider through this; any other platform answers alone.
        Sdl3::Sdl3Platform::InstallCamera(&CreateCameraProvider);
        return GetCameraService();
    }
}
