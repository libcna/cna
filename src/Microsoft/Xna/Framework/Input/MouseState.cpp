//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/MouseState.h"

namespace Microsoft::Xna::Framework::Input {
    igetter(ButtonState, LeftButton, MouseState)
    igetter(int, X, MouseState)
    igetter(int, Y, MouseState)

    MouseState::MouseState(): LeftButton_(Released), X_(0), Y_(0) {
    }
}
