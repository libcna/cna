// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <utility>
#include <vector>
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

TEST(TouchCollectionLimitTest, Tq019BothVectorConstructorsAcceptEightAndRejectNine)
{
    // XNA TouchCollection..ctor IL_000e-001e: the source length must be <= 8.
    std::vector<TouchLocation> eight;
    for (int id = 0; id < 8; ++id)
        eight.emplace_back(id, TouchLocationState::Pressed, Vector2(id, -id));
    const TouchCollection copied(eight);
    auto movedSource = eight;
    const TouchCollection moved(std::move(movedSource));
    ASSERT_EQ(copied.getCountProperty(), 8);
    ASSERT_EQ(moved.getCountProperty(), 8);
    EXPECT_EQ(copied[7].getIdProperty(), 7);
    EXPECT_EQ(copied[7].getPositionProperty(), Vector2(7, -7));
    EXPECT_EQ(moved[7].getIdProperty(), 7);
    EXPECT_EQ(moved[7].getPositionProperty(), Vector2(7, -7));
    auto nine = eight;
    nine.emplace_back(8, TouchLocationState::Pressed, Vector2(8, -8));
    EXPECT_THROW((void)TouchCollection(nine), System::ArgumentOutOfRangeException);
    EXPECT_EQ(nine.size(), 9u);
    EXPECT_EQ(nine[8].getIdProperty(), 8);
    EXPECT_THROW((void)TouchCollection(std::move(nine)), System::ArgumentOutOfRangeException);
    // C++ rvalue adaptation validates before taking ownership of a refused argument.
    ASSERT_EQ(nine.size(), 9u);
    EXPECT_EQ(nine[8].getIdProperty(), 8);
}
