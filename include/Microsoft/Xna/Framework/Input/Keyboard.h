//
// Created by robertvokac on 5/26/25.
//

#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "KeyboardState.h"


namespace Microsoft::Xna::Framework::Input {
    struct KeyboardState;

    class Keyboard {
    public:
        static KeyboardState GetState() { return KeyboardState(); }
    };
}


#endif //KEYBOARD_H
