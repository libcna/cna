// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Platform::Common {

    /** @brief Where a presented frame lands inside its target, in target pixels. */
    struct PresentRect
    {
        /** @brief Left edge; negative when an overscanned frame is cropped on the left. */
        int x = 0;
        /** @brief Top edge; negative when an overscanned frame is cropped at the top. */
        int y = 0;
        /** @brief Width the frame is scaled to. */
        int width = 0;
        /** @brief Height the frame is scaled to. */
        int height = 0;
    };

    /**
     * @brief Where a frame of one size lands inside a target of another, for a scale mode.
     *
     * Shared by the X11 and Wayland presenters (plans/plan_wayland.md WAYLAND-0013): fitting a
     * picture into a window is arithmetic, not a window-system feature.
     *
     * @param mode The scale mode.
     * @param frameWidth The frame's width.
     * @param frameHeight The frame's height.
     * @param targetWidth The target's width.
     * @param targetHeight The target's height.
     * @return The rectangle the frame is drawn into; it may extend past the target (overscan).
     */
    [[nodiscard]] PresentRect ComputePresentRect(PresentScaleMode mode, int frameWidth, int frameHeight,
                                                 int targetWidth, int targetHeight);

    /**
     * @brief Scales an RGBA frame to a rectangle's size, handing every destination pixel to a
     * store function in one pass.
     *
     * Nearest-neighbour, except that downscaling with `PresentFilter::Linear` averages a 2x2 box:
     * dropping whole source pixels looks like noise on text and fine detail, while interpolating a
     * magnified image would blur pixel art the caller may have intended. Everything that does not
     * change across a frame is decided once per frame -- the first X11 presenter derived each
     * channel's shift and divided for the source column per pixel and spent 30 ms on a 1080p frame
     * (plans/plan_native_platform_validation.md NPV-0116).
     *
     * @tparam Store Called as `store(x, y, red, green, blue)` for x in [0, width), y in [0, height).
     * @param frame The validated frame.
     * @param stride The frame's effective row stride in bytes.
     * @param width Destination width.
     * @param height Destination height.
     * @param filter The filter.
     * @param columns Scratch storage for the source column of each destination column.
     * @param store The per-pixel store; it decides the pixel format.
     */
    template <typename Store>
    void ScaleSurfaceFrame(const SurfaceFrame& frame, const int stride, const int width, const int height,
                           const PresentFilter filter, std::vector<int>& columns, Store&& store)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        columns.resize(static_cast<std::size_t>(width));
        for (int x = 0; x < width; ++x)
        {
            columns[static_cast<std::size_t>(x)] = std::min(frame.width - 1, x * frame.width / std::max(1, width));
        }
        const bool box = filter == PresentFilter::Linear && width < frame.width && height < frame.height;
        for (int y = 0; y < height; ++y)
        {
            const int sourceY = std::min(frame.height - 1, y * frame.height / std::max(1, height));
            const std::uint8_t* row =
                frame.pixels + static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(stride);
            const std::uint8_t* nextRow = frame.pixels + static_cast<std::size_t>(std::min(frame.height - 1, sourceY + 1)) *
                                                             static_cast<std::size_t>(stride);
            for (int x = 0; x < width; ++x)
            {
                const int sourceX = columns[static_cast<std::size_t>(x)];
                const std::uint8_t* pixel = row + static_cast<std::size_t>(sourceX) * 4u;
                unsigned int red = pixel[0];
                unsigned int green = pixel[1];
                unsigned int blue = pixel[2];
                if (box)
                {
                    const std::size_t nextX = static_cast<std::size_t>(std::min(frame.width - 1, sourceX + 1)) * 4u;
                    const std::uint8_t* p10 = row + nextX;
                    const std::uint8_t* p01 = nextRow + static_cast<std::size_t>(sourceX) * 4u;
                    const std::uint8_t* p11 = nextRow + nextX;
                    red = (red + p10[0] + p01[0] + p11[0]) / 4u;
                    green = (green + p10[1] + p01[1] + p11[1]) / 4u;
                    blue = (blue + p10[2] + p01[2] + p11[2]) / 4u;
                }
                store(x, y, red, green, blue);
            }
        }
    }

} // namespace CNA::Platform::Common
