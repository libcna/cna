// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "Microsoft/Xna/Framework/Rectangle.hpp"

namespace Microsoft::Xna::Framework
{
    class GameWindow;
}

namespace CNA::Devices
{
    /**
     * @brief Reports platform display properties that
     * `Microsoft::Xna::Framework::GameWindow` does not already cover.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Deliberately does **not**
     * duplicate `GameWindow::getCurrentOrientationProperty()`/
     * `getClientBoundsProperty()`, which are already real XNA API covering
     * orientation and window bounds — this class only wraps the two genuinely new
     * platform capabilities `GameWindow` has no property for: per-window display content
     * scale and the window's safe interactive area.
     *
     * Every method here takes the game's existing `GameWindow` by reference and
     * reads from its already-created platform window. It never creates or owns a window and does
     * not depend on a native window-system type.
     */
    class DisplayInfo
    {
    public:
        /**
         * @brief Gets the display content scale for the given window.
         *
         * A combination of the window's pixel density and the display's own content
         * scale setting — the expected scale factor for displaying content in this
         * window (e.g. 2.0 on a "Retina"/high-DPI display).
         *
         * @param window The game window to query.
         * @return The content scale, or 0.0f if it could not be determined (e.g. the
         * window has no underlying platform window yet, or the platform does not support
         * this query).
         */
        [[nodiscard]] static float getContentScaleProperty(const Microsoft::Xna::Framework::GameWindow& window);

        /**
         * @brief Gets the safe interactive area within the given window.
         *
         * Some devices have portions of the window that are partially obscured or
         * not interactive (on-screen controls, curved edges, camera notches, TV
         * overscan, etc.). This is the sub-region of the window's client area that
         * is safe to place interactable content in; content outside it should still
         * render, but should not be visually important or interactable.
         *
         * @param window The game window to query.
         * @return The safe-area rectangle, in the window's client coordinates, or
         * `Rectangle::Empty` if it could not be determined.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Rectangle getSafeAreaProperty(
            const Microsoft::Xna::Framework::GameWindow& window);

    private:
        /** @brief Not instantiable — every member is static. */
        DisplayInfo() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
