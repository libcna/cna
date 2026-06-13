// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Specifies the state of a touch-screen touch point.
     */
    enum class TouchLocationState
    {
        /** @brief This touch point is in an invalid state. */
        Invalid,

        /** @brief This touch point was released. */
        Released,

        /** @brief This touch point was pressed. */
        Pressed,

        /** @brief This touch point was moved. */
        Moved
    };
}
