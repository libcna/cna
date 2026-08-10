// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Specifies a type of dead zone processing to apply to the controllers analog sticks
     *        when calling GetState.
     */
    enum class GamePadDeadZone
    {
        /** @brief The values of each stick are not processed and are returned by GetState as raw values.
         *         Use this if you intend to implement your own dead zone processing. */
        None,
        /** @brief The X and Y positions of each stick are compared against the dead zone independently.
         *         This is the default when calling GetState. */
        IndependentAxes,
        /** @brief The combined X and Y position of each stick is compared to the dead zone.
         *         Provides better control than IndependentAxes when the stick is used as a
         *         two-dimensional control surface, such as in a first-person game. */
        Circular,
    };
}
