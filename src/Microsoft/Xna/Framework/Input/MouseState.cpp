//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

namespace Microsoft::Xna::Framework::Input {
    IMPL_PROP(ButtonState, LeftButton, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(int, X, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(int, Y, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)

    MouseState::MouseState(): LeftButton_(Released), X_(0), Y_(0) {
    }
}
