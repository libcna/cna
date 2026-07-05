// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"

using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

// Values are sequential from Invalid=0; several call sites compare these numerically.
TEST(TouchLocationStateTest, ValuesMatchXnaSequentialConstants)
{
    EXPECT_EQ(static_cast<int>(TouchLocationState::Invalid), 0);
    EXPECT_EQ(static_cast<int>(TouchLocationState::Released), 1);
    EXPECT_EQ(static_cast<int>(TouchLocationState::Pressed), 2);
    EXPECT_EQ(static_cast<int>(TouchLocationState::Moved), 3);
}
