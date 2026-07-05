// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"

using Microsoft::Xna::Framework::Input::GamePadDeadZone;

// IndependentAxes is the GetState default; the numeric order matches XNA/FNA.
TEST(GamePadDeadZoneTest, ValuesMatchXnaSequentialConstants)
{
    EXPECT_EQ(static_cast<int>(GamePadDeadZone::None), 0);
    EXPECT_EQ(static_cast<int>(GamePadDeadZone::IndependentAxes), 1);
    EXPECT_EQ(static_cast<int>(GamePadDeadZone::Circular), 2);
}
