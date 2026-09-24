// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Internal::Renderers
{
    /**
     * @brief Platform-neutral presentation metrics shared by CNA's OpenGL renderer families.
     *
     * The platform publishes physical drawable pixels plus a logical-to-physical display scale.
     * This value object derives the client-coordinate dimensions and owns the virtual
     * resolution transform (NativeBackBuffer, FixedHeightDynamicWidth, Stretch, Letterbox,
     * Overscan), so resize/DPI behaviour can be tested without a native window or GL.
     *
     * plans/plan_opengl4_modern_graphics.md GL4-0010: moved verbatim out of the EasyGL family,
     * where it was EasyGLSurfaceState, so OpenGL4 presents with the same transform instead of the
     * fill-the-window-only mapping it had before.
     */
    class GlPresentationSurfaceState
    {
    public:
        /**
         * @brief Creates the presentation state for one platform surface.
         *
         * @param surface The platform surface snapshot.
         * @param virtualWidth The game's virtual (logical) width; 0 when unset.
         * @param virtualHeight The game's virtual (logical) height; 0 when unset.
         * @param presentationMode How the logical extent is mapped onto the drawable.
         */
        GlPresentationSurfaceState(
            const RendererSurfaceInfo& surface, const int virtualWidth, const int virtualHeight,
            const CnaPresentationMode presentationMode)
            : surface_(surface), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight),
              presentationMode_(presentationMode)
        {
            Update(surface);
        }

        /** @brief Replaces the platform snapshot after resize or density change. */
        void Update(const RendererSurfaceInfo& surface)
        {
            surface_ = surface;
            if (!(surface_.displayScale > 0.0f))
            {
                surface_.displayScale = 1.0f;
            }
        }

        /** @brief Replaces the virtual game resolution. */
        void SetVirtualResolution(const int width, const int height)
        {
            virtualWidth_ = width;
            virtualHeight_ = height;
        }

        /** @brief Replaces the presentation policy. */
        void SetPresentationMode(const CnaPresentationMode mode)
        {
            presentationMode_ = mode;
        }

        /** @brief Gets the physical drawable extent used by GL framebuffer operations. */
        void GetDrawableSize(int& width, int& height) const
        {
            width = surface_.drawableSize.width;
            height = surface_.drawableSize.height;
        }

        /** @brief Gets the logical client extent of the window, in window units. */
        void GetClientSize(int& width, int& height) const
        {
            width = static_cast<int>(std::lround(
                static_cast<double>(surface_.drawableSize.width) / surface_.displayScale));
            height = static_cast<int>(std::lround(
                static_cast<double>(surface_.drawableSize.height) / surface_.displayScale));
        }

        /** @brief Gets the renderer's logical game extent. */
        void GetLogicalSize(int& width, int& height) const
        {
            int clientWidth = 0;
            int clientHeight = 0;
            GetClientSize(clientWidth, clientHeight);
            if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
                virtualHeight_ <= 0)
            {
                width = clientWidth;
                height = clientHeight;
                return;
            }

            height = virtualHeight_;
            if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && clientHeight > 0)
            {
                width = static_cast<int>(
                    static_cast<double>(clientWidth) * virtualHeight_ / clientHeight + 0.5);
            }
            else
            {
                width = virtualWidth_ > 0 ? virtualWidth_ : clientWidth;
            }
        }

        // The physical counterpart of GetLogicalSize() above, and the reason a GL renderer using this
        // state needs an IGraphicsRenderer::GetDefaultViewportRect() override at all.
        //
        // GraphicsDevice::UpdateViewportFromWindow() pushes THIS rectangle to glViewport, while
        // GetLogicalSize() only feeds GraphicsDevice.Viewport.Width/Height. The base-class default
        // returns (0, 0, GetViewportSize()) -- the LOGICAL size used as if it were physical pixels.
        // That is correct only for a renderer with no virtual resolution, and a GL renderer always has one:
        // GraphicsDevice::Reset() sets it to the backbuffer size on every device creation, and the
        // default presentation mode is FixedHeightDynamicWidth.
        //
        // So the default was actively wrong here. Resize an 800x480 window to 1200x800 and the logical
        // size became 720x480 (height pinned, width following the aspect) -- which then got applied as
        // a 720x480 PHYSICAL viewport inside a 1200x800 drawable. The game rendered into a corner and
        // the rest of the window kept the clear colour, while glClear (viewport-independent) covered
        // all of it. Reported against galaxy-eggbert 2026-08-21: resizing the window or going
        // fullscreen with F11 did not enlarge the game.
        //
        // Mirrors OpenGL2Renderer::ComputeLogicalViewport()/SdlGpuRenderer's algorithm, the
        // established reference for real Letterbox/Overscan/Stretch semantics in this codebase.
        /**
         * @brief Gets the PHYSICAL drawable sub-rectangle the logical extent maps into.
         *
         * The counterpart of GetLogicalSize(): that one answers "what resolution does the game
         * think it is drawing at", this one answers "which drawable pixels does that land on".
         * They are the same rectangle only when the window happens to match the virtual
         * resolution's aspect. Identical to the full drawable for FixedHeightDynamicWidth,
         * NativeBackBuffer and Stretch; only Letterbox/Overscan shrink and centre it.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) const
        {
            int physWidth = 0;
            int physHeight = 0;
            GetDrawableSize(physWidth, physHeight);

            x = 0;
            y = 0;
            width = std::max(0, physWidth);
            height = std::max(0, physHeight);

            if (physWidth <= 0 || physHeight <= 0)
            {
                return;
            }

            // Full drawable, no scaling: nothing to centre, and a degenerate virtual resolution has
            // no aspect to preserve.
            if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
                presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth ||
                presentationMode_ == CnaPresentationMode::Stretch ||
                virtualWidth_ <= 0 || virtualHeight_ <= 0)
            {
                return;
            }

            // Letterbox/Overscan: scale the virtual resolution uniformly -- min to fit inside the
            // drawable (bars), max to cover it (cropping) -- then centre the result.
            const double logicalWidth = static_cast<double>(virtualWidth_);
            const double logicalHeight = static_cast<double>(virtualHeight_);
            const double scaleX = static_cast<double>(physWidth) / logicalWidth;
            const double scaleY = static_cast<double>(physHeight) / logicalHeight;
            const double scale = (presentationMode_ == CnaPresentationMode::Overscan)
                                     ? std::max(scaleX, scaleY)
                                     : std::min(scaleX, scaleY);

            width = static_cast<int>(std::lround(logicalWidth * scale));
            height = static_cast<int>(std::lround(logicalHeight * scale));
            x = static_cast<int>(std::lround((static_cast<double>(physWidth) - logicalWidth * scale) * 0.5));
            y = static_cast<int>(std::lround((static_cast<double>(physHeight) - logicalHeight * scale) * 0.5));
        }

        /** @brief Converts logical client units into renderer game units. */
        bool WindowToLogical(const float windowX, const float windowY,
                                                 float& logicalX, float& logicalY) const
        {
            int logicalWidth = 0;
            int logicalHeight = 0;
            GetLogicalSize(logicalWidth, logicalHeight);

            int viewportX = 0;
            int viewportY = 0;
            int viewportWidth = 0;
            int viewportHeight = 0;
            GetDefaultViewportRect(
                viewportX, viewportY, viewportWidth, viewportHeight);
            if (logicalWidth <= 0 || logicalHeight <= 0 ||
                viewportWidth <= 0 || viewportHeight <= 0)
            {
                return false;
            }

            const float inverseDisplayScale = 1.0f / surface_.displayScale;
            const float clientViewportX =
                static_cast<float>(viewportX) * inverseDisplayScale;
            const float clientViewportY =
                static_cast<float>(viewportY) * inverseDisplayScale;
            const float clientViewportWidth =
                static_cast<float>(viewportWidth) * inverseDisplayScale;
            const float clientViewportHeight =
                static_cast<float>(viewportHeight) * inverseDisplayScale;
            logicalX = (windowX - clientViewportX) *
                static_cast<float>(logicalWidth) / clientViewportWidth;
            logicalY = (windowY - clientViewportY) *
                static_cast<float>(logicalHeight) / clientViewportHeight;
            return windowX >= clientViewportX &&
                   windowX < clientViewportX + clientViewportWidth &&
                   windowY >= clientViewportY &&
                   windowY < clientViewportY + clientViewportHeight;
        }

        /** @brief Converts renderer game units into logical client units. */
        bool LogicalToWindow(const float logicalX, const float logicalY,
                                                 float& windowX, float& windowY) const
        {
            int logicalWidth = 0;
            int logicalHeight = 0;
            GetLogicalSize(logicalWidth, logicalHeight);

            int viewportX = 0;
            int viewportY = 0;
            int viewportWidth = 0;
            int viewportHeight = 0;
            GetDefaultViewportRect(
                viewportX, viewportY, viewportWidth, viewportHeight);
            if (logicalWidth <= 0 || logicalHeight <= 0 ||
                viewportWidth <= 0 || viewportHeight <= 0)
            {
                return false;
            }

            const float inverseDisplayScale = 1.0f / surface_.displayScale;
            const float clientViewportX =
                static_cast<float>(viewportX) * inverseDisplayScale;
            const float clientViewportY =
                static_cast<float>(viewportY) * inverseDisplayScale;
            const float clientViewportWidth =
                static_cast<float>(viewportWidth) * inverseDisplayScale;
            const float clientViewportHeight =
                static_cast<float>(viewportHeight) * inverseDisplayScale;
            windowX = clientViewportX + logicalX * clientViewportWidth /
                static_cast<float>(logicalWidth);
            windowY = clientViewportY + logicalY * clientViewportHeight /
                static_cast<float>(logicalHeight);
            return true;
        }

        /** @brief Gets the stable platform window identity. */
        [[nodiscard]] CNA::Platform::WindowId GetWindowId() const { return surface_.windowId; }

    private:
        RendererSurfaceInfo surface_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::Letterbox;
    };
}
