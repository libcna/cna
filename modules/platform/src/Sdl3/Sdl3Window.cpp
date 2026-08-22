// SPDX-License-Identifier: MS-PL

#include "Sdl3Window.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/TargetPlatform.hpp"

#if defined(CNA_TARGET_IOS)
#include "Sdl3AppleOrientation.hpp"
#endif

#include <SDL3/SDL.h>

#include <cstring>

namespace CNA::Platform::Sdl3 {

    namespace {

        std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error != nullptr ? std::string(error) : std::string();
        }

        void RequireSdlSuccess(const bool succeeded, const char* operation)
        {
            if (!succeeded)
            {
                throw PlatformException(operation, LastSdlError());
            }
        }

        void SetExclusiveMode(SDL_Window* window, const int requestedWidth,
                              const int requestedHeight, const char* operation)
        {
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
            if (display == 0)
            {
                throw PlatformException(operation, LastSdlError());
            }

            SDL_DisplayMode closest{};
            const bool highDensity =
                (SDL_GetWindowFlags(window) & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0;
            if (!SDL_GetClosestFullscreenDisplayMode(display, requestedWidth, requestedHeight,
                                                     0.0f, highDensity, &closest))
            {
                throw PlatformException(operation, LastSdlError());
            }
            RequireSdlSuccess(SDL_SetWindowFullscreenMode(window, &closest), operation);
        }

        /// Determines which windowing system SDL is actually driving.
        ///
        /// Keyed off the driver name rather than off which properties happen to be present:
        /// probing properties would silently pick the first that answers, and several drivers
        /// expose overlapping ones (XWayland answers both X11 and Wayland queries). The driver
        /// name is what SDL itself considers authoritative.
        NativeWindowSystem DetectSystem()
        {
            const char* driver = SDL_GetCurrentVideoDriver();
            if (driver == nullptr)
            {
                return NativeWindowSystem::Unknown;
            }
            if (std::strcmp(driver, "windows") == 0) { return NativeWindowSystem::Win32; }
            if (std::strcmp(driver, "x11") == 0) { return NativeWindowSystem::X11; }
            if (std::strcmp(driver, "wayland") == 0) { return NativeWindowSystem::Wayland; }
            if (std::strcmp(driver, "cocoa") == 0) { return NativeWindowSystem::Cocoa; }
            if (std::strcmp(driver, "android") == 0) { return NativeWindowSystem::Android; }
            if (std::strcmp(driver, "emscripten") == 0) { return NativeWindowSystem::Web; }
            // "dummy", "offscreen" and anything unrecognised expose no consumable native window.
            // Reporting Headless is what makes a GPU renderer refuse deterministically instead of
            // dereferencing a null it was told was valid.
            return NativeWindowSystem::Headless;
        }

    } // namespace

    Sdl3Window::Sdl3Window(SDL_Window* window, const bool ownsWindow)
        : window_(window)
        , ownsWindow_(ownsWindow)
    {
        if (window_ == nullptr)
        {
            throw PlatformException("Sdl3Window", "constructed with a null SDL_Window");
        }
        SDL_GetWindowPosition(window_, &lastKnownBounds_.x, &lastKnownBounds_.y);
        SDL_GetWindowSize(window_, &lastKnownBounds_.width, &lastKnownBounds_.height);
    }

    Sdl3Window::~Sdl3Window()
    {
        if (ownsWindow_)
        {
            SDL_DestroyWindow(window_);
        }
    }

    WindowId Sdl3Window::GetId() const
    {
        return static_cast<WindowId>(SDL_GetWindowID(window_));
    }

    std::uintptr_t Sdl3Window::GetWindowHandle() const
    {
        return reinterpret_cast<std::uintptr_t>(window_);
    }

    NativeWindowHandle Sdl3Window::GetNativeHandle() const
    {
        NativeWindowHandle handle;
        handle.system = DetectSystem();

        const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        if (properties == 0)
        {
            return handle;
        }

        switch (handle.system)
        {
            case NativeWindowSystem::Win32:
                handle.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
                break;
            case NativeWindowSystem::X11:
                handle.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
                // An X11 Window is an XID -- an integer resource id, not an address -- so it is
                // read as a NUMBER property into the dedicated integer field. Putting it in the
                // pointer field is the classic interop bug NativeWindowHandle exists to prevent.
                handle.windowId = static_cast<std::uint64_t>(
                    SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
                break;
            case NativeWindowSystem::Wayland:
                handle.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
                handle.surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
                break;
            case NativeWindowSystem::Cocoa:
                handle.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
                break;
            case NativeWindowSystem::Android:
                handle.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
                break;
            case NativeWindowSystem::Web:
            case NativeWindowSystem::Headless:
            case NativeWindowSystem::Terminal:
            case NativeWindowSystem::Unknown:
                // No native handle by design; every pointer stays null.
                break;
        }

        return handle;
    }

    std::string Sdl3Window::GetTitle() const
    {
        const char* title = SDL_GetWindowTitle(window_);
        return title != nullptr ? std::string(title) : std::string();
    }

    void Sdl3Window::SetTitle(const std::string& title)
    {
        RequireSdlSuccess(SDL_SetWindowTitle(window_, title.c_str()), "Window::SetTitle");
    }

    // Deliberately non-throwing, unlike its GetPixelSize() sibling: this is the query a game may
    // read every frame as GameWindow.ClientBounds, and the one an event-driven bounds refresh
    // makes while the frame's event pump is on the stack. Both keep the last successfully queried
    // value rather than reporting a failure the caller has no way to act on.
    //
    // SDL can refuse a size query while the browser is updating its canvas or while native window
    // state is being torn down. Keeping the last valid rectangle is the only useful result for this
    // non-critical per-frame property; renderer creation uses the strict pixel-size query instead.
    WindowBounds Sdl3Window::GetClientBounds() const
    {
        int x = lastKnownBounds_.x;
        int y = lastKnownBounds_.y;
        if (SDL_GetWindowPosition(window_, &x, &y))
        {
            lastKnownBounds_.x = x;
            lastKnownBounds_.y = y;
        }

        int width = lastKnownBounds_.width;
        int height = lastKnownBounds_.height;
        if (SDL_GetWindowSize(window_, &width, &height))
        {
            lastKnownBounds_.width = width;
            lastKnownBounds_.height = height;
        }
        return lastKnownBounds_;
    }

    WindowSize Sdl3Window::GetPixelSize() const
    {
        WindowSize size;
        // Deliberately the pixel size, not the logical size: under high DPI they differ, and a
        // renderer must size its swapchain from this one.
        RequireSdlSuccess(SDL_GetWindowSizeInPixels(window_, &size.width, &size.height),
                          "Window::GetPixelSize");
        return size;
    }

    void Sdl3Window::SetSize(const int width, const int height)
    {
        // SDL_SetWindowSize has no effect in fullscreen. For exclusive fullscreen, a size change
        // means selecting the closest real display mode instead; otherwise a GraphicsDevice reset
        // would report the new backbuffer size while the monitor stayed on the old one.
        if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0 &&
            SDL_GetWindowFullscreenMode(window_) != nullptr)
        {
            SetExclusiveMode(window_, width, height, "Window::SetSize(ExclusiveFullscreen)");
            return;
        }
        RequireSdlSuccess(SDL_SetWindowSize(window_, width, height), "Window::SetSize");
    }

    float Sdl3Window::GetDisplayScale() const
    {
        // IPlatformWindow's value converts logical window coordinates to drawable pixels. SDL
        // calls that ratio "pixel density". SDL_GetWindowDisplayScale is deliberately different:
        // it also folds in the user's UI/content-scale preference and can be 1.25 even when a
        // non-high-density 120x60 window has a 120x60 drawable. Using it for input transforms
        // shifts every renderer coordinate despite there being no extra pixels.
        const float scale = SDL_GetWindowPixelDensity(window_);
        // SDL returns 0.0f on failure; a zero scale would divide to infinity in layout code.
        return scale > 0.0f ? scale : 1.0f;
    }

    bool Sdl3Window::IsResizable() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_RESIZABLE) != 0;
    }

    void Sdl3Window::SetResizable(const bool resizable)
    {
        RequireSdlSuccess(SDL_SetWindowResizable(window_, resizable), "Window::SetResizable");
    }

    bool Sdl3Window::IsBorderless() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_BORDERLESS) != 0;
    }

    void Sdl3Window::SetBorderless(const bool borderless)
    {
        RequireSdlSuccess(SDL_SetWindowBordered(window_, !borderless), "Window::SetBorderless");
    }

    void Sdl3Window::SetFullscreenMode(const WindowFullscreenMode mode)
    {
        switch (mode)
        {
            case WindowFullscreenMode::Windowed:
                RequireSdlSuccess(SDL_SetWindowFullscreen(window_, false),
                                  "Window::SetFullscreenMode(Windowed)");
                break;
            case WindowFullscreenMode::BorderlessFullscreen:
                // Null exclusive mode means "use the desktop mode", i.e. borderless fullscreen.
                RequireSdlSuccess(SDL_SetWindowFullscreenMode(window_, nullptr),
                                  "Window::SetFullscreenMode(BorderlessFullscreen)");
                RequireSdlSuccess(SDL_SetWindowFullscreen(window_, true),
                                  "Window::SetFullscreenMode(BorderlessFullscreen)");
                break;
            case WindowFullscreenMode::ExclusiveFullscreen:
            {
                const WindowBounds bounds = GetClientBounds();
                SetExclusiveMode(window_, bounds.width, bounds.height,
                                 "Window::SetFullscreenMode(ExclusiveFullscreen)");
                RequireSdlSuccess(SDL_SetWindowFullscreen(window_, true),
                                  "Window::SetFullscreenMode(ExclusiveFullscreen)");
                break;
            }
        }
    }

    WindowFullscreenMode Sdl3Window::GetFullscreenMode() const
    {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
        if ((flags & SDL_WINDOW_FULLSCREEN) == 0)
        {
            return WindowFullscreenMode::Windowed;
        }
        // A fullscreen window with no exclusive mode set is on the desktop mode.
        return SDL_GetWindowFullscreenMode(window_) == nullptr
                   ? WindowFullscreenMode::BorderlessFullscreen
                   : WindowFullscreenMode::ExclusiveFullscreen;
    }

    void Sdl3Window::Show()
    {
        RequireSdlSuccess(SDL_ShowWindow(window_), "Window::Show");
    }

    void Sdl3Window::Hide()
    {
        RequireSdlSuccess(SDL_HideWindow(window_), "Window::Hide");
    }

    void Sdl3Window::Minimize()
    {
        RequireSdlSuccess(SDL_MinimizeWindow(window_), "Window::Minimize");
    }

    void Sdl3Window::Maximize()
    {
        RequireSdlSuccess(SDL_MaximizeWindow(window_), "Window::Maximize");
    }

    void Sdl3Window::Restore()
    {
        RequireSdlSuccess(SDL_RestoreWindow(window_), "Window::Restore");
    }

    void Sdl3Window::Sync()
    {
        // Deliberately NOT RequireSdlSuccess. SDL_SyncWindow() returns false for exactly one
        // reason -- "the operation timed out before the window was in the requested state" -- and
        // a timeout is an ordinary outcome here, not a failure: on Wayland and X11 the compositor
        // owns the geometry and may simply not have acknowledged the change within SDL's window.
        //
        // Throwing on it turned a routine window resize into an uncaught PlatformException, i.e. a
        // std::terminate, in every game that resizes its window (reproduced with galaxy-eggbert
        // 2026-08-21: "CNA::Platform: Window::Sync failed" while dragging the window edge).
        //
        // Nothing depends on this call succeeding. Both callers -- GameWindow::
        // EndScreenDeviceChange() and GraphicsDevice::applyPresentationParametersToWindow() --
        // use it only as a best-effort ordering point so the size query that follows sees the new
        // geometry; UpdateViewportFromWindow() re-reads that geometry on the resize event and on
        // every Present() regardless, so a missed sync costs at most one frame of stale size.
        //
        // This also brings SDL3 back in line with the rest of the contract: IPlatformWindow::
        // Sync()'s own documentation promises only that it blocks, never that it throws; the SDL2
        // implementation pumps events and cannot fail; and PlatformConformanceTests asserts
        // EXPECT_NO_THROW(window_->Sync()).
        (void)SDL_SyncWindow(window_);
    }

    bool Sdl3Window::HasFocus() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    }

    bool Sdl3Window::IsMinimized() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
    }

    std::string Sdl3Window::GetDisplayName() const
    {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        if (display == 0)
        {
            return {};
        }
        const char* name = SDL_GetDisplayName(display);
        return name != nullptr ? std::string(name) : std::string();
    }

    void Sdl3Window::SetSupportedOrientations(const ScreenOrientation orientations)
    {
        // plans/plan_apple.md APPLE-15. Desktop keeps this as pure framework bookkeeping; only a mobile
        // operating system actually consults the declared set.
        if constexpr (!CNA::isMobilePlatform())
        {
            return;
        }
        else
        {
            if (orientations == ScreenOrientation::None)
            {
                // "No preference" has to clear the value rather than set an empty one, so the
                // platform falls back to the set declared at packaging time instead of retaining
                // an explicitly empty override.
                SDL_ResetHint(SDL_HINT_ORIENTATIONS);
            }
            else
            {
                std::string accepted;
                if (HasOrientation(orientations, ScreenOrientation::LandscapeLeft))
                {
                    accepted += "LandscapeLeft ";
                }
                if (HasOrientation(orientations, ScreenOrientation::LandscapeRight))
                {
                    accepted += "LandscapeRight ";
                }
                if (HasOrientation(orientations, ScreenOrientation::Portrait))
                {
                    accepted += "Portrait ";
                }
                SDL_SetHint(SDL_HINT_ORIENTATIONS, accepted.c_str());
            }

#if defined(CNA_TARGET_IOS)
            RequestAppleOrientationUpdate(window_);
#endif
        }
    }

} // namespace CNA::Platform::Sdl3
