// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/KeyState.hpp"

using Microsoft::Xna::Framework::Input::KeyState;

// Dedicated value coverage (KeyboardStateTests exercises KeyState only indirectly).
TEST(KeyStateTest, ValuesMatchXnaNumericConstants)
{
    EXPECT_EQ(static_cast<int>(KeyState::Up), 0);
    EXPECT_EQ(static_cast<int>(KeyState::Down), 1);
}
