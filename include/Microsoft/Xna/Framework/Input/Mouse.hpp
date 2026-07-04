// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "CNA/CNAHelper.hpp"

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
         * @brief Gets mouse state information including position and button presses.
         * @return The current mouse state.
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
        NOXNA static void SetCursor(MouseCursor& cursor);

        /** @brief FNA extension: fires when a mouse button is clicked. */
        NOXNA static std::function<void(int)> ClickedEXT;

        /**
         * @brief FNA extension: gets whether mouse motion is reported as relative delta
         * rather than absolute position.
         * @return True if relative mouse mode is enabled for the current window.
         */
        NOXNA static bool getIsRelativeMouseModeEXTProperty();

        /**
         * @brief FNA extension: sets whether mouse motion is reported as relative delta
         * rather than absolute position.
         * @param value True to enable relative mouse mode; false to disable it.
         */
        NOXNA static void setIsRelativeMouseModeEXTProperty(bool value);

        /**
         * @brief Internal: dispatches the ClickedEXT event for the given button index.
         * @param button The button index that was clicked.
         */
        NOXNA static void INTERNAL_onClicked(int button);

        /**
         * @brief Test-only: resets Mouse's process-wide static state (window handle, ClickedEXT).
         *
         * Does not touch the SDL cursor or InputManager mouse state (reset those separately).
         */
        NOXNA static void ResetForTests();

    private:
        /** @brief Backing store for the WindowHandle property. */
        static std::uintptr_t windowHandle_;
    };
}
