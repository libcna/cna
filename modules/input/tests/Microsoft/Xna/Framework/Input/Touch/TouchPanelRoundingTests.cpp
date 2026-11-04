// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Input/GestureDetector.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::GestureDetector;
using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    struct TouchPanelRoundingTest : ::testing::Test
    {
        void SetUp() override
        {
            TouchPanel::ResetForTests();
            GestureDetector::ResetForTests();
            GestureDetector::EnableTestClock();
            TouchPanel::setDisplayWidthProperty(100);
            TouchPanel::setDisplayHeightProperty(100);
        }
        void TearDown() override
        {
            GestureDetector::ResetForTests();
            TouchPanel::ResetForTests();
        }
    };
}

TEST_F(TouchPanelRoundingTest, Tq019HalfPixelTapPositionUsesNearestEvenForBothSigns)
{
    // FNA TouchPanel.cs:147-152 uses Math.Round (ties to even).
    // +/-0.125*100 = +/-12.5 -> +/-12; +/-0.375*100 = +/-37.5 -> +/-38.
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
    for (const float sign : {1.0f, -1.0f})
    {
        TouchPanel::INTERNAL_onTouchEvent(301, TouchLocationState::Pressed, sign*.125f, sign*.375f, 0, 0);
        TouchPanel::INTERNAL_onTouchEvent(301, TouchLocationState::Released, sign*.125f, sign*.375f, 0, 0);
        ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
        const auto sample = TouchPanel::ReadGesture();
        EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Tap);
        EXPECT_EQ(sample.getPositionProperty(), Vector2(sign*12, sign*38));
        EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
    }
}

TEST_F(TouchPanelRoundingTest, Tq019HalfPixelDragDeltaUsesNearestEvenForBothSigns)
{
    // FNA TouchPanel.cs:161-165 applies the same Math.Round to both delta components.
    // .625*100=62.5 -> 62; .375*100=37.5 -> 38; delta12.5 -> 12, delta-37.5 -> -38.
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag);
    for (const float sign : {1.0f, -1.0f})
    {
        GestureDetector::ResetForTests();
        GestureDetector::EnableTestClock();
        // Position and delta describe a consistent motion: (.5,.75) -> (.625,.375).
        TouchPanel::INTERNAL_onTouchEvent(302, TouchLocationState::Pressed, sign*.5f, sign*.75f, 0, 0);
        TouchPanel::INTERNAL_onTouchEvent(302, TouchLocationState::Moved, sign*.625f, sign*.375f,
                                         sign*.125f, -sign*.375f);
        ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
        const auto sample = TouchPanel::ReadGesture();
        EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::FreeDrag);
        EXPECT_EQ(sample.getPositionProperty(), Vector2(sign*62, sign*38));
        EXPECT_EQ(sample.getDeltaProperty(), Vector2(sign*12, -sign*38));
        EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
    }
}
