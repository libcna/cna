// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Defines the type of gamepad controller.
     */
    enum class GamePadType
    {
        /** @brief Unknown controller type. */
        Unknown,
        /** @brief Standard gamepad. */
        GamePad,
        /** @brief Racing wheel controller. */
        Wheel,
        /** @brief Arcade stick controller. */
        ArcadeStick,
        /** @brief Flight stick controller. */
        FlightStick,
        /** @brief Dance pad controller. */
        DancePad,
        /** @brief Guitar controller. */
        Guitar,
        /** @brief Alternate guitar controller. */
        AlternateGuitar,
        /** @brief Drum kit controller. */
        DrumKit,
        /** @brief Big button pad controller. */
        BigButtonPad,
    };
}
