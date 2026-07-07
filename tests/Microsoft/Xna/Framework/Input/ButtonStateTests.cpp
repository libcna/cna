// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"

using Microsoft::Xna::Framework::Input::ButtonState;

// Pins the XNA 4.0 numeric values so a future renumbering (an ABI break) fails loudly.
TEST(ButtonStateTest, ValuesMatchXnaNumericConstants)
{
    EXPECT_EQ(static_cast<int>(ButtonState::Released), 0);
    EXPECT_EQ(static_cast<int>(ButtonState::Pressed), 1);
}
