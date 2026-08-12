// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "CNA/CNAHelper.hpp"
#include "System/MulticastAction.hpp"

#include <cstdint>
#include <functional>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Allows reading position and button click information from the mouse.
     */
    class Mouse
    {
    public:
        Mouse() = delete;

        /**
         * @brief Gets the native window handle used for mouse state queries.
         * @return The window handle, or 0 if none has been published.
         */
        [[nodiscard]] static std::uintptr_t getWindowHandleProperty();

        /**
         * @brief Sets the native window handle used for mouse state queries.
         * @param value The window handle to associate the mouse with.
         */
        static void setWindowHandleProperty(std::uintptr_t value);

        /**
         * @brief Gets mouse state captured at the current frame boundary.
         *
         * Relative displacement is the one consume-on-read field: a second call with no new
         * motion reports zero x/y, matching FNA.
         *
         * @return The most recently published platform mouse state.
         */
        static MouseState GetState();

        /**
         * @brief Sets the mouse cursor's position relative to the game window.
         * @param x The relative horizontal position of the cursor.
         * @param y The relative vertical position of the cursor.
         */
        static void SetPosition(int x, int y);

        /**
         * @brief Sets the mouse cursor image.
         * @param cursor The cursor to display.
         */
        CNAEXT static void SetCursor(MouseCursor& cursor);

        /** @brief FNA extension: fires when a mouse button is clicked. Multicast (matches FNA's
         *         `public static Action<int> ClickedEXT`): use `+=` to add subscribers, `=` to set a
         *         single handler or `= nullptr` to clear. */
        CNAEXT static System::MulticastAction<int> ClickedEXT;

        /**
         * @brief FNA extension: gets whether mouse motion is reported as relative delta
         * rather than absolute position.
         * @return True if relative mouse mode is enabled for the current window.
         */
        CNAEXT static bool getIsRelativeMouseModeEXTProperty();

        /**
         * @brief FNA extension: sets whether mouse motion is reported as relative delta
         * rather than absolute position.
         * @param value True to enable relative mouse mode; false to disable it.
         */
        CNAEXT static void setIsRelativeMouseModeEXTProperty(bool value);

        /**
         * @brief CNAEXT/EXT: enables or disables capturing mouse events outside the window.
         * @param enabled True to capture the mouse; false to release it.
         * @return True on success; false if the platform does not support capture.
         */
        CNAEXT static bool SetCaptureEXT(bool enabled);

        /**
         * @brief CNAEXT/EXT: reads the cursor position in desktop (global) coordinates.
         * @param x Output receiving the global x coordinate.
         * @param y Output receiving the global y coordinate.
         */
        CNAEXT static void GetGlobalPositionEXT(int& x, int& y);

        /**
         * @brief CNAEXT/EXT: moves the cursor to a desktop (global) coordinate.
         * @param x The global x coordinate to warp to.
         * @param y The global y coordinate to warp to.
         * @return True on success; false if the platform does not support global warp.
         */
        CNAEXT static bool WarpGlobalEXT(int x, int y);

        /**
         * @brief Internal: dispatches the ClickedEXT event for the given button index.
         * @param button The button index that was clicked.
         */
        CNAEXT static void INTERNAL_onClicked(int button);

        /**
         * @brief Test-only: resets Mouse's process-wide static state (window ids, ClickedEXT).
         *
         * Does not touch the cursor or platform mouse snapshot (reset those separately).
         */
        CNAEXT static void ResetForTests();

    private:
        /** @brief Backing store for the WindowHandle property. */
        static std::uintptr_t windowHandle_;
        /** @brief Platform event id corresponding to WindowHandle, or zero. */
        static std::uint32_t windowId_;
    };
}
