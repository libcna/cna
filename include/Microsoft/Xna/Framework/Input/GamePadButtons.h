//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPADBUTTONS_H
#define GAMEPADBUTTONS_H
#include "ButtonState.h"



namespace Microsoft::Xna::Framework::Input {
struct GamePadButtons {
public:
    DEF_PROP_AUTO(ButtonState, Back, ButtonState::Released)
    GamePadButtons():
    IMPL_PROP_AUTO(ButtonState, Back){

    }
};
}



#endif //GAMEPADBUTTONS_H
