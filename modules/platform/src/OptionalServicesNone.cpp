// SPDX-License-Identifier: MS-PL
// IPlatform::GetCamera() and GetTray() where no platform with either is compiled: the Headless and
// Terminal platforms report neither, so there is nothing to create. The SDL3 platform's are in
// Sdl3/Sdl3CameraService.cpp and Sdl3/Sdl3TrayService.cpp.
#include "CNA/Platform/IPlatform.hpp"

#if !defined(CNA_PLATFORM_SDL3)
namespace CNA::Platform {

    IPlatformTray* IPlatform::GetTray() { return GetTrayService(); }

    IPlatformCameraProvider* IPlatform::GetCamera() { return GetCameraService(); }
}
#endif
