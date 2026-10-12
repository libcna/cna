// SPDX-License-Identifier: MS-PL
// Whether the camera and the tray are supported: what Sdl3Platform::GetCapabilities() reports,
// whether or not a program has enabled the services.
//
// Link boundary: apart from the providers (Sdl3Camera.cpp, Sdl3Tray.cpp), which only
// EnableCameraService()/EnableTrayService() link, so that reporting support links neither
// (docs/link-size-architecture.md).
#include "Sdl3ServiceSupport.hpp"
#include "Sdl3Synchronization.hpp"

#include <SDL3/SDL.h>

#include <optional>

namespace CNA::Platform::Sdl3 {

    bool TraySupported() noexcept
    {
#if defined(SDL_PLATFORM_WINDOWS) || defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_MACOS)
        return true;
#else
        return false;
#endif
    }

    /// Starts SDL's camera subsystem once, and answers whether it is up.
    ///
    /// SDL_GetCameras returns NULL with "Camera subsystem is not initialized" until
    /// SDL_INIT_CAMERA has run, and SDL_INIT_CAMERA appeared nowhere in this tree. So
    /// IsSupported answered true -- it reads the compiled-in driver table, which needs no
    /// subsystem -- while GetCameras answered an empty list, and a caller was told the
    /// platform supports cameras and handed nothing. That reads as "no camera is attached",
    /// which is the one thing it did not mean. Measured in a browser with a fake capture
    /// device present and permission granted, and it is not browser-specific: this provider
    /// serves every SDL3 platform.
    ///
    /// Bracketed the way Sdl3AudioDevice::Open brackets SDL_INIT_AUDIO, with one difference:
    /// the subsystem is not quit again. A SDL_CameraID is only meaningful inside the session
    /// that issued it, so quitting between the enumeration and the SDL_OpenCamera that follows
    /// it would hand back ids that name nothing -- the same trap the graphics adapter cache
    /// fell into with displays. PLAT-4 leaves global SDL lifetime to the host application, so
    /// nothing here calls SDL_Quit either way.
    bool EnsureCameraSubsystem()
    {
        static const bool started = []() {
            // Subsystem start-up is process-global SDL state; unlocked, it ran beside another
            // thread's SDL_CreateWindow and corrupted the heap (RUST-UPSTREAM-023).
            std::optional<SdlGlobalStateLock> lock;
            if (!SdlGlobalStateHeldByThisThread())
            {
                lock.emplace();
            }
            if (SDL_WasInit(SDL_INIT_CAMERA) != 0)
            {
                return true;
            }
            return SDL_InitSubSystem(SDL_INIT_CAMERA) != false;
        }();
        return started;
    }

    bool CameraSupported()
    {
        // Both halves, in this order: a build with no camera driver cannot start the subsystem,
        // and a driver that cannot start is not support a caller can act on.
        return SDL_GetNumCameraDrivers() > 0 && EnsureCameraSubsystem();
    }

    bool CameraSubsystemRunning()
    {
        return SDL_WasInit(SDL_INIT_CAMERA) != 0;
    }
}
