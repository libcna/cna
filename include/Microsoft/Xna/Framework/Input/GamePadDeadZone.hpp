// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Input
{
    /// Specifies a type of dead zone processing to apply to analog sticks when calling GetState.
    enum class GamePadDeadZone
    {
        /// Values are returned as raw values; no dead zone processing is applied.
        None,
        /// The X and Y positions of each stick are compared against the dead zone independently.
        IndependentAxes,
        /// The combined X and Y position of each stick is compared to the dead zone.
        Circular,
    };
}
