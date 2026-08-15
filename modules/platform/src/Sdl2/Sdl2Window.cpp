// SPDX-License-Identifier: MS-PL

#include "Sdl2Window.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <SDL.h>

namespace CNA::Platform::Sdl2 {

    namespace {

        void RequireSdlSuccess(const int result, const char* operation)
        {
            if (result != 0)
            {
                throw PlatformException(operation, SDL_GetError());
            }
        }

    } // namespace

    Sdl2Window::Sdl2Window(SDL_Window* window, const bool ownsWindow)
        : window_(window), ownsWindow_(ownsWindow)
    {
        if (window_ == nullptr)
        {
            throw PlatformException("Sdl2Window", "constructed with a null SDL_Window");
        }
    }

    Sdl2Window::~Sdl2Window()
    {
        if (ownsWindow_)
        {
            SDL_DestroyWindow(window_);
        }
    }

    WindowId Sdl2Window::GetId() const { return static_cast<WindowId>(SDL_GetWindowID(window_)); }
    std::uintptr_t Sdl2Window::GetWindowHandle() const
    {
        return reinterpret_cast<std::uintptr_t>(window_);
    }

    NativeWindowHandle Sdl2Window::GetNativeHandle() const
    {
        // SDL2 has SDL_SysWMinfo, but its platform-specific union is intentionally not part of
        // the initial backend contract. OpenGL only needs the SDL window id; other native-handle
        // consumers must use the capability and refuse cleanly until a typed SDL2 bridge exists.
        NativeWindowHandle handle;
        handle.system = NativeWindowSystem::Unknown;
        return handle;
    }

    std::string Sdl2Window::GetTitle() const
    {
        const char* title = SDL_GetWindowTitle(window_);
        return title != nullptr ? std::string(title) : std::string();
    }
    void Sdl2Window::SetTitle(const std::string& title)
    {
        SDL_SetWindowTitle(window_, title.c_str());
    }
    WindowBounds Sdl2Window::GetClientBounds() const
    {
        WindowBounds result;
        SDL_GetWindowPosition(window_, &result.x, &result.y);
        SDL_GetWindowSize(window_, &result.width, &result.height);
        return result;
    }
    WindowSize Sdl2Window::GetPixelSize() const
    {
        WindowSize result;
        SDL_GL_GetDrawableSize(window_, &result.width, &result.height);
        if (result.width <= 0 || result.height <= 0)
        {
            SDL_GetWindowSize(window_, &result.width, &result.height);
        }
        return result;
    }
    void Sdl2Window::SetSize(const int width, const int height)
    {
        SDL_SetWindowSize(window_, width, height);
    }
    float Sdl2Window::GetDisplayScale() const
    {
        const WindowBounds logical = GetClientBounds();
        const WindowSize pixels = GetPixelSize();
        return logical.width > 0 && pixels.width > 0
                   ? static_cast<float>(pixels.width) / static_cast<float>(logical.width)
                   : 1.0F;
    }
    bool Sdl2Window::IsResizable() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_RESIZABLE) != 0;
    }
    void Sdl2Window::SetResizable(const bool resizable)
    {
        SDL_SetWindowResizable(window_, resizable ? SDL_TRUE : SDL_FALSE);
    }
    bool Sdl2Window::IsBorderless() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_BORDERLESS) != 0;
    }
    void Sdl2Window::SetBorderless(const bool borderless)
    {
        SDL_SetWindowBordered(window_, borderless ? SDL_FALSE : SDL_TRUE);
    }
    void Sdl2Window::SetFullscreenMode(const WindowFullscreenMode mode)
    {
        Uint32 flags = 0;
        if (mode == WindowFullscreenMode::BorderlessFullscreen) { flags = SDL_WINDOW_FULLSCREEN_DESKTOP; }
        if (mode == WindowFullscreenMode::ExclusiveFullscreen) { flags = SDL_WINDOW_FULLSCREEN; }
        RequireSdlSuccess(SDL_SetWindowFullscreen(window_, flags), "Window::SetFullscreenMode");
    }
    WindowFullscreenMode Sdl2Window::GetFullscreenMode() const
    {
        const Uint32 flags = SDL_GetWindowFlags(window_);
        if ((flags & SDL_WINDOW_FULLSCREEN) == 0) { return WindowFullscreenMode::Windowed; }
        return (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP
                   ? WindowFullscreenMode::BorderlessFullscreen
                   : WindowFullscreenMode::ExclusiveFullscreen;
    }
    void Sdl2Window::Show() { SDL_ShowWindow(window_); }
    void Sdl2Window::Hide() { SDL_HideWindow(window_); }
    void Sdl2Window::Minimize() { SDL_MinimizeWindow(window_); }
    void Sdl2Window::Maximize() { SDL_MaximizeWindow(window_); }
    void Sdl2Window::Restore() { SDL_RestoreWindow(window_); }
    void Sdl2Window::Sync()
    {
        // SDL2 applies window changes through the native event loop; it has no SDL3 SyncWindow
        // equivalent. Pumping events makes that progress without inventing a blocking primitive.
        SDL_PumpEvents();
    }
    bool Sdl2Window::HasFocus() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    }
    bool Sdl2Window::IsMinimized() const
    {
        return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
    }
    std::string Sdl2Window::GetDisplayName() const
    {
        const int display = SDL_GetWindowDisplayIndex(window_);
        if (display < 0) { return {}; }
        const char* name = SDL_GetDisplayName(display);
        return name != nullptr ? std::string(name) : std::string();
    }

} // namespace CNA::Platform::Sdl2
