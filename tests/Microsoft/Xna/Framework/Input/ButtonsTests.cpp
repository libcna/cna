// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cstdint>

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"

using Microsoft::Xna::Framework::Input::Buttons;

namespace
{
    constexpr std::uint32_t U(Buttons b) { return static_cast<std::uint32_t>(b); }
}

// Every core XNA Buttons bit is pinned; these are ABI and must never be renumbered.
TEST(ButtonsTest, CoreXnaValuesMatchXnaBitConstants)
{
    EXPECT_EQ(U(Buttons::DPadUp),               0x00000001u);
    EXPECT_EQ(U(Buttons::DPadDown),             0x00000002u);
    EXPECT_EQ(U(Buttons::DPadLeft),             0x00000004u);
    EXPECT_EQ(U(Buttons::DPadRight),            0x00000008u);
    EXPECT_EQ(U(Buttons::Start),                0x00000010u);
    EXPECT_EQ(U(Buttons::Back),                 0x00000020u);
    EXPECT_EQ(U(Buttons::LeftStick),            0x00000040u);
    EXPECT_EQ(U(Buttons::RightStick),           0x00000080u);
    EXPECT_EQ(U(Buttons::LeftShoulder),         0x00000100u);
    EXPECT_EQ(U(Buttons::RightShoulder),        0x00000200u);
    EXPECT_EQ(U(Buttons::BigButton),            0x00000800u);
    EXPECT_EQ(U(Buttons::A),                    0x00001000u);
    EXPECT_EQ(U(Buttons::B),                    0x00002000u);
    EXPECT_EQ(U(Buttons::X),                    0x00004000u);
    EXPECT_EQ(U(Buttons::Y),                    0x00008000u);
    EXPECT_EQ(U(Buttons::LeftThumbstickLeft),   0x00200000u);
    EXPECT_EQ(U(Buttons::RightTrigger),         0x00400000u);
    EXPECT_EQ(U(Buttons::LeftTrigger),          0x00800000u);
    EXPECT_EQ(U(Buttons::RightThumbstickUp),    0x01000000u);
    EXPECT_EQ(U(Buttons::RightThumbstickDown),  0x02000000u);
    EXPECT_EQ(U(Buttons::RightThumbstickRight), 0x04000000u);
    EXPECT_EQ(U(Buttons::RightThumbstickLeft),  0x08000000u);
    EXPECT_EQ(U(Buttons::LeftThumbstickUp),     0x10000000u);
    EXPECT_EQ(U(Buttons::LeftThumbstickDown),   0x20000000u);
    EXPECT_EQ(U(Buttons::LeftThumbstickRight),  0x40000000u);
}

// The FNA-extension bits (paddles / misc / touchpad) occupy the gaps in the XNA layout.
TEST(ButtonsTest, FnaExtensionValuesMatchTheExtensionBits)
{
    EXPECT_EQ(U(Buttons::Misc1EXT),    0x00000400u);
    EXPECT_EQ(U(Buttons::Paddle1EXT),  0x00010000u);
    EXPECT_EQ(U(Buttons::Paddle2EXT),  0x00020000u);
    EXPECT_EQ(U(Buttons::Paddle3EXT),  0x00040000u);
    EXPECT_EQ(U(Buttons::Paddle4EXT),  0x00080000u);
    EXPECT_EQ(U(Buttons::TouchPadEXT), 0x00100000u);
}

TEST(ButtonsTest, BitwiseOperatorsCombineMaskAndComplementFlags)
{
    constexpr Buttons combined = Buttons::A | Buttons::B;
    EXPECT_EQ(U(combined), U(Buttons::A) | U(Buttons::B));
    EXPECT_EQ(U(combined & Buttons::A), U(Buttons::A));
    EXPECT_EQ(U(combined & Buttons::X), 0u);

    Buttons acc = Buttons::A;
    acc |= Buttons::Y;
    EXPECT_EQ(U(acc), U(Buttons::A) | U(Buttons::Y));
    acc &= Buttons::Y;
    EXPECT_EQ(U(acc), U(Buttons::Y));

    EXPECT_EQ(U(~Buttons::A), ~U(Buttons::A));
}
