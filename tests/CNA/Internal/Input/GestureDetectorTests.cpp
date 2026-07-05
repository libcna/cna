// SPDX-License-Identifier: MS-PL
//
// Timing gestures (Hold / DoubleTap / Flick) are driven by GestureDetector's injectable test
// clock (task 830: GestureDetector::EnableTestClock + AdvanceTestClockMilliseconds) instead of
// real std::this_thread::sleep_for, making them deterministic and fast. State is reset between
// tests via GestureDetector::ResetForTests (task 824) rather than the old neutral-press hack.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/GestureDetector.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::GestureDetector;
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
            GestureDetector::ResetForTests();
            TouchPanel::setEnabledGesturesProperty(GestureType::None);
            DrainGestures();
            GestureDetector::EnableTestClock();
        }

        void TearDown() override
        {
            GestureDetector::DisableTestClock();
            GestureDetector::ResetForTests();
            TouchPanel::setEnabledGesturesProperty(GestureType::None);
            DrainGestures();
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

    GestureDetector::AdvanceTestClockMilliseconds(350); // DoubleTap's window is 300ms.

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

    GestureDetector::AdvanceTestClockMilliseconds(1000); // Threshold is >= 1s (deterministic now).
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

    GestureDetector::AdvanceTestClockMilliseconds(200);
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

    GestureDetector::AdvanceTestClockMilliseconds(10);

    Move(10, 1.0f, 0.0f, 0.5f, 0.0f); // pixel position now (1000, 0): 500px in exactly 10ms.
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

TEST_F(GestureDetectorTest, DragCompleteFiresWhenAFreeDragEndsWithRelease)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag | GestureType::DragComplete);

    Press(30, 0.5f, 0.5f);
    Move(30, 0.6f, 0.6f, 0.1f, 0.1f); // pixel delta (100, 100): starts a FreeDrag.
    DrainGestures();                  // discard the FreeDrag sample; isolate the release.

    Release(30, 0.6f, 0.6f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::DragComplete);
    EXPECT_EQ(sample.getFingerIdEXTProperty(), 30);
    EXPECT_EQ(sample.getFingerId2EXTProperty(), TouchPanel::NO_FINGER);
    // XNA/FNA DragComplete carries no position or delta.
    EXPECT_FLOAT_EQ(sample.getPositionProperty().X, 0.0f);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().Y, 0.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().X, 0.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().Y, 0.0f);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, DragCompleteFiresAfterAHorizontalDragAndCarriesReleaseFingerId)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::HorizontalDrag | GestureType::DragComplete);

    Press(31, 0.5f, 0.5f);
    Move(31, 0.6f, 0.5f, 0.1f, 0.0f); // pixel delta (100, 0): starts a HorizontalDrag.
    DrainGestures();

    Release(31, 0.6f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::DragComplete);
    EXPECT_EQ(sample.getFingerIdEXTProperty(), 31);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, DragCompleteDoesNotFireWhenFingerIsReleasedWithoutDragging)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag | GestureType::DragComplete);

    Press(32, 0.5f, 0.5f);
    Release(32, 0.5f, 0.5f); // never crossed MOVE_THRESHOLD, so no drag ever started.

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, DragCompleteDoesNotFireWhenMovementStaysBelowMoveThreshold)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag | GestureType::DragComplete);

    Press(33, 0.5f, 0.5f);
    Move(33, 0.51f, 0.51f, 0.01f, 0.01f); // pixel delta (10, 10): below MOVE_THRESHOLD (35).
    Release(33, 0.51f, 0.51f);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(GestureDetectorTest, DragCompleteDoesNotFireWhenTheGestureIsNotEnabled)
{
    // FreeDrag is enabled so a drag actually starts, but DragComplete is filtered out.
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag);

    Press(34, 0.5f, 0.5f);
    Move(34, 0.6f, 0.6f, 0.1f, 0.1f); // starts (and emits) a FreeDrag.
    DrainGestures();

    Release(34, 0.6f, 0.6f);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}
