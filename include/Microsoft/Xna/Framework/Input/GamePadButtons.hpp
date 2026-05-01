//
// Created by robertvokac on 5/28/25.
//

#pragma once
#include "ButtonState.hpp"
#include "SharpRuntime/Prop.hpp"


namespace Microsoft::Xna::Framework::Input {
    /**
     * @brief Snapshot of digital gamepad buttons.
     *
     * @note Status: PARTIAL
     */
    struct GamePadButtons {
    public:
        DEF_PROP(ButtonState, A, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, B, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, X, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, Y, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, Back, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, Start, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, LeftShoulder, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, RightShoulder, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, LeftStick, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, RightStick, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, DPadUp, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, DPadDown, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, DPadLeft, getter1, setter0, member1, static0, constret1, ref1, constmet1)
        DEF_PROP(ButtonState, DPadRight, getter1, setter0, member1, static0, constret1, ref1, constmet1)

        /**
         * @brief Initializes all gamepad buttons as released.
         */
        GamePadButtons();

        /**
         * @brief Initializes all mapped gamepad buttons.
         */
        GamePadButtons(
            ButtonState a,
            ButtonState b,
            ButtonState x,
            ButtonState y,
            ButtonState back,
            ButtonState start,
            ButtonState leftShoulder,
            ButtonState rightShoulder,
            ButtonState leftStick,
            ButtonState rightStick,
            ButtonState dPadUp,
            ButtonState dPadDown,
            ButtonState dPadLeft,
            ButtonState dPadRight
        );
    };
}
