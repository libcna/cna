//
// Created by robertvokac on 5/25/25.
//

#ifndef MOUSESTATE_H
#define MOUSESTATE_H
#include "ButtonState.h"



namespace Microsoft::Xna::Framework::Input {
    struct MouseState {
    public:
        DEF_PROP_AUTO(ButtonState, LeftButton, ButtonState::Released)
        DEF_PROP_AUTO(int, X, 0)
        DEF_PROP_AUTO(int, Y, 0)

        MouseState():
            IMPL_PROP_AUTO(ButtonState, LeftButton),
            IMPL_PROP_AUTO_READONLY(int, X),
            IMPL_PROP_AUTO_READONLY(int, Y) {
        }
    };
}


#endif //MOUSESTATE_H
