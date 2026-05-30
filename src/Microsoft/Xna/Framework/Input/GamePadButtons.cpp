//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"

namespace Microsoft::Xna::Framework::Input
{
    IMPL_PROP(ButtonState, A, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons, nothing)
    IMPL_PROP(ButtonState, B, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons, nothing)
    IMPL_PROP(ButtonState, X, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons, nothing)
    IMPL_PROP(ButtonState, Y, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons, nothing)

    IMPL_PROP(ButtonState, Back, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, Start, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, LeftShoulder, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, RightShoulder, getter1, setter0, member0, static0, constret1, ref1, constmet1,
              GamePadButtons, nothing)
    IMPL_PROP(ButtonState, LeftStick, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, RightStick, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, DPadUp, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, DPadDown, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, DPadLeft, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)
    IMPL_PROP(ButtonState, DPadRight, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons,
              nothing)

    GamePadButtons::GamePadButtons()
        : A_(ButtonState::Released),
          B_(ButtonState::Released),
          X_(ButtonState::Released),
          Y_(ButtonState::Released),
          Back_(ButtonState::Released),
          Start_(ButtonState::Released),
          LeftShoulder_(ButtonState::Released),
          RightShoulder_(ButtonState::Released),
          LeftStick_(ButtonState::Released),
          RightStick_(ButtonState::Released),
          DPadUp_(ButtonState::Released),
          DPadDown_(ButtonState::Released),
          DPadLeft_(ButtonState::Released),
          DPadRight_(ButtonState::Released)
    {
    }

    GamePadButtons::GamePadButtons(
        const ButtonState a,
        const ButtonState b,
        const ButtonState x,
        const ButtonState y,
        const ButtonState back,
        const ButtonState start,
        const ButtonState leftShoulder,
        const ButtonState rightShoulder,
        const ButtonState leftStick,
        const ButtonState rightStick,
        const ButtonState dPadUp,
        const ButtonState dPadDown,
        const ButtonState dPadLeft,
        const ButtonState dPadRight
    )
        : A_(a),
          B_(b),
          X_(x),
          Y_(y),
          Back_(back),
          Start_(start),
          LeftShoulder_(leftShoulder),
          RightShoulder_(rightShoulder),
          LeftStick_(leftStick),
          RightStick_(rightStick),
          DPadUp_(dPadUp),
          DPadDown_(dPadDown),
          DPadLeft_(dPadLeft),
          DPadRight_(dPadRight)
    {
    }
}
