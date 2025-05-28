//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPAD_H
#define GAMEPAD_H
#include "GamePadState.h"
#include "Microsoft/Xna/Framework/PlayerIndex.h"


namespace Microsoft::Xna::Framework::Input {
class GamePad {
public: static GamePadState GetState(PlayerIndex playerIndex);
};
}



#endif //GAMEPAD_H
