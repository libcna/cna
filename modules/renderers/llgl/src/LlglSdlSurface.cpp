// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Llgl/LlglSdlSurface.hpp"

#include <LLGL/Display.h>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

// LLGL's Linux native handle is defined in terms of Xlib, so this translation unit -- and only
// this one -- pulls in X11/Xlib.h. Xlib defines object-like macros with names this codebase uses
// as ordinary identifiers (None is an enumerator of several XNA enums, Success and Status are
// common member names), so nothing above this point may include an XNA header, and the macros are
// undefined again immediately below.
#include <LLGL/Platform/NativeHandle.h>

#if defined(__linux__) && !defined(__ANDROID__)
#   include <X11/Xlib.h>
#   include <X11/Xutil.h>
#   undef None
#   undef Status
#   undef Success
#   undef Bool
#   undef True
#   undef False
#   undef Always
#   undef Complex
#   define CNA_LLGL_SURFACE_X11 1
#endif

namespace CNA::Internal::Backends::Llgl
{
    namespace
    {
#ifdef CNA_LLGL_SURFACE_X11
        struct X11WindowHandles
        {
            void*         display = nullptr;
            unsigned long window  = 0;
        };

        X11WindowHandles GetX11Handles(SDL_Window* window)
        {
            SDL_PropertiesID properties = SDL_GetWindowProperties(window);
            X11WindowHandles handles;
            handles.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            handles.window = static_cast<unsigned long>(
                SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
            return handles;
        }
#endif
    }

    LlglSdlSurface::~LlglSdlSurface()
    {
#ifdef CNA_LLGL_SURFACE_X11
        if (cachedVisualInfo_ != nullptr)
            XFree(cachedVisualInfo_);
#endif
    }

    LlglSdlSurface::LlglSdlSurface(SDL_Window* window)
        : window_(window)
    {
        if (window_ == nullptr)
            throw std::runtime_error("LLGL backend: cannot build a surface from a null SDL window");

#ifdef CNA_LLGL_SURFACE_X11
        const X11WindowHandles handles = GetX11Handles(window_);
        if (handles.display == nullptr || handles.window == 0)
        {
            const char* driver = SDL_GetCurrentVideoDriver();
            throw std::runtime_error(
                std::string("LLGL backend: the SDL window exposes no X11 handles (video driver '") +
                (driver != nullptr ? driver : "unknown") +
                "'). This backend needs the x11 driver -- run with SDL_VIDEODRIVER=x11.");
        }
#else
        throw std::runtime_error(
            "LLGL backend: no native window handle is implemented for this platform yet");
#endif
    }

    bool LlglSdlSurface::GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize)
    {
        if (nativeHandle == nullptr || nativeHandleSize != sizeof(LLGL::NativeHandle))
            return false;

#ifdef CNA_LLGL_SURFACE_X11
        const X11WindowHandles handles = GetX11Handles(window_);
        if (handles.display == nullptr || handles.window == 0)
            return false;

        auto* display = static_cast<::Display*>(handles.display);
        const auto drawable = static_cast<::Window>(handles.window);

        auto* handle = static_cast<LLGL::NativeHandle*>(nativeHandle);
        *handle = {};
        handle->type = LLGL::NativeType::X11;
        handle->x11.display = display;
        handle->x11.window = drawable;
        handle->x11.screen = DefaultScreen(display);
        handle->x11.visual = nullptr;
        handle->x11.colorMap = 0;

        // Report the window's actual visual. LLGL falls back to choosing its own when this is
        // null, and on X11 a GLX context created for a different visual than the drawable's cannot
        // be made current (BadMatch) -- the SDL window already committed to a visual when it was
        // created, so this is the only one that can work.
        //
        // The window's visual and colormap never change once the window exists, so this is
        // resolved once and cached rather than re-queried on every call (LLGL calls this once per
        // swap-chain creation, and again on every resize) -- XGetVisualInfo allocates a fresh block
        // each time it is called, and LLGL keeps the pointer past this call (it creates the GLX
        // context from it later), so calling it again on every resize leaked one block per call.
        if (!visualResolved_)
        {
            ::XWindowAttributes attributes = {};
            if (XGetWindowAttributes(display, drawable, &attributes) != 0 && attributes.visual != nullptr)
            {
                ::XVisualInfo query = {};
                query.visualid = XVisualIDFromVisual(attributes.visual);
                int matchCount = 0;
                ::XVisualInfo* visualInfo = XGetVisualInfo(display, VisualIDMask, &query, &matchCount);
                if (visualInfo != nullptr && matchCount > 0)
                {
                    cachedVisualInfo_ = visualInfo;
                    cachedColorMap_ = attributes.colormap;
                    visualResolved_ = true;
                }
            }
        }
        handle->x11.visual = static_cast<::XVisualInfo*>(cachedVisualInfo_);
        handle->x11.colorMap = cachedColorMap_;

        return true;
#else
        return false;
#endif
    }

    LLGL::Extent2D LlglSdlSurface::GetContentSize() const
    {
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(window_, &width, &height))
        {
            width = 0;
            height = 0;
        }
        return LLGL::Extent2D{
            static_cast<std::uint32_t>(width > 0 ? width : 0),
            static_cast<std::uint32_t>(height > 0 ? height : 0)
        };
    }

    bool LlglSdlSurface::AdaptForVideoMode(LLGL::Extent2D* resolution, bool* fullscreen)
    {
        bool adaptedExactly = true;

        if (fullscreen != nullptr)
        {
            // CNA owns the window's fullscreen state (GraphicsDeviceManager.IsFullScreen drives
            // SDL_SetWindowFullscreen), so LLGL is told what the window really is rather than
            // being allowed to change it behind the shared layer's back.
            const bool isFullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
            if (*fullscreen != isFullscreen)
            {
                *fullscreen = isFullscreen;
                adaptedExactly = false;
            }
        }

        if (resolution != nullptr)
        {
            const LLGL::Extent2D contentSize = GetContentSize();
            if (resolution->width != contentSize.width || resolution->height != contentSize.height)
            {
                *resolution = contentSize;
                adaptedExactly = false;
            }
        }

        return adaptedExactly;
    }

    LLGL::Display* LlglSdlSurface::FindResidentDisplay() const
    {
        return LLGL::Display::GetPrimary();
    }
}
