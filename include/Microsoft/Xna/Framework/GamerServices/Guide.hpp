//
// Created by robertvokac on 5/28/25.
//
#pragma once

#include "CNA/Prop.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

/**
 * @note Status: Stub
 */
namespace Microsoft::Xna::Framework::GamerServices {
    class Guide {
    public:
        static void Show(const PlayerIndex& playerIndex);
        DEF_PROP(bool, IsTrialMode, getter1, setter0, member0, static1, constret1, ref1, constmet0)

    };
}

