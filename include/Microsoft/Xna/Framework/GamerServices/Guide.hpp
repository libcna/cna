// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/28/25.
//
#pragma once

#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

/**
 * @note Status: Stub
 */
namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Provides access to the in-game Guide overlay (trial mode detection, etc.).
     *
     * @note CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
     */
    class Guide
    {
    public:
        /**
         * @brief Shows the Guide overlay for the specified player.
         *
         * @param playerIndex Index of the player requesting the overlay.
         */
        static void Show(const PlayerIndex& playerIndex);

        /** @brief Gets or sets whether the game is running in trial mode. */
        DEF_PROP(bool, IsTrialMode, getter1, setter0, member0, static1, constret1, ref1, constmet0)
    };
}
