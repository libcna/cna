//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPADSTATE_H
#define GAMEPADSTATE_H
#include "GamePadButtons.h"


namespace Microsoft::Xna::Framework::Input {
    struct GamePadState {
    public:
        DEF_PROP_AUTO(GamePadButtons, Buttons, GamePadButtons())
        GamePadState() :
        IMPL_PROP_AUTO(GamePadButtons, Buttons){}
    };
}



#endif //GAMEPADSTATE_H
