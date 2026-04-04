//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include "MouseCursor.hpp"
#include "MouseState.hpp"


namespace Microsoft::Xna::Framework::Input {
class Mouse {
public:
    static MouseState GetState();

    static void SetCursor(MouseCursor arrow);
};
}


