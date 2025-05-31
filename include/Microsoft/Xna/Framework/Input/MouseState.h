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
        dgetter(ButtonState, LeftButton)
        dgetter(int, X)
        dgetter(int, Y)

        MouseState();
    };
}


#endif //MOUSESTATE_H
