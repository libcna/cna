// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <type_traits>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Defines the buttons on a gamepad.
     */
    enum class Buttons : uint32_t
    {
        /** @brief Directional pad up. */
        DPadUp                = 0x00000001,
        /** @brief Directional pad down. */
        DPadDown              = 0x00000002,
        /** @brief Directional pad left. */
        DPadLeft              = 0x00000004,
        /** @brief Directional pad right. */
        DPadRight             = 0x00000008,
        /** @brief START button. */
        Start                 = 0x00000010,
        /** @brief BACK button. */
        Back                  = 0x00000020,
        /** @brief Left stick button (pressing the left stick). */
        LeftStick             = 0x00000040,
        /** @brief Right stick button (pressing the right stick). */
        RightStick            = 0x00000080,
        /** @brief Left bumper (shoulder) button. */
        LeftShoulder          = 0x00000100,
        /** @brief Right bumper (shoulder) button. */
        RightShoulder         = 0x00000200,
        /** @brief Big button. */
        BigButton             = 0x00000800,
        /** @brief A button. */
        A                     = 0x00001000,
        /** @brief B button. */
        B                     = 0x00002000,
        /** @brief X button. */
        X                     = 0x00004000,
        /** @brief Y button. */
        Y                     = 0x00008000,
        /** @brief Left stick is towards the left. */
        LeftThumbstickLeft    = 0x00200000,
        /** @brief Right trigger. */
        RightTrigger          = 0x00400000,
        /** @brief Left trigger. */
        LeftTrigger           = 0x00800000,
        /** @brief Right stick is towards up. */
        RightThumbstickUp     = 0x01000000,
        /** @brief Right stick is towards down. */
        RightThumbstickDown   = 0x02000000,
        /** @brief Right stick is towards the right. */
        RightThumbstickRight  = 0x04000000,
        /** @brief Right stick is towards the left. */
        RightThumbstickLeft   = 0x08000000,
        /** @brief Left stick is towards up. */
        LeftThumbstickUp      = 0x10000000,
        /** @brief Left stick is towards down. */
        LeftThumbstickDown    = 0x20000000,
        /** @brief Left stick is towards the right. */
        LeftThumbstickRight   = 0x40000000,
        /** @brief Miscellaneous button 1 (FNA extension). */
        Misc1EXT              = 0x00000400,
        /** @brief Paddle button 1 (FNA extension). */
        Paddle1EXT            = 0x00010000,
        /** @brief Paddle button 2 (FNA extension). */
        Paddle2EXT            = 0x00020000,
        /** @brief Paddle button 3 (FNA extension). */
        Paddle3EXT            = 0x00040000,
        /** @brief Paddle button 4 (FNA extension). */
        Paddle4EXT            = 0x00080000,
        /** @brief Touchpad button (FNA extension). */
        TouchPadEXT           = 0x00100000,
    };

    /**
     * @brief Combines two Buttons values using bitwise OR.
     * @param left The left operand.
     * @param right The right operand.
     * @return The combined Buttons value.
     */
    [[nodiscard]] constexpr Buttons operator|(Buttons left, Buttons right)
    {
        using U = std::underlying_type_t<Buttons>;
        return static_cast<Buttons>(static_cast<U>(left) | static_cast<U>(right));
    }

    /**
     * @brief Masks two Buttons values using bitwise AND.
     * @param left The left operand.
     * @param right The right operand.
     * @return The masked Buttons value.
     */
    [[nodiscard]] constexpr Buttons operator&(Buttons left, Buttons right)
    {
        using U = std::underlying_type_t<Buttons>;
        return static_cast<Buttons>(static_cast<U>(left) & static_cast<U>(right));
    }

    /**
     * @brief Inverts a Buttons value using bitwise NOT.
     * @param b The Buttons value to invert.
     * @return The inverted Buttons value.
     */
    [[nodiscard]] constexpr Buttons operator~(Buttons b)
    {
        using U = std::underlying_type_t<Buttons>;
        return static_cast<Buttons>(~static_cast<U>(b));
    }

    /**
     * @brief Combines a Buttons value with another using bitwise OR-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr Buttons& operator|=(Buttons& left, Buttons right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Masks a Buttons value with another using bitwise AND-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr Buttons& operator&=(Buttons& left, Buttons right)
    {
        left = left & right;
        return left;
    }
}
