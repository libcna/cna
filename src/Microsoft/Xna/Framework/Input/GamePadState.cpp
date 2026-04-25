//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
namespace Microsoft::Xna::Framework::Input {
    IMPL_PROP(GamePadButtons, Buttons, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadState, nothing)
    IMPL_PROP(Microsoft::Xna::Framework::Vector2, LeftThumbstick, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadState, nothing)
    IMPL_PROP(Microsoft::Xna::Framework::Vector2, RightThumbstick, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadState, nothing)
    IMPL_PROP(float, LeftTrigger, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadState, nothing)
    IMPL_PROP(float, RightTrigger, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadState, nothing)
    IMPL_PROP(bool, IsConnected, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadState, nothing)

    GamePadState::GamePadState()
        : Buttons_(),
          LeftThumbstick_(0.0f, 0.0f),
          RightThumbstick_(0.0f, 0.0f),
          LeftTrigger_(0.0f),
          RightTrigger_(0.0f),
          IsConnected_(false) {
    }

    GamePadState::GamePadState(
        const GamePadButtons& buttons,
        const Microsoft::Xna::Framework::Vector2& leftThumbstick,
        const Microsoft::Xna::Framework::Vector2& rightThumbstick,
        const float leftTrigger,
        const float rightTrigger,
        const bool isConnected
    )
        : Buttons_(buttons),
          LeftThumbstick_(leftThumbstick),
          RightThumbstick_(rightThumbstick),
          LeftTrigger_(leftTrigger),
          RightTrigger_(rightTrigger),
          IsConnected_(isConnected) {
    }
}
