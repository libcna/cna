//
// Created by robertvokac on 5/25/25.
//

#ifndef MOUSESTATE_H
#define MOUSESTATE_H
#include "ButtonState.h"
#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Input {
    struct MouseState {
    public:
        DEF_PROP(ButtonState, LeftButton, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(int, X, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(int, Y, getter1, setter0, member1, static0, constret1, ref1, constmet1)


        MouseState();
    };
}


#endif //MOUSESTATE_H
