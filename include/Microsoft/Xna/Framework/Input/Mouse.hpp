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

        /** @brief Gets or sets the native window handle used for mouse state queries. */
        static std::uintptr_t WindowHandle;

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

        /** @brief When true, mouse motion is reported as relative delta rather than absolute position. */
        NOXNA static bool IsRelativeMouseModeEXT;

        /** @brief Internal: game window width used for coordinate scaling. */
        NOXNA static int INTERNAL_WindowWidth;
        /** @brief Internal: game window height used for coordinate scaling. */
        NOXNA static int INTERNAL_WindowHeight;
        /** @brief Internal: back buffer width used for coordinate scaling. */
        NOXNA static int INTERNAL_BackBufferWidth;
        /** @brief Internal: back buffer height used for coordinate scaling. */
        NOXNA static int INTERNAL_BackBufferHeight;
        /** @brief Internal: accumulated scroll wheel value. */
        NOXNA static int INTERNAL_MouseWheel;

        /**
         * @brief Internal: dispatches the ClickedEXT event for the given button index.
         * @param button The button index that was clicked.
         */
        NOXNA static void INTERNAL_onClicked(int button);
    };
}
