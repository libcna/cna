// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"

using Microsoft::Xna::Framework::Input::GamePadType;

// The XNA GamePadType values are sequential from Unknown=0; order is API-observable.
TEST(GamePadTypeTest, ValuesMatchXnaSequentialConstants)
{
    EXPECT_EQ(static_cast<int>(GamePadType::Unknown), 0);
    EXPECT_EQ(static_cast<int>(GamePadType::GamePad), 1);
    EXPECT_EQ(static_cast<int>(GamePadType::Wheel), 2);
    EXPECT_EQ(static_cast<int>(GamePadType::ArcadeStick), 3);
    EXPECT_EQ(static_cast<int>(GamePadType::FlightStick), 4);
    EXPECT_EQ(static_cast<int>(GamePadType::DancePad), 5);
    EXPECT_EQ(static_cast<int>(GamePadType::Guitar), 6);
    EXPECT_EQ(static_cast<int>(GamePadType::AlternateGuitar), 7);
    EXPECT_EQ(static_cast<int>(GamePadType::DrumKit), 8);
    EXPECT_EQ(static_cast<int>(GamePadType::BigButtonPad), 9);
}
