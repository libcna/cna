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
    /// Allows retrieval of keystrokes from a keyboard input device.
    class Keyboard
    {
    public:
        Keyboard() = delete;

        /// Returns a snapshot of current keyboard state.
        static KeyboardState GetState();

        /// Returns a snapshot of current keyboard state for the given player.
        /// In XNA 4.0 this ignores playerIndex and returns the shared state.
        static KeyboardState GetState(Microsoft::Xna::Framework::PlayerIndex playerIndex);

        /// Returns the Keys value corresponding to the given hardware scancode (FNA extension).
        NOXNA static Keys GetKeyFromScancodeEXT(Keys scancode);
    };
}
