//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include <vector>

#include "TouchLocation.hpp"
#include "CNA/Prop.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchCollection {
    private: std::vector<TouchLocation> touches;

        DEF_PROP(int, Count, getter1, setter0, member0, static0, constret0, ref0, constmet0)

        std::vector<TouchLocation>::iterator begin();
        std::vector<TouchLocation>::iterator end();
        TouchCollection();
    };
}

