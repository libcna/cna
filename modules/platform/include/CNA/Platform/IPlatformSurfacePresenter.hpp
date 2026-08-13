// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Platform {

    class IPlatformWindow;

    /** @brief How a presented image is fitted to a window whose size differs from the image's. */
    enum class PresentScaleMode
    {
        /** @brief Stretch to fill, ignoring aspect ratio. */
        Stretch,
        /** @brief Scale to fit while preserving aspect ratio, padding the remainder. */
        Letterbox,
        /** @brief Scale to cover while preserving aspect ratio, cropping the excess. */
        Overscan,
        /** @brief Present at 1:1 without scaling, centred. */
        None,
        /** @brief Present at 1:1 without scaling, aligned to the top-left corner. */
        Native
    };

    /** @brief How a presented image is filtered when scaled. */
    enum class PresentFilter
    {
        /** @brief Nearest-neighbour: preserves hard pixel edges. */
        Nearest,
        /** @brief Bilinear: smooths on scaling. */
        Linear
    };

    /** @brief One frame's worth of finished CPU pixels. */
    struct SurfaceFrame
    {
        /** @brief RGBA8 pixels, row-major, 4 bytes per pixel. Never null. */
        const std::uint8_t* pixels = nullptr;
        /** @brief Image width in pixels. */
        int width = 0;
        /** @brief Image height in pixels. */
        int height = 0;
        /**
         * @brief Bytes per row.
         *
         * Zero means tightly packed (`width * 4`). A non-zero value lets a rasteriser present a
         * sub-rectangle of a larger buffer without copying it first.
         */
        int strideBytes = 0;
    };

    /**
     * @brief Presents finished CPU-rasterised pixels to a window.
     *
     * This interface exists because PLAT-3's audit found `SKIA` and `BLEND2D` rasterise entirely
     * on the CPU and use `SDL_Renderer` for one purpose only: getting the finished image onto the
     * screen, with letterbox scaling and vsync. Without somewhere for that to go, both would have
     * stayed permanently allowlisted for want of an interface rather than for any real SDL
     * dependence.
     *
     * "Present a pixel buffer to a window" is a genuine platform capability, not an SDL
     * peculiarity: SDL2 provides it, SDL 1.2 provides it as a software surface, Win32 has
     * `StretchDIBits`, and a terminal can satisfy it by quantising the buffer into a character
     * grid — which is exactly how the Phase 10 terminal platform attaches without needing a
     * renderer of its own.
     *
     * ### This is not a drawing API
     *
     * The platform never participates in rendering. It receives one finished frame per present
     * and puts it on the screen. A renderer that called through here per primitive would be
     * violating the plan's performance contract; the dispatch is once per frame, by construction.
     *
     * Gated by the `SurfacePresentation` capability.
     */
    class IPlatformSurfacePresenter
    {
    public:
        /** @brief Destroys the presenter and releases whatever it holds for the window. */
        virtual ~IPlatformSurfacePresenter() = default;

        /**
         * @brief Sets how a frame is fitted to the window when their sizes differ.
         *
         * @param mode The scaling mode.
         * @param filter The filter used when scaling.
         */
        virtual void SetScaleMode(PresentScaleMode mode, PresentFilter filter) = 0;

        /**
         * @brief Sets whether presentation waits for vertical blank.
         *
         * @param enabled True to synchronise with vertical blank.
         * @return True if the setting was applied; false if the platform cannot honour it, in
         * which case presentation continues unsynchronised rather than failing.
         */
        virtual bool SetVSync(bool enabled) = 0;

        /**
         * @brief Presents one finished frame.
         *
         * @param frame The pixels to present.
         * @throws PlatformException If the frame is malformed (null pixels, non-positive size)
         * or presentation failed.
         */
        virtual void Present(const SurfaceFrame& frame) = 0;

        /**
         * @brief Gets the size the presenter is currently targeting, in physical pixels.
         *
         * A caller sizes its own rasterisation buffer from this. It tracks the window and can
         * change between presents, so it is read per frame rather than cached.
         *
         * @param width Receives the target width.
         * @param height Receives the target height.
         */
        virtual void GetTargetSize(int& width, int& height) const = 0;
    };

} // namespace CNA::Platform
