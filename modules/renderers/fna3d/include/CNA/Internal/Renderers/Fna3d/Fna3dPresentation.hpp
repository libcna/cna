// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::Fna3d
{
    /**
     * @brief Where the FNA3D back buffer lands inside the OS window, and at what logical size.
     *
     * FNA3D presents through `FNA3D_SwapBuffers(device, sourceRectangle, destinationRectangle,
     * window)`, so CNA's presentation modes are expressible directly as a destination rectangle
     * rather than needing a scaling pass of CNA's own. This value is the whole of that decision,
     * and is computed by the pure functions below so the arithmetic is testable without a device.
     */
    struct PresentationLayout
    {
        /** @brief Back-buffer width -- the logical resolution game code draws in. */
        int logicalWidth = 0;
        /** @brief Back-buffer height -- the logical resolution game code draws in. */
        int logicalHeight = 0;
        /** @brief Window width in pixels the layout was computed against. */
        int windowWidth = 0;
        /** @brief Window height in pixels the layout was computed against. */
        int windowHeight = 0;
        /** @brief Left edge of the presented rectangle, in window pixels. */
        int destinationX = 0;
        /** @brief Top edge of the presented rectangle, in window pixels. */
        int destinationY = 0;
        /** @brief Width of the presented rectangle, in window pixels. */
        int destinationWidth = 0;
        /** @brief Height of the presented rectangle, in window pixels. */
        int destinationHeight = 0;
    };

    /**
     * @brief Resolves the logical (back-buffer) size a presentation mode asks for.
     *
     * Every mode but `FixedHeightDynamicWidth` draws at exactly the preferred resolution; that one
     * keeps the preferred height and derives the width from the window's aspect ratio, matching
     * the XNA/Windows Phone behaviour where a wider device simply shows more horizontal content.
     *
     * @param mode         Presentation policy.
     * @param preferredWidth  Requested back-buffer width.
     * @param preferredHeight Requested back-buffer height.
     * @param windowWidth  Current window width in pixels.
     * @param windowHeight Current window height in pixels.
     * @param logicalWidth  Filled with the resolved logical width (never below 1).
     * @param logicalHeight Filled with the resolved logical height (never below 1).
     */
    inline void ResolveLogicalSize(CnaPresentationMode mode, int preferredWidth,
                                   int preferredHeight, int windowWidth, int windowHeight,
                                   int& logicalWidth, int& logicalHeight)
    {
        logicalWidth = preferredWidth > 0 ? preferredWidth : 1;
        logicalHeight = preferredHeight > 0 ? preferredHeight : 1;

        if (mode == CnaPresentationMode::FixedHeightDynamicWidth && windowHeight > 0 &&
            windowWidth > 0)
        {
            const double derived = static_cast<double>(windowWidth) *
                                   static_cast<double>(logicalHeight) /
                                   static_cast<double>(windowHeight);
            const int rounded = static_cast<int>(derived + 0.5);
            logicalWidth = rounded > 0 ? rounded : 1;
        }
    }

    /**
     * @brief Computes the window rectangle a logical back buffer is presented into.
     *
     * `NativeBackBuffer` presents 1:1 at the window origin; `Stretch` fills the window ignoring
     * aspect ratio; `Letterbox` fits and centres (adding bars); `Overscan` fills and centres
     * (cropping edges); `FixedHeightDynamicWidth` uses the Letterbox fit, which by construction
     * lands exactly on the window once `ResolveLogicalSize` has derived the width.
     *
     * @param mode          Presentation policy.
     * @param logicalWidth  Back-buffer width.
     * @param logicalHeight Back-buffer height.
     * @param windowWidth   Window width in pixels.
     * @param windowHeight  Window height in pixels.
     * @return The resolved layout.
     */
    [[nodiscard]] inline PresentationLayout ComputePresentationLayout(CnaPresentationMode mode,
                                                                      int logicalWidth,
                                                                      int logicalHeight,
                                                                      int windowWidth,
                                                                      int windowHeight)
    {
        PresentationLayout layout;
        layout.logicalWidth = logicalWidth > 0 ? logicalWidth : 1;
        layout.logicalHeight = logicalHeight > 0 ? logicalHeight : 1;
        layout.windowWidth = windowWidth > 0 ? windowWidth : layout.logicalWidth;
        layout.windowHeight = windowHeight > 0 ? windowHeight : layout.logicalHeight;

        const double logicalW = static_cast<double>(layout.logicalWidth);
        const double logicalH = static_cast<double>(layout.logicalHeight);
        const double windowW = static_cast<double>(layout.windowWidth);
        const double windowH = static_cast<double>(layout.windowHeight);

        switch (mode)
        {
            case CnaPresentationMode::NativeBackBuffer:
                layout.destinationX = 0;
                layout.destinationY = 0;
                layout.destinationWidth = layout.logicalWidth;
                layout.destinationHeight = layout.logicalHeight;
                return layout;

            case CnaPresentationMode::Stretch:
                layout.destinationX = 0;
                layout.destinationY = 0;
                layout.destinationWidth = layout.windowWidth;
                layout.destinationHeight = layout.windowHeight;
                return layout;

            case CnaPresentationMode::Overscan:
            {
                const double scaleX = windowW / logicalW;
                const double scaleY = windowH / logicalH;
                const double scale = scaleX > scaleY ? scaleX : scaleY;
                layout.destinationWidth = static_cast<int>(logicalW * scale + 0.5);
                layout.destinationHeight = static_cast<int>(logicalH * scale + 0.5);
                layout.destinationX = (layout.windowWidth - layout.destinationWidth) / 2;
                layout.destinationY = (layout.windowHeight - layout.destinationHeight) / 2;
                return layout;
            }

            case CnaPresentationMode::Letterbox:
            case CnaPresentationMode::FixedHeightDynamicWidth:
            default:
            {
                const double scaleX = windowW / logicalW;
                const double scaleY = windowH / logicalH;
                const double scale = scaleX < scaleY ? scaleX : scaleY;
                layout.destinationWidth = static_cast<int>(logicalW * scale + 0.5);
                layout.destinationHeight = static_cast<int>(logicalH * scale + 0.5);
                layout.destinationX = (layout.windowWidth - layout.destinationWidth) / 2;
                layout.destinationY = (layout.windowHeight - layout.destinationHeight) / 2;
                return layout;
            }
        }
    }

    /**
     * @brief Maps a window-space point into logical (game) coordinates.
     *
     * @param layout  Active presentation layout.
     * @param windowX Window-space X.
     * @param windowY Window-space Y.
     * @param logX    Filled with the logical X.
     * @param logY    Filled with the logical Y.
     * @return False when the layout has a degenerate destination rectangle.
     */
    [[nodiscard]] inline bool WindowToLogical(const PresentationLayout& layout, float windowX,
                                              float windowY, float& logX, float& logY)
    {
        if (layout.destinationWidth <= 0 || layout.destinationHeight <= 0)
        {
            return false;
        }
        logX = (windowX - static_cast<float>(layout.destinationX)) *
               static_cast<float>(layout.logicalWidth) /
               static_cast<float>(layout.destinationWidth);
        logY = (windowY - static_cast<float>(layout.destinationY)) *
               static_cast<float>(layout.logicalHeight) /
               static_cast<float>(layout.destinationHeight);
        return true;
    }

    /**
     * @brief Maps a logical (game) point back into window space -- the inverse of WindowToLogical.
     *
     * @param layout  Active presentation layout.
     * @param logX    Logical X.
     * @param logY    Logical Y.
     * @param windowX Filled with the window-space X.
     * @param windowY Filled with the window-space Y.
     * @return False when the layout has a degenerate logical size.
     */
    [[nodiscard]] inline bool LogicalToWindow(const PresentationLayout& layout, float logX,
                                              float logY, float& windowX, float& windowY)
    {
        if (layout.logicalWidth <= 0 || layout.logicalHeight <= 0)
        {
            return false;
        }
        windowX = static_cast<float>(layout.destinationX) +
                  logX * static_cast<float>(layout.destinationWidth) /
                      static_cast<float>(layout.logicalWidth);
        windowY = static_cast<float>(layout.destinationY) +
                  logY * static_cast<float>(layout.destinationHeight) /
                      static_cast<float>(layout.logicalHeight);
        return true;
    }
}
