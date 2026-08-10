// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"

using Microsoft::Xna::Framework::Input::Touch::GestureType;

// GestureType is a [Flags] enum; each value is a distinct power of two (None = 0).
TEST(GestureTypeTest, ValuesMatchXnaFlagConstants)
{
    EXPECT_EQ(static_cast<int>(GestureType::None),           0);
    EXPECT_EQ(static_cast<int>(GestureType::Tap),            1);
    EXPECT_EQ(static_cast<int>(GestureType::DoubleTap),      2);
    EXPECT_EQ(static_cast<int>(GestureType::Hold),           4);
    EXPECT_EQ(static_cast<int>(GestureType::HorizontalDrag), 8);
    EXPECT_EQ(static_cast<int>(GestureType::VerticalDrag),   16);
    EXPECT_EQ(static_cast<int>(GestureType::FreeDrag),       32);
    EXPECT_EQ(static_cast<int>(GestureType::Pinch),          64);
    EXPECT_EQ(static_cast<int>(GestureType::Flick),          128);
    EXPECT_EQ(static_cast<int>(GestureType::DragComplete),   256);
    EXPECT_EQ(static_cast<int>(GestureType::PinchComplete),  512);
}

TEST(GestureTypeTest, BitwiseOperatorsCombineAndMaskFlags)
{
    constexpr GestureType combined = GestureType::Tap | GestureType::Hold;
    EXPECT_EQ(static_cast<int>(combined), 1 | 4);
    EXPECT_EQ(static_cast<int>(combined & GestureType::Tap), 1);
    EXPECT_EQ(static_cast<int>(combined & GestureType::Flick), 0);

    GestureType acc = GestureType::Tap;
    acc |= GestureType::Flick;
    EXPECT_EQ(static_cast<int>(acc), 1 | 128);
    acc &= GestureType::Flick;
    EXPECT_EQ(static_cast<int>(acc), 128);
}
