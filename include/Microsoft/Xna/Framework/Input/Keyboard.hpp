//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "KeyboardState.hpp"


namespace Microsoft::Xna::Framework::Input {
    class Keyboard {
    public:
        static KeyboardState GetState() { return KeyboardState(); }
    };
}

