//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPADSTATE_H
#define GAMEPADSTATE_H
#include "GamePadButtons.h"


namespace Microsoft::Xna::Framework::Input {
    struct GamePadState {
    public:
        DEF_PROP(GamePadButtons, Buttons, getter1, setter0, member1, static0, constret1, ref1, constmet1)

        GamePadState();
    };
}


#endif //GAMEPADSTATE_H
