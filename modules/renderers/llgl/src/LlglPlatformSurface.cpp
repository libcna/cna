// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Llgl/LlglPlatformSurface.hpp"

#include <LLGL/Display.h>

#include <stdexcept>

// LLGL's Linux native handle is defined in terms of Xlib. Keep the X11 headers confined to this
// translation unit, and remove their object-like macros before entering CNA namespaces.
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

namespace CNA::Internal::Renderers::Llgl
{
    LlglPlatformSurface::LlglPlatformSurface(const RendererSurfaceInfo& surface,
                                             const bool fullscreen)
        : surface_(surface, "LLGL renderer")
        , fullscreen_(fullscreen)
    {
#ifdef CNA_LLGL_SURFACE_X11
        CNA::Platform::X11NativeWindow x11;
        if (!CNA::Platform::TryGetX11(surface_.GetNativeHandle(), x11))
        {
            throw std::runtime_error(
                "LLGL renderer: Linux requires an X11 native window, got " +
                CNA::Platform::Describe(surface_.GetNativeHandle()));
        }
#else
        throw std::runtime_error(
            "LLGL renderer: no native window handle is implemented for this platform yet");
#endif
    }

    LlglPlatformSurface::~LlglPlatformSurface()
    {
#ifdef CNA_LLGL_SURFACE_X11
        if (cachedVisualInfo_ != nullptr)
            XFree(cachedVisualInfo_);
#endif
    }

    bool LlglPlatformSurface::GetNativeHandle(void* nativeHandle,
                                              const std::size_t nativeHandleSize)
    {
        if (nativeHandle == nullptr || nativeHandleSize != sizeof(LLGL::NativeHandle))
            return false;

#ifdef CNA_LLGL_SURFACE_X11
        CNA::Platform::X11NativeWindow x11;
        if (!CNA::Platform::TryGetX11(surface_.GetNativeHandle(), x11))
            return false;

        auto* display = static_cast<::Display*>(x11.display);
        const auto drawable = static_cast<::Window>(x11.window);

        auto* handle = static_cast<LLGL::NativeHandle*>(nativeHandle);
        *handle = {};
        handle->type = LLGL::NativeType::X11;
        handle->x11.display = display;
        handle->x11.window = drawable;
        handle->x11.screen = DefaultScreen(display);
        handle->x11.visual = nullptr;
        handle->x11.colorMap = 0;

        // LLGL otherwise chooses a visual itself. A GLX context built for a visual different from
        // the already-created drawable cannot become current, so report the drawable's real one.
        if (!visualResolved_)
        {
            ::XWindowAttributes attributes = {};
            if (XGetWindowAttributes(display, drawable, &attributes) != 0
                && attributes.visual != nullptr)
            {
                ::XVisualInfo query = {};
                query.visualid = XVisualIDFromVisual(attributes.visual);
                int matchCount = 0;
                ::XVisualInfo* visualInfo =
                    XGetVisualInfo(display, VisualIDMask, &query, &matchCount);
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

    LLGL::Extent2D LlglPlatformSurface::GetContentSize() const
    {
        const CNA::Platform::WindowSize size = surface_.GetDrawableSize();
        return {
            static_cast<std::uint32_t>(size.width > 0 ? size.width : 0),
            static_cast<std::uint32_t>(size.height > 0 ? size.height : 0)
        };
    }

    bool LlglPlatformSurface::AdaptForVideoMode(LLGL::Extent2D* resolution, bool* fullscreen)
    {
        bool adaptedExactly = true;
        if (fullscreen != nullptr && *fullscreen != fullscreen_)
        {
            *fullscreen = fullscreen_;
            adaptedExactly = false;
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

    LLGL::Display* LlglPlatformSurface::FindResidentDisplay() const
    {
        return LLGL::Display::GetPrimary();
    }
}
