// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Hands the keyboard, the pads' buttons and the mouse buttons to the system UI (the
     * Guide) or back to the game. While the system owns them, the game's Keyboard, GamePad and
     * Mouse reads report nothing pressed (pads stay connected, sticks and the pointer keep
     * moving); whatever is still held when the system lets go stays hidden from the game until it
     * is released, so the key that closed the Guide never reaches the game.
     *
     * @param owns Whether the system UI owns input now.
     */
    void setSystemOwnsInput(bool owns);

    /** @brief Whether the system UI owns input. @return Owns. */
    [[nodiscard]] bool systemOwnsInput();

    /** @brief The keyboard as the system UI sees it, whoever owns input. @return State. */
    [[nodiscard]] Microsoft::Xna::Framework::Input::KeyboardState systemKeyboardState();

    /** @brief A pad as the system UI sees it. @param player Pad. @return State. */
    [[nodiscard]] Microsoft::Xna::Framework::Input::GamePadState systemGamePadState(Microsoft::Xna::Framework::PlayerIndex player);

    /** @brief The mouse as the system UI sees it (relative motion is left for the game).
     * @return State. */
    [[nodiscard]] Microsoft::Xna::Framework::Input::MouseState systemMouseState();
}
