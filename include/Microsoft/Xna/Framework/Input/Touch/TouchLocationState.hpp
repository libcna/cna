// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Specifies the state of a touch-screen touch-point.
    enum class TouchLocationState
    {
        /// This touch point is in an invalid state.
        Invalid,

        /// This touch point was released.
        Released,

        /// This touch point was pressed.
        Pressed,

        /// This touch point was moved.
        Moved
    };
}
