// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformMouse.hpp"

#include "WaylandProtocols.hpp"

#include <memory>

namespace CNA::Platform::Wayland {

    /** @brief One cursor image from a theme, as a buffer the compositor can show. */
    struct WaylandCursorImage
    {
        /** @brief The buffer; owned by the theme. */
        wl_buffer* buffer = nullptr;
        /** @brief Width in pixels. */
        int width = 0;
        /** @brief Height in pixels. */
        int height = 0;
        /** @brief Hot spot x. */
        int hotSpotX = 0;
        /** @brief Hot spot y. */
        int hotSpotY = 0;
    };

    /**
     * @brief The user's Xcursor theme through libwayland-cursor, loaded at run time (D-1, D-20).
     *
     * The fallback for a compositor without `wp_cursor_shape_manager_v1` (Weston, older ones): the
     * client draws the theme's images itself, as every Wayland toolkit did before cursor-shape.
     * The theme and size are `XCURSOR_THEME` and `XCURSOR_SIZE`, the variables every desktop sets
     * for exactly this. Only the first image of an animated cursor is used.
     */
    class WaylandCursorTheme
    {
    public:
        /**
         * @brief Loads the theme.
         * @param shm The `wl_shm` its buffers are made with.
         * @return The theme, or null where libwayland-cursor or any cursor theme is missing.
         */
        [[nodiscard]] static std::unique_ptr<WaylandCursorTheme> Load(wl_shm* shm);

        /** @brief Destroys the theme and its buffers. */
        ~WaylandCursorTheme();

        WaylandCursorTheme(const WaylandCursorTheme&) = delete;
        WaylandCursorTheme& operator=(const WaylandCursorTheme&) = delete;

        /**
         * @brief Gets a system cursor's image.
         * @param cursor The shape.
         * @param image Receives it.
         * @return False when the theme has no image under any of the shape's names.
         */
        [[nodiscard]] bool Get(SystemCursor cursor, WaylandCursorImage& image) const;

    private:
        WaylandCursorTheme() = default;

        void* theme_ = nullptr;
    };

} // namespace CNA::Platform::Wayland
