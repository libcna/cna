//
// Created by robertvokac on 5/28/25.
//
#pragma once
#include "GamePadState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"


namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Provides access to gamepad input snapshots.
     *
     * @note Status: PARTIAL
     */
    class GamePad
    {
    public:
        /**
         * @brief Returns a snapshot of the current gamepad state for a player.
         *
         * @note Status: PARTIAL
         */
        static GamePadState GetState(PlayerIndex playerIndex);
    };
}
