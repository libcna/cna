// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include "KeyboardState.hpp"
#include "Keys.hpp"
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Allows getting keystrokes from keyboard.
     */
    class Keyboard
    {
    public:
        Keyboard() = delete;

        /**
         * @brief Returns the current keyboard state.
         * @return The current keyboard state.
         */
        static KeyboardState GetState();

        /**
         * @brief Returns the current keyboard state for a given player.
         * @param playerIndex Player index of the keyboard.
         * @return The current keyboard state.
         */
        static KeyboardState GetState(Microsoft::Xna::Framework::PlayerIndex playerIndex);

        /**
         * @brief Returns the Keys value corresponding to the given hardware scancode (FNA extension).
         * @param scancode The scancode to translate.
         * @return The corresponding Keys value.
         */
        NOXNA static Keys GetKeyFromScancodeEXT(Keys scancode);
    };
}
