//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

namespace Microsoft::Xna::Framework::Input {
    IMPL_PROP(ButtonState, LeftButton, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(ButtonState, RightButton, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(ButtonState, MiddleButton, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(int, X, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(int, Y, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)
    IMPL_PROP(int, ScrollWheelValue, getter1, setter0, member0, static0, constret1, ref1, constmet1, MouseState, nothing)

    MouseState::MouseState():
        LeftButton_(ButtonState::Released),
        RightButton_(ButtonState::Released),
        MiddleButton_(ButtonState::Released),
        X_(0),
        Y_(0),
        ScrollWheelValue_(0) {
    }

    MouseState::MouseState(
        const int x,
        const int y,
        const ButtonState leftButton,
        const ButtonState rightButton,
        const ButtonState middleButton,
        const int scrollWheelValue
    ):
        LeftButton_(leftButton),
        RightButton_(rightButton),
        MiddleButton_(middleButton),
        X_(x),
        Y_(y),
        ScrollWheelValue_(scrollWheelValue) {
    }
}
