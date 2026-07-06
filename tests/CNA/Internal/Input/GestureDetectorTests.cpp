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

// P6-005(b): moving beyond MOVE_THRESHOLD with only Tap enabled cancels the tap. No drag is enabled, so
// the detector leaves HOLDING for NONE once the finger crosses 35px, and the release emits no Tap.
TEST_F(GestureDetectorTest, TapDoesNotFireWhenFingerMovesBeyondMoveThreshold)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);

    Press(50, 0.5f, 0.5f);
    Move(50, 0.6f, 0.5f, 0.1f, 0.0f); // 100px move, above MOVE_THRESHOLD (35)
    Release(50, 0.6f, 0.5f);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

// P6-005(c): a finger held >= 1s is a Hold candidate, not a Tap — the tap gate is `held < 1s`, so a
// release at/after the threshold emits no Tap even with only Tap enabled.
TEST_F(GestureDetectorTest, TapDoesNotFireWhenHeldForOneSecondOrMore)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);

    Press(51, 0.5f, 0.5f);
    GestureDetector::AdvanceTestClockMilliseconds(1000); // reaches the 1s tap cutoff (held < 1s is false)
    Release(51, 0.5f, 0.5f);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

// P6-005(d): with Tap (and DoubleTap) disabled, a quick press+release emits nothing.
TEST_F(GestureDetectorTest, TapDoesNotFireWhenTapGestureIsDisabled)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Hold); // anything but Tap/DoubleTap

    Press(52, 0.5f, 0.5f);
    Release(52, 0.5f, 0.5f);

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

// P6-006(c): a second tap within the 300ms window but beyond MOVE_THRESHOLD is NOT a double tap — the
// double-tap distance gate is `dist <= MOVE_THRESHOLD` (35px) measured from the first tap's press. The
// second press falls through to a plain Tap.
TEST_F(GestureDetectorTest, DoubleTapDoesNotFireWhenSecondTapIsTooFarAway)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::DoubleTap);

    Press(53, 0.5f, 0.5f);
    Release(53, 0.5f, 0.5f);
    Press(53, 0.6f, 0.5f);   // 100px away at DisplaySize 1000: within timing, beyond distance
    Release(53, 0.6f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::Tap);
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::Tap)
        << "a second tap too far from the first must be a plain Tap, not a DoubleTap";
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

// P6-006(d): with DoubleTap disabled, two quick co-located taps are just two Taps.
TEST_F(GestureDetectorTest, DoubleTapDoesNotFireWhenDoubleTapGestureIsDisabled)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap); // DoubleTap NOT enabled

    Press(54, 0.5f, 0.5f);
    Release(54, 0.5f, 0.5f);
    Press(54, 0.5f, 0.5f);
    Release(54, 0.5f, 0.5f);

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::Tap);
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::Tap);
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

// P6-007(b): moving beyond MOVE_THRESHOLD cancels a pending hold — the finger leaves HOLDING (no drag is
// enabled, so it goes to NONE), so even after 1s the Hold does not fire.
TEST_F(GestureDetectorTest, HoldDoesNotFireWhenFingerMovesBeyondMoveThreshold)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Hold);

    Press(55, 0.5f, 0.5f);
    Move(55, 0.6f, 0.5f, 0.1f, 0.0f); // 100px move > MOVE_THRESHOLD (35) leaves HOLDING
    GestureDetector::AdvanceTestClockMilliseconds(1000);
    TouchPanel::Update();

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(55, 0.6f, 0.5f);
    DrainGestures();
}

// P6-007(d): with Hold disabled, holding a finger past 1s emits nothing.
TEST_F(GestureDetectorTest, HoldDoesNotFireWhenHoldGestureIsDisabled)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap); // Hold NOT enabled

    Press(56, 0.5f, 0.5f);
    GestureDetector::AdvanceTestClockMilliseconds(1500);
    TouchPanel::Update();

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(56, 0.5f, 0.5f);
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

// P6-008(d): with only HorizontalDrag enabled, a predominantly-vertical move (ay > ax) past the threshold
// starts NO drag — vdrag/fdrag are disabled, so the detector falls through to NONE and emits nothing.
TEST_F(GestureDetectorTest, HorizontalDragRejectsPredominantlyVerticalMovement)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::HorizontalDrag);

    Press(60, 0.5f, 0.5f);
    Move(60, 0.5f, 0.6f, 0.0f, 0.1f); // pixel delta (0, 100): ay > ax
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(60, 0.5f, 0.6f);
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

// P6-009(c): a VerticalDrag that ends with a release fires DragComplete carrying the release finger id.
TEST_F(GestureDetectorTest, DragCompleteFiresAfterAVerticalDrag)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::VerticalDrag | GestureType::DragComplete);

    Press(61, 0.5f, 0.5f);
    Move(61, 0.5f, 0.6f, 0.0f, 0.1f); // starts a VerticalDrag
    DrainGestures();

    Release(61, 0.5f, 0.6f);
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::DragComplete);
    EXPECT_EQ(sample.getFingerIdEXTProperty(), 61);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

// P6-009(d): with only VerticalDrag enabled, a predominantly-horizontal move (ax > ay) past the threshold
// starts NO drag — hdrag/fdrag are disabled, so the detector falls through to NONE and emits nothing.
TEST_F(GestureDetectorTest, VerticalDragRejectsPredominantlyHorizontalMovement)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::VerticalDrag);

    Press(62, 0.5f, 0.5f);
    Move(62, 0.6f, 0.5f, 0.1f, 0.0f); // pixel delta (100, 0): ax > ay
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(62, 0.6f, 0.5f);
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

// P6-010(d): with FreeDrag disabled (and no other drag enabled), a diagonal move past the threshold starts
// no drag and emits nothing.
TEST_F(GestureDetectorTest, FreeDragDoesNotFireWhenFreeDragGestureIsDisabled)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap); // no drag gesture enabled

    Press(63, 0.5f, 0.5f);
    Move(63, 0.6f, 0.6f, 0.1f, 0.1f); // diagonal, above MOVE_THRESHOLD (35)
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(63, 0.6f, 0.6f);
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
    // P6-011(c): the Flick sample's Delta is the velocity vector — it carries direction, not just
    // magnitude. The swipe was purely +X, so velocity points +X with no vertical component.
    EXPECT_GT(sample.getDeltaProperty().X, 0.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().Y, 0.0f);

    DrainGestures();
}

// P6-011(b): moving far enough but too SLOWLY yields no flick — the flick has BOTH a distance gate
// (distFromPress > MOVE_THRESHOLD) and a velocity gate (velocity.Length() >= MIN_FLICK_VELOCITY). Here the
// distance is sufficient (40px) but the finger sat still for 1s so the velocity decays to ~0.
TEST_F(GestureDetectorTest, FlickDoesNotFireWhenReleaseVelocityIsBelowThreshold)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Flick);

    Press(70, 0.5f, 0.5f);
    Move(70, 0.54f, 0.5f, 0.04f, 0.0f); // pixel (540,500): 40px from press, above MOVE_THRESHOLD
    TouchPanel::Update();               // baseline: records position/time, no velocity yet
    GestureDetector::AdvanceTestClockMilliseconds(1000);
    TouchPanel::Update();               // 1s elapsed with no further movement -> velocity ~0
    Release(70, 0.54f, 0.5f);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

// P6-011(d): with Flick disabled, a fast swipe emits nothing (velocity is not even computed).
TEST_F(GestureDetectorTest, FlickDoesNotFireWhenFlickGestureIsDisabled)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap); // Flick NOT enabled

    Press(71, 0.0f, 0.0f);
    Move(71, 0.5f, 0.0f, 0.5f, 0.0f);
    TouchPanel::Update();
    GestureDetector::AdvanceTestClockMilliseconds(10);
    Move(71, 1.0f, 0.0f, 0.5f, 0.0f); // fast 500px in 10ms
    TouchPanel::Update();
    Release(71, 1.0f, 0.0f);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
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
    // P6-013(b): PinchComplete carries no position or delta — all four vectors are zeroed (it is a
    // terminal marker, like DragComplete).
    EXPECT_FLOAT_EQ(pinchComplete.getPositionProperty().X, 0.0f);
    EXPECT_FLOAT_EQ(pinchComplete.getPositionProperty().Y, 0.0f);
    EXPECT_FLOAT_EQ(pinchComplete.getPosition2Property().X, 0.0f);
    EXPECT_FLOAT_EQ(pinchComplete.getPosition2Property().Y, 0.0f);
    EXPECT_FLOAT_EQ(pinchComplete.getDeltaProperty().X, 0.0f);
    EXPECT_FLOAT_EQ(pinchComplete.getDelta2Property().X, 0.0f);
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

TEST_F(GestureDetectorTest, SecondFingerDuringADragInterruptsItAndBecomesAPinch)
{
    TouchPanel::setEnabledGesturesProperty(
        GestureType::FreeDrag | GestureType::Pinch | GestureType::PinchComplete);

    Press(40, 0.4f, 0.5f);
    Move(40, 0.5f, 0.5f, 0.1f, 0.0f); // 100px move: starts a FreeDrag.
    DrainGestures();                  // discard the FreeDrag sample.

    Press(41, 0.6f, 0.5f);            // second finger interrupts the drag -> PINCHING.
    Move(40, 0.3f, 0.5f, -0.1f, 0.0f);// the active finger now drives a Pinch, not a FreeDrag.

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Pinch);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().X, 300.0f);
    EXPECT_FLOAT_EQ(sample.getPosition2Property().X, 600.0f);
    EXPECT_FLOAT_EQ(sample.getDeltaProperty().X, -100.0f);
    EXPECT_EQ(sample.getFingerIdEXTProperty(), 40);
    EXPECT_EQ(sample.getFingerId2EXTProperty(), 41);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Release(40, 0.3f, 0.5f);
    Release(41, 0.6f, 0.5f);
    DrainGestures();
}

// P6-014(a): a tap must NOT be reported while a second finger is still down. With two fingers pressed,
// releasing the first leaves the other finger tracked, so OnReleased early-returns (`!fingerIds.empty()`)
// and no Tap is emitted until the whole touch ends.
TEST_F(GestureDetectorTest, TapDoesNotFireWhileASecondFingerIsStillDown)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);

    Press(80, 0.4f, 0.5f);
    Press(81, 0.6f, 0.5f);   // second finger appears
    Release(80, 0.4f, 0.5f); // first finger lifts, but 81 is still down

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty())
        << "no Tap may fire while another finger is still down";

    Release(81, 0.6f, 0.5f);
    DrainGestures();
}

TEST_F(GestureDetectorTest, DragInterruptedByASecondFingerReportsPinchCompleteNotDragComplete)
{
    TouchPanel::setEnabledGesturesProperty(
        GestureType::FreeDrag | GestureType::Pinch | GestureType::PinchComplete |
        GestureType::DragComplete);

    Press(42, 0.4f, 0.5f);
    Move(42, 0.5f, 0.5f, 0.1f, 0.0f); // starts a FreeDrag.
    DrainGestures();

    Press(43, 0.6f, 0.5f);            // second finger interrupts: drag -> pinch.

    Release(42, 0.5f, 0.5f);          // lifting the pinch reports PinchComplete...
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::PinchComplete)
        << "an interrupted drag becomes a pinch, so releasing must report PinchComplete";

    Release(43, 0.6f, 0.5f);          // ...and never a DragComplete.
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty())
        << "a drag interrupted by a pinch must not also report DragComplete";
}

TEST_F(GestureDetectorTest, GestureStateRecoversAfterADragEndsSoAFreshTapStillFires)
{
    // A mid-drag cancel reaches the detector as a Release (SdlInputBridge maps FINGER_CANCELED to
    // Released), so it is indistinguishable from a normal lift and does emit DragComplete. What
    // matters for clean termination is that no finger/state is left stuck afterwards.
    TouchPanel::setEnabledGesturesProperty(
        GestureType::Tap | GestureType::FreeDrag | GestureType::DragComplete);

    Press(44, 0.5f, 0.5f);
    Move(44, 0.6f, 0.5f, 0.1f, 0.0f); // FreeDrag starts.
    DrainGestures();

    Release(44, 0.6f, 0.5f);          // ends (or cancels) the drag.
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::DragComplete);
    DrainGestures();

    // A brand-new, independent finger must produce a clean Tap: this only holds if fingerIds and
    // activeFingerId were cleared on release (otherwise this press would look like a second finger).
    Press(45, 0.2f, 0.2f);
    Release(45, 0.2f, 0.2f);
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample tap = TouchPanel::ReadGesture();
    EXPECT_EQ(tap.getGestureTypeProperty(), GestureType::Tap);
    EXPECT_EQ(tap.getFingerIdEXTProperty(), 45);
}

// P6-015(b): GestureDetector::ResetForTests must clear the detector's internal state (activeFingerId,
// secondFingerId, fingerIds, state). Drive it into a non-idle two-finger PINCHING state, reset mid-gesture,
// then a brand-new finger must produce a clean Tap — which only holds if the stale fingers/state were wiped
// (otherwise the fresh press would be mistaken for a continuation / a second finger).
TEST_F(GestureDetectorTest, ResetForTestsClearsDetectorInternalState)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::Pinch);

    Press(90, 0.4f, 0.5f);
    Press(91, 0.6f, 0.5f); // two tracked fingers -> PINCHING (a decidedly non-idle state)

    GestureDetector::ResetForTests();   // wipe detector state...
    GestureDetector::EnableTestClock(); // ...ResetForTests returns to the real clock; restore determinism
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
    DrainGestures();

    Press(92, 0.2f, 0.2f);
    Release(92, 0.2f, 0.2f);
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample tap = TouchPanel::ReadGesture();
    EXPECT_EQ(tap.getGestureTypeProperty(), GestureType::Tap)
        << "after ResetForTests a fresh finger must tap cleanly, proving stale state was cleared";
    EXPECT_EQ(tap.getFingerIdEXTProperty(), 92);
}
