// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

namespace CNA::Platform::Sdl2 {

    /**
     * @brief SDL2 display enumeration (plans/plan_platform.md PLAT-SDL2-5).
     *
     * SDL2 addresses displays by a dense 0-based index while the contract carries an opaque
     * `std::uint32_t` id. The two are not interchangeable: `GraphicsAdapter` treats id 0 as "no
     * display" and falls back to a default mode, so the index is offset by one here and mapped
     * back on every lookup. That keeps SDL3's own 1-based `SDL_DisplayID` convention as the
     * contract-wide meaning of the field rather than making callers know which platform they are on.
     */
    class Sdl2Displays final : public IPlatformDisplays
    {
    public:
        /** @brief Gets every connected display. @return The displays; empty when none. */
        [[nodiscard]] std::vector<DisplayInfo> GetDisplays() const override;
        /**
         * @brief Gets the display a window is on.
         * @param window The window to locate.
         * @param display Receives the display; untouched on false.
         * @return True when the window belongs to this implementation and SDL2 knows its display.
         */
        [[nodiscard]] bool TryGetDisplayForWindow(const IPlatformWindow& window,
                                                  DisplayInfo& display) const override;
        /**
         * @brief Gets the unobscured interactive region of a window.
         *
         * Always false: SDL2 has no safe-area query at all — the concept arrived with SDL3's
         * `SDL_GetWindowSafeArea`. Refusing is the contract's answer for a platform that cannot
         * tell; returning the full client bounds would be a fabricated one.
         *
         * @param window The window whose client area is being described.
         * @param safeArea Left untouched.
         * @return False, always.
         */
        [[nodiscard]] bool TryGetSafeAreaForWindow(const IPlatformWindow& window,
                                                   WindowBounds& safeArea) const override;
        /**
         * @brief Gets the modes a display supports.
         * @param displayId Which display, in the offset convention described on this class.
         * @return The supported modes; empty when the display is unknown or exposes none.
         */
        [[nodiscard]] std::vector<DisplayMode> GetDisplayModes(std::uint32_t displayId) const override;
        /**
         * @brief Gets the mode a display is currently using.
         * @param displayId Which display, in the offset convention described on this class.
         * @param mode Receives the current mode; untouched on false.
         * @return True when the display is known and its current mode is available.
         */
        [[nodiscard]] bool TryGetCurrentDisplayMode(std::uint32_t displayId,
                                                    DisplayMode& mode) const override;
        /** @brief Gets whether the host screen saver may activate. */
        [[nodiscard]] bool IsScreenSaverEnabled() const override;
        /**
         * @brief Allows or prevents the host screen saver from activating.
         * @param enabled True to allow screen saving.
         */
        void SetScreenSaverEnabled(bool enabled) override;
    };

} // namespace CNA::Platform::Sdl2
