// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <chrono>
#include <thread>

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    // Mirrors GestureDetector.cpp's anonymous-namespace constants (not exposed via header).
    constexpr float MOVE_THRESHOLD = 35.0f;
    constexpr float MIN_FLICK_VELOCITY = 100.0f;

    constexpr int DisplaySize = 1000;

    void DrainGestures()
    {
        while (TouchPanel::getIsGestureAvailableProperty())
        {
            (void)TouchPanel::ReadGesture();
        }
    }

    // GestureDetector keeps its state machine in file-static variables with no reset
    // hook. A neutral press/release cycle (with all gestures disabled) reliably drives
    // it back to its idle NONE / NO_FINGER resting state between tests, as long as the
    // test itself has already released every finger it pressed (see class comment).
    void ResetGestureDetector()
    {
        TouchPanel::setEnabledGesturesProperty(GestureType::None);
        TouchPanel::INTERNAL_onTouchEvent(900001, TouchLocationState::Pressed, 0.0f, 0.0f, 0.0f, 0.0f);
        TouchPanel::INTERNAL_onTouchEvent(900001, TouchLocationState::Released, 0.0f, 0.0f, 0.0f, 0.0f);
        DrainGestures();
    }

    void Sleep(int milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // Presses/moves/releases are expressed as normalized [0,1] coordinates; DisplaySize
    // makes the pixel-space math (INTERNAL_onTouchEvent rounds x*DisplayWidth) exact.
    void Press(int fingerId, float x, float y)
    {
        TouchPanel::INTERNAL_onTouchEvent(fingerId, TouchLocationState::Pressed, x, y, 0.0f, 0.0f);
    }

    void Move(int fingerId, float x, float y, float dx, float dy)
    {
        TouchPanel::INTERNAL_onTouchEvent(fingerId, TouchLocationState::Moved, x, y, dx, dy);
    }

    void Release(int fingerId, float x, float y)
    {
        TouchPanel::INTERNAL_onTouchEvent(fingerId, TouchLocationState::Released, x, y, 0.0f, 0.0f);
    }

    struct GestureDetectorTest : ::testing::Test
    {
        void SetUp() override
        {
            TouchPanel::setDisplayWidthProperty(DisplaySize);
            TouchPanel::setDisplayHeightProperty(DisplaySize);
            ResetGestureDetector();
        }

        void TearDown() override
        {
            ResetGestureDetector();
        }
    };
}

TEST_F(GestureDetectorTest, TapFiresOnQuickReleaseNearPressPosition)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);

    Press(1, 0.5f, 0.5f);
    Release(1, 0.5f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Tap);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().X, 500.0f);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().Y, 500.0f);
    EXPECT_EQ(sample.getFingerIdEXTProperty(), 1);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, DoubleTapFiresWhenSecondTapIsWithinTimingAndDistanceWindow)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::DoubleTap);

    Press(2, 0.5f, 0.5f);
    Release(2, 0.5f, 0.5f);
    Press(2, 0.5f, 0.5f);
    Release(2, 0.5f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample first = TouchPanel::ReadGesture();
    EXPECT_EQ(first.getGestureTypeProperty(), GestureType::Tap);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample second = TouchPanel::ReadGesture();
    EXPECT_EQ(second.getGestureTypeProperty(), GestureType::DoubleTap);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, DoubleTapDoesNotFireWhenSecondTapArrivesAfterTimingWindow)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::DoubleTap);

    Press(3, 0.5f, 0.5f);
    Release(3, 0.5f, 0.5f);

    Sleep(350); // DoubleTap's window is 300ms.

    Press(3, 0.5f, 0.5f);
    Release(3, 0.5f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample first = TouchPanel::ReadGesture();
    EXPECT_EQ(first.getGestureTypeProperty(), GestureType::Tap);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample second = TouchPanel::ReadGesture();
    EXPECT_EQ(second.getGestureTypeProperty(), GestureType::Tap);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, HoldFiresAfterFingerIsHeldForAtLeastOneSecond)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Hold);

    Press(4, 0.5f, 0.5f);

    Sleep(1150); // Threshold is 1s; leave comfortable margin for scheduling jitter.
    TouchPanel::Update();

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Hold);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().X, 500.0f);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().Y, 500.0f);

    Release(4, 0.5f, 0.5f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, HoldDoesNotFireBeforeOneSecondElapses)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Hold);

    Press(5, 0.5f, 0.5f);

    Sleep(200);
    TouchPanel::Update();

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(5, 0.5f, 0.5f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, HorizontalDragFiresWhenMovementIsPredominantlyHorizontal)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::HorizontalDrag);

    Press(6, 0.5f, 0.5f);
    Move(6, 0.6f, 0.5f, 0.1f, 0.0f); // pixel delta (100, 0): exceeds MOVE_THRESHOLD, ax > ay.

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::HorizontalDrag);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().X, 100.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().Y, 0.0f);

    Release(6, 0.6f, 0.5f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, VerticalDragFiresWhenMovementIsPredominantlyVertical)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::VerticalDrag);

    Press(7, 0.5f, 0.5f);
    Move(7, 0.5f, 0.6f, 0.0f, 0.1f); // pixel delta (0, 100): exceeds MOVE_THRESHOLD, ay > ax.

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::VerticalDrag);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().X, 0.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().Y, 100.0f);

    Release(7, 0.5f, 0.6f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, FreeDragFiresForDiagonalMovementWhenOnlyFreeDragIsEnabled)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag);

    Press(8, 0.5f, 0.5f);
    Move(8, 0.6f, 0.6f, 0.1f, 0.1f); // pixel delta (100, 100): diagonal, exceeds MOVE_THRESHOLD.

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::FreeDrag);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().X, 100.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().Y, 100.0f);

    Release(8, 0.6f, 0.6f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, DragDoesNotStartBelowMoveThreshold)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::HorizontalDrag);

    Press(9, 0.5f, 0.5f);
    Move(9, 0.51f, 0.5f, 0.01f, 0.0f); // pixel delta (10, 0): below MOVE_THRESHOLD (35).

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(9, 0.51f, 0.5f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, FlickFiresWhenReleaseVelocityExceedsMinimumThreshold)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Flick);

    Press(10, 0.0f, 0.0f);
    Move(10, 0.5f, 0.0f, 0.5f, 0.0f); // pixel position now (500, 0).
    TouchPanel::Update();            // Baseline: no prior updateTimestamp yet, so no velocity computed.

    Sleep(10);

    Move(10, 1.0f, 0.0f, 0.5f, 0.0f); // pixel position now (1000, 0): 500px in ~10ms.
    TouchPanel::Update();             // Computes a velocity far above MIN_FLICK_VELOCITY.

    Release(10, 1.0f, 0.0f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Flick);
    EXPECT_GE(sample.getDeltaProperty().Length(), MIN_FLICK_VELOCITY);

    DrainGestures();
}

TEST_F(GestureDetectorTest, FlickDoesNotFireWithoutSufficientMovementFromPressPosition)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Flick);

    Press(11, 0.5f, 0.5f);
    Release(11, 0.5f, 0.5f); // distFromPress is 0, well below MOVE_THRESHOLD regardless of velocity.

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, PinchAndPinchCompleteFireForTwoFingerGesture)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Pinch | GestureType::PinchComplete);

    Press(20, 0.4f, 0.5f);  // pixel (400, 500).
    Press(21, 0.6f, 0.5f);  // pixel (600, 500): second finger enters PINCHING.

    Move(20, 0.3f, 0.5f, -0.1f, 0.0f); // pixel (300, 500), delta (-100, 0).

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample pinch = TouchPanel::ReadGesture();
    EXPECT_EQ(pinch.getGestureTypeProperty(), GestureType::Pinch);
    EXPECT_FLOAT_EQ(pinch.getPositionProperty().X, 300.0f);
    EXPECT_FLOAT_EQ(pinch.getPosition2Property().X, 600.0f);
    EXPECT_FLOAT_EQ(pinch.getDeltaProperty().X, -100.0f);
    EXPECT_FLOAT_EQ(pinch.getDelta2Property().X, 0.0f);
    EXPECT_EQ(pinch.getFingerIdEXTProperty(), 20);
    EXPECT_EQ(pinch.getFingerId2EXTProperty(), 21);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(20, 0.3f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample pinchComplete = TouchPanel::ReadGesture();
    EXPECT_EQ(pinchComplete.getGestureTypeProperty(), GestureType::PinchComplete);
    EXPECT_EQ(pinchComplete.getFingerIdEXTProperty(), 20);
    EXPECT_EQ(pinchComplete.getFingerId2EXTProperty(), 21);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(21, 0.6f, 0.5f);
    DrainGestures();
}
