// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform::Sdl3::Detail {

    /**
     * @brief Emulates a window-local pointer warp on browser platforms that cannot move the
     *        operating-system cursor.
     *
     * The browser keeps its physical pointer in place. This state maps later physical movement
     * relative to the point at which the game requested a warp into the requested virtual
     * coordinate space, preserving XNA mouse-look loops that recenter every frame.
     */
    class BrowserMouseWarp
    {
    public:
        /**
         * @brief Starts or replaces a virtual warp.
         *
         * @param rawX Current browser-reported window x coordinate.
         * @param rawY Current browser-reported window y coordinate.
         * @param targetX X coordinate requested by the game.
         * @param targetY Y coordinate requested by the game.
         */
        void Anchor(float rawX, float rawY, int targetX, int targetY);

        /**
         * @brief Maps a browser-reported pointer position into the virtual warped coordinates.
         *
         * @param rawX Current browser-reported window x coordinate.
         * @param rawY Current browser-reported window y coordinate.
         * @param x Receives the emulated XNA window x coordinate.
         * @param y Receives the emulated XNA window y coordinate.
         */
        void Apply(float rawX, float rawY, int& x, int& y) const;

        /** @brief Disables the virtual warp and returns to raw window coordinates. */
        void Reset();

        /** @brief Gets whether a virtual warp is active. @return True after Anchor until Reset. */
        [[nodiscard]] bool IsActive() const;

    private:
        float rawAnchorX_ = 0.0f;
        float rawAnchorY_ = 0.0f;
        int targetX_ = 0;
        int targetY_ = 0;
        bool active_ = false;
    };

} // namespace CNA::Platform::Sdl3::Detail
