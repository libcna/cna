// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"

#include <LLGL/Surface.h>
#include <LLGL/Types.h>

#include <cstddef>

namespace CNA::Internal::Renderers::Llgl
{
    /** @brief Presents a platform-owned native window to LLGL as an `LLGL::Surface`. CNAEXT. */
    class LlglPlatformSurface final : public LLGL::Surface
    {
    public:
        /**
         * @brief Wraps an immutable native handle and its mutable size/density snapshot.
         * @param surface Platform renderer surface snapshot.
         * @param fullscreen Whether the platform window is currently fullscreen.
         */
        LlglPlatformSurface(const RendererSurfaceInfo& surface, bool fullscreen);

        /** @brief Frees the cached X11 visual info, if one was resolved. */
        ~LlglPlatformSurface() override;

        /** @brief Fills the native handle LLGL needs to create its context or surface. */
        bool GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize) override;

        /** @brief Returns the latest physical drawable size. */
        [[nodiscard]] LLGL::Extent2D GetContentSize() const override;

        /** @brief Reports the platform-owned size/fullscreen state without changing the window. */
        bool AdaptForVideoMode(LLGL::Extent2D* resolution, bool* fullscreen) override;

        /** @brief Returns LLGL's primary display, or null when none is reported. */
        [[nodiscard]] LLGL::Display* FindResidentDisplay() const override;

        /** @brief Refreshes mutable surface size and density. */
        void Update(const RendererSurfaceInfo& surface) { surface_.Update(surface); }

        /** @brief Returns the stable platform window identity. */
        [[nodiscard]] CNA::Platform::WindowId GetWindowId() const noexcept
        {
            return surface_.GetWindowId();
        }

        /** @brief Converts one logical window coordinate to drawable pixels. */
        [[nodiscard]] float WindowToDrawable(float value) const noexcept
        {
            return surface_.WindowToDrawable(value);
        }

        /** @brief Converts one drawable coordinate to logical window units. */
        [[nodiscard]] float DrawableToWindow(float value) const noexcept
        {
            return surface_.DrawableToWindow(value);
        }

    private:
        PlatformRendererSurfaceState surface_;
        bool fullscreen_ = false;

        // X11 visuals are fixed for the native window's lifetime. LLGL retains this pointer after
        // GetNativeHandle returns, so it is resolved once and owned by the adapter.
        void* cachedVisualInfo_ = nullptr;
        bool visualResolved_ = false;
        unsigned long cachedColorMap_ = 0;
    };
}
