//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include "CNA/Prop.hpp"


namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchPanelCapabilities {

    public:
        TouchPanelCapabilities();
        DEF_PROP(bool, IsConnected, getter1, setter0, member1, static0, constret1, ref1, constmet1)
};
}

