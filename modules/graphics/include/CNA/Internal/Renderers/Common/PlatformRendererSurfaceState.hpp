// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <cmath>
#include <string>

namespace CNA::Internal::Renderers
{
    /**
     * @brief Mutable, platform-neutral surface snapshot for native-window renderer families.
     *
     * The stable window id and native handle identify the surface for the renderer's whole
     * lifetime. Size and display scale are refreshed by @ref IGraphicsRenderer::OnSurfaceChanged.
     * Window coordinates are logical client units; drawable coordinates are physical pixels.
     */
    class PlatformRendererSurfaceState final
    {
    public:
        explicit PlatformRendererSurfaceState(const RendererSurfaceInfo& surface,
                                              const char* rendererName)
            : window_(surface.windowId)
            , nativeHandle_(surface.nativeHandle)
            , rendererName_(rendererName != nullptr ? rendererName : "native renderer")
        {
            if (window_ == 0)
            {
                throw CNA::Platform::PlatformException(
                    rendererName_ + "::Surface", "missing platform window id");
            }
            if (!CNA::Platform::HasNativeWindow(nativeHandle_))
            {
                throw CNA::Platform::PlatformException(
                    rendererName_ + "::Surface",
                    "missing native window (" + CNA::Platform::Describe(nativeHandle_) + ")");
            }
            Update(surface);
        }

        /** @brief Refreshes drawable size and density without permitting surface replacement. */
        void Update(const RendererSurfaceInfo& surface)
        {
            if (surface.windowId != window_)
            {
                throw CNA::Platform::PlatformException(
                    rendererName_ + "::OnSurfaceChanged", "stable window id changed");
            }
            if (!SameNativeHandle(surface.nativeHandle, nativeHandle_))
            {
                throw CNA::Platform::PlatformException(
                    rendererName_ + "::OnSurfaceChanged", "native window handle changed");
            }
            surface_ = surface;
            if (!(surface_.displayScale > 0.0f) || !std::isfinite(surface_.displayScale))
                surface_.displayScale = 1.0f;
        }

        /** @brief Returns the stable platform window identity. */
        [[nodiscard]] CNA::Platform::WindowId GetWindowId() const noexcept { return window_; }

        /** @brief Returns the immutable, non-owning native handle. */
        [[nodiscard]] const CNA::Platform::NativeWindowHandle& GetNativeHandle() const noexcept
        {
            return nativeHandle_;
        }

        /** @brief Returns the current physical drawable size. */
        [[nodiscard]] CNA::Platform::WindowSize GetDrawableSize() const noexcept
        {
            return surface_.drawableSize;
        }

        /** @brief Converts a logical window coordinate to physical drawable units. */
        [[nodiscard]] float WindowToDrawable(const float value) const noexcept
        {
            return value * surface_.displayScale;
        }

        /** @brief Converts a physical drawable coordinate to logical window units. */
        [[nodiscard]] float DrawableToWindow(const float value) const noexcept
        {
            return value / surface_.displayScale;
        }

        /** @brief Returns the current logical-to-physical scale. */
        [[nodiscard]] float GetDisplayScale() const noexcept { return surface_.displayScale; }

    private:
        static bool SameNativeHandle(const CNA::Platform::NativeWindowHandle& left,
                                     const CNA::Platform::NativeWindowHandle& right) noexcept
        {
            return left.system == right.system && left.display == right.display
                && left.window == right.window && left.surface == right.surface
                && left.windowId == right.windowId;
        }

        CNA::Platform::WindowId window_ = 0;
        CNA::Platform::NativeWindowHandle nativeHandle_;
        std::string rendererName_;
        RendererSurfaceInfo surface_;
    };
}
