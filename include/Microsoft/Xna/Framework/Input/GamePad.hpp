//
// Created by robertvokac on 5/28/25.
//
#pragma once
#include "GamePadState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"


namespace Microsoft::Xna::Framework::Input {
class GamePad {
public: static GamePadState GetState(PlayerIndex playerIndex);
};
}

