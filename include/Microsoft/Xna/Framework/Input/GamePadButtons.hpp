//
// Created by robertvokac on 5/28/25.
//

#pragma once
#include "ButtonState.hpp"
#include "Keys.hpp"
#include "CNA/Prop.hpp"


namespace Microsoft::Xna::Framework::Input {
    struct GamePadButtons {
    public:
    private:
        DEF_PROP(ButtonState, Back, getter1, setter0, member1, static0, constret1, ref1, constmet1)

        GamePadButtons();
    };
}
