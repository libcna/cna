//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "CNA/Internal/Input/InputManager.hpp"

namespace Microsoft::Xna::Framework::Input {

    GamePadState GamePad::GetState(const PlayerIndex playerIndex) {
        return CNA::Internal::Input::InputManager::GetGamePadState(playerIndex);
    }

}
