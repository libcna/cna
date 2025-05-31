//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPADSTATE_H
#define GAMEPADSTATE_H
#include "GamePadButtons.h"


namespace Microsoft::Xna::Framework::Input {
    struct GamePadState {
    public:
        dgetter(GamePadButtons, Buttons)
        GamePadState() {}
    };
}



#endif //GAMEPADSTATE_H
