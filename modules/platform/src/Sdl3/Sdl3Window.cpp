// SPDX-License-Identifier: MS-PL

#include "Sdl3Window.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace CNA::Platform::Sdl3 {

    namespace {

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
        SDL_SetWindowTitle(window_, title.c_str());
    }

    WindowBounds Sdl3Window::GetClientBounds() const
    {
        WindowBounds bounds;
        SDL_GetWindowPosition(window_, &bounds.x, &bounds.y);
        SDL_GetWindowSize(window_, &bounds.width, &bounds.height);
        return bounds;
    }

    WindowSize Sdl3Window::GetPixelSize() const
    {
        WindowSize size;
        // Deliberately the pixel size, not the logical size: under high DPI they differ, and a
        // renderer must size its swapchain from this one.
        SDL_GetWindowSizeInPixels(window_, &size.width, &size.height);
        return size;
    }

    void Sdl3Window::SetSize(const int width, const int height)
    {
        SDL_SetWindowSize(window_, width, height);
    }

    float Sdl3Window::GetDisplayScale() const
    {
        const float scale = SDL_GetWindowDisplayScale(window_);
        // SDL returns 0.0f on failure; a zero scale would divide to infinity in any layout
        // computation, so an unknown scale reports as 1:1 instead.
        return scale > 0.0f ? scale : 1.0f;
    }

    void Sdl3Window::SetResizable(const bool resizable)
    {
        SDL_SetWindowResizable(window_, resizable);
    }

    void Sdl3Window::SetBorderless(const bool borderless)
    {
        SDL_SetWindowBordered(window_, !borderless);
    }

    void Sdl3Window::SetFullscreenMode(const WindowFullscreenMode mode)
    {
        switch (mode)
        {
            case WindowFullscreenMode::Windowed:
                SDL_SetWindowFullscreen(window_, false);
                break;
            case WindowFullscreenMode::BorderlessFullscreen:
                // Null exclusive mode means "use the desktop mode", i.e. borderless fullscreen.
                SDL_SetWindowFullscreenMode(window_, nullptr);
                SDL_SetWindowFullscreen(window_, true);
                break;
            case WindowFullscreenMode::ExclusiveFullscreen:
                SDL_SetWindowFullscreen(window_, true);
                break;
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

    void Sdl3Window::Show() { SDL_ShowWindow(window_); }
    void Sdl3Window::Hide() { SDL_HideWindow(window_); }
    void Sdl3Window::Minimize() { SDL_MinimizeWindow(window_); }
    void Sdl3Window::Maximize() { SDL_MaximizeWindow(window_); }
    void Sdl3Window::Restore() { SDL_RestoreWindow(window_); }
    void Sdl3Window::Sync() { SDL_SyncWindow(window_); }

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

} // namespace CNA::Platform::Sdl3
