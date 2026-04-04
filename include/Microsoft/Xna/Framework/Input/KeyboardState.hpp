//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "Keys.hpp"

namespace Microsoft::Xna::Framework::Input {

struct KeyboardState {
    bool IsKeyDown(Keys key);
};

}

