// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "System/ArgumentException.hpp"

using Microsoft::Xna::Framework::DisplayOrientation;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    struct TouchPanelValidationTest : ::testing::Test
    {
        void SetUp() override { TouchPanel::ResetForTests(); }
        void TearDown() override { TouchPanel::ResetForTests(); }
    };
}

TEST_F(TouchPanelValidationTest, Tq019AllDefinedGestureMasksRoundTrip)
{
    // XNA TouchPanel.set_EnabledGestures IL_0000-0020: only bits outside 0x3ff are rejected.
    for (int mask = 0; mask <= 0x3ff; ++mask)
    {
        SCOPED_TRACE(mask);
        TouchPanel::setEnabledGesturesProperty(static_cast<GestureType>(mask));
        EXPECT_EQ(static_cast<int>(TouchPanel::getEnabledGesturesProperty()), mask);
    }
}

TEST_F(TouchPanelValidationTest, Tq019UndefinedGestureBitsThrowAndPreservePreviousMask)
{
    // XNA TouchPanel.set_EnabledGestures IL_0000-0015: ArgumentException before state assignment.
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::PinchComplete);
    for (const int mask : {0x400, 0x401, 0x800, -1})
    {
        SCOPED_TRACE(mask);
        EXPECT_THROW(TouchPanel::setEnabledGesturesProperty(static_cast<GestureType>(mask)),
                     System::ArgumentException);
        EXPECT_EQ(TouchPanel::getEnabledGesturesProperty(), GestureType::Tap | GestureType::PinchComplete);
    }
}

TEST_F(TouchPanelValidationTest, Tq019InvalidOrientationThrowsAndPreservesPreviousValue)
{
    // XNA TouchPanel.set_DisplayOrientation IL_0000-0007; Helpers.ValidateOrientation IL_0000-001a.
    for (const int value : {0, 1, 2, 4})
    {
        TouchPanel::setDisplayOrientationProperty(static_cast<DisplayOrientation>(value));
        EXPECT_EQ(static_cast<int>(TouchPanel::getDisplayOrientationProperty()), value);
    }
    TouchPanel::setDisplayOrientationProperty(DisplayOrientation::LandscapeRight);
    for (const int value : {-1, 3, 5, 6, 7, 8})
    {
        SCOPED_TRACE(value);
        EXPECT_THROW(TouchPanel::setDisplayOrientationProperty(static_cast<DisplayOrientation>(value)),
                     System::ArgumentException);
        EXPECT_EQ(TouchPanel::getDisplayOrientationProperty(), DisplayOrientation::LandscapeRight);
    }
}
