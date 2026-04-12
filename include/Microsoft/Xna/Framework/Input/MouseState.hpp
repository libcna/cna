//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include "ButtonState.hpp"
#include "CppDotNet/Prop.hpp"

namespace Microsoft::Xna::Framework::Input {
    struct MouseState {
    public:
        DEF_PROP(ButtonState, LeftButton, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(int, X, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(int, Y, getter1, setter0, member1, static0, constret1, ref1, constmet1)

        MouseState();
    };
}
