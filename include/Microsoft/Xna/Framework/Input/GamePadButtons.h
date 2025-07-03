//
// Created by robertvokac on 5/28/25.
//

#ifndef GAMEPADBUTTONS_H
#define GAMEPADBUTTONS_H
#include "ButtonState.h"
#include "Keys.h"
#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Input {
    struct GamePadButtons {
    public:
    private:
        DEF_PROP(ButtonState, Back, getter1, setter0, member1, static0, constret1, ref1, constmet1)


        GamePadButtons();
    };
}


#endif //GAMEPADBUTTONS_H
