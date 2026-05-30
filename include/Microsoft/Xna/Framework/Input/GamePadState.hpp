//
// Created by robertvokac on 5/28/25.
//

#pragma once
#include "GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"


namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Snapshot of one gamepad state.
     *
     * @note Status: PARTIAL
     */
    struct GamePadState
    {
    public:
        DEF_PROP(GamePadButtons, Buttons, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(Microsoft::Xna::Framework::Vector2, LeftThumbstick, getter1, setter0, member1, static0, constret1,
                 ref1, constmet1)
        DEF_PROP(Microsoft::Xna::Framework::Vector2, RightThumbstick, getter1, setter0, member1, static0, constret1,
                 ref1, constmet1)
        DEF_PROP(float, LeftTrigger, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(float, RightTrigger, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(bool, IsConnected, getter1, setter0, member1, static0, constret1, ref1, constmet1)

        /**
         * @brief Initializes a disconnected gamepad snapshot.
         */
        GamePadState();

        /**
         * @brief Initializes a full gamepad snapshot.
         */
        GamePadState(
            const GamePadButtons& buttons,
            const Microsoft::Xna::Framework::Vector2& leftThumbstick,
            const Microsoft::Xna::Framework::Vector2& rightThumbstick,
            float leftTrigger,
            float rightTrigger,
            bool isConnected
        );
    };
}
