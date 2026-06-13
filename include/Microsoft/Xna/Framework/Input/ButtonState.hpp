// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Specifies the state of a button on a mouse or gamepad.
     */
    enum class ButtonState
    {
        /** @brief The button is released. */
        Released,
        /** @brief The button is pressed. */
        Pressed,
    };
}
