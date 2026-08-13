// SPDX-License-Identifier: MS-PL
//
// Task 782 / PLAT-86: integration test proving touch/gesture wiring works end-to-end from the
// platform-neutral event entry point (PlatformInputBridge::ProcessEvent -> TouchPanel
// -> GestureDetector -> TouchPanel::ReadGesture), not just via GestureDetector's direct API
// (already covered by GestureDetectorTests.cpp).

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Platform::TouchEvent;
using CNA::Platform::TouchEventKind;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    constexpr int DisplaySize = 1000;

    CNA::Platform::PlatformEvent fingerEvent(
        const TouchEventKind kind,
        const std::uint64_t fingerId,
        const float x,
        const float y,
        const float dx = 0.0f,
        const float dy = 0.0f,
        const float pressure = 0.0f)
    {
        TouchEvent event{};
        event.kind = kind;
        event.fingerId = fingerId;
        event.x = x;
        event.y = y;
        event.deltaX = dx;
        event.deltaY = dy;
        event.pressure = pressure;
        return event;
    }

    struct PlatformInputBridgeTouchGestureTest : ::testing::Test
    {
        void SetUp() override
        {
            // Central reset (task 887/888) gives every input subsystem a deterministic clean
            // baseline — including restoring the REAL gesture clock — so these real-timing gesture
            // tests are order-independent under --gtest_shuffle. (They previously used a fragile
            // press/release hack that did NOT reset the leaked test-clock mode, so a shuffled-
            // earlier test that used the manual clock could make Tap/Flick fail intermittently.)
            // Set the display size AFTER the reset, which zeroes it.
            InputManager::ResetAllForTests();
            TouchPanel::setDisplayWidthProperty(DisplaySize);
            TouchPanel::setDisplayHeightProperty(DisplaySize);
        }

        void TearDown() override
        {
            InputManager::ResetAllForTests();
        }
    };
}

// INPUT-TOUCH-024: gesture positions and touch-state positions share ONE coordinate basis — the logical
// (virtual back-buffer) space. GraphicsDevice sets TouchPanel::DisplayWidth/Height to virtualWidth/Height;
// the gesture path scales normalized platform coords by DisplayWidth/Height (round(x*W, y*H)), and the
// touch-state path (to_touch_pixel_position → to_logical_position) maps into the same logical space. This
// pins that a Tap's pixel position divided by the display metric equals the normalized point the
// touch-state snapshot reports for the same finger — i.e. both encode the identical logical point.
// (Headless has no window, so GetState reports the raw normalized coord; the gesture pixel is that coord
// scaled by DisplayWidth/Height, so gesturePos/metric == statePos is the basis-equality invariant.)
TEST_F(PlatformInputBridgeTouchGestureTest, GestureAndTouchStateShareTheLogicalCoordinateBasis)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
    constexpr float nx = 0.5f, ny = 0.25f;

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 6001, nx, ny));
    const TouchCollection s = TouchPanel::GetState();
    ASSERT_EQ(s.getCountProperty(), 1);
    const Microsoft::Xna::Framework::Vector2 statePos = s[0].getPositionProperty();

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 6001, nx, ny));
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const Microsoft::Xna::Framework::Vector2 gesturePos = TouchPanel::ReadGesture().getPositionProperty();

    // Same logical point: the gesture pixel position is the normalized state position scaled by the same
    // DisplayWidth/Height, so dividing back out recovers the normalized coordinate exactly.
    EXPECT_FLOAT_EQ(gesturePos.X / static_cast<float>(DisplaySize), statePos.X);
    EXPECT_FLOAT_EQ(gesturePos.Y / static_cast<float>(DisplaySize), statePos.Y);
    EXPECT_FLOAT_EQ(gesturePos.X, nx * static_cast<float>(DisplaySize));
    EXPECT_FLOAT_EQ(gesturePos.Y, ny * static_cast<float>(DisplaySize));
}

// P5-014: at startup, before GraphicsDevice publishes the display size, TouchPanel.DisplayWidth/Height
// are 0. Touch PRESENCE must still be tracked (the bridge records it via window/normalized coords,
// independent of the display metric), but GESTURES must be suppressed until a valid display size exists —
// otherwise a touch would scale to a bogus (0,0)-corner gesture. This is an intentional, documented
// startup divergence (touch tracked, gesture suppressed), NOT an unexpected one; gestures resume the
// moment the display size is published.
TEST_F(PlatformInputBridgeTouchGestureTest, TouchBeforeDisplaySizeIsKnownTracksTouchButSuppressesGestures)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
    TouchPanel::setDisplayWidthProperty(0);   // simulate pre-GraphicsDevice startup
    TouchPanel::setDisplayHeightProperty(0);

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 9001, 0.5f, 0.5f));
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 1)           // presence tracked despite 0 display
        << "touch presence must be tracked even before the display size is published";

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 9001, 0.5f, 0.5f));
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty())          // no bogus (0,0) corner gesture
        << "gestures must be suppressed until a valid display size exists";

    // Recovery: once the display size is published, gestures resume for a fresh finger.
    TouchPanel::setDisplayWidthProperty(DisplaySize);
    TouchPanel::setDisplayHeightProperty(DisplaySize);
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 9002, 0.4f, 0.6f));
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 9002, 0.4f, 0.6f));
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty())
        << "gestures must resume once the display size is known";
}

TEST_F(PlatformInputBridgeTouchGestureTest, FingerDownUpThroughProcessEventProducesTap)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 5001, 0.5f, 0.5f));
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 5001, 0.5f, 0.5f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Tap);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().X, 500.0f);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().Y, 500.0f);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(PlatformInputBridgeTouchGestureTest, FingerMotionThroughProcessEventProducesFlick)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Flick);

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 5002, 0.0f, 0.0f));
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Motion, 5002, 0.5f, 0.0f, 0.5f, 0.0f));
    TouchPanel::Update(); // Baseline: no prior updateTimestamp yet, so no velocity computed.

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Motion, 5002, 1.0f, 0.0f, 0.5f, 0.0f));
    TouchPanel::Update(); // Computes a velocity far above MIN_FLICK_VELOCITY (500px in ~10ms).

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 5002, 1.0f, 0.0f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Flick);
    EXPECT_GE(sample.getDeltaProperty().Length(), 100.0f); // MIN_FLICK_VELOCITY (GestureDetector.cpp)
}

// Task 872: drive Down/Motion/Up touch events through the real bridge entry point and verify
// TouchPanel::GetState()'s previous-location behaviour through the public API (GetState falls back
// to TouchPanel's event-driven snapshot, which carries previous state/position — task 870).
TEST_F(PlatformInputBridgeTouchGestureTest, FingerEventsExposePreviousLocationThroughTouchPanelGetState)
{
    // DOWN -> GetState: Pressed, no previous. Capture the reported position.
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 7001, 0.25f, 0.75f));
    Microsoft::Xna::Framework::Vector2 pressedPos;
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Pressed);
        TouchLocation prev;
        EXPECT_FALSE(s[0].TryGetPreviousLocation(prev));
        pressedPos = s[0].getPositionProperty();
    }
    TouchPanel::Update(); // frame boundary (INP-AUD-001): previous becomes the Pressed location

    // MOTION -> GetState: Moved, previous is the pressed location.
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Motion, 7001, 0.5f, 0.9f, 0.25f, 0.15f));
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
        TouchLocation prev;
        ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
        EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Pressed);
        EXPECT_EQ(prev.getPositionProperty(), pressedPos);
    }
    TouchPanel::Update(); // frame boundary: previous becomes the Moved location

    // UP -> GetState: Released, previous still present; then removed once the frame advances.
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 7001, 0.5f, 0.9f));
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Released);
        TouchLocation prev;
        EXPECT_TRUE(s[0].TryGetPreviousLocation(prev));
    }
}

// Task 892/893: a cancelled touch must release exactly like an Up event — it may
// not leave a permanently pressed/moved touch, and it must free the internal finger mapping.
TEST_F(PlatformInputBridgeTouchGestureTest, FingerCanceledReleasesTouchLikeFingerUp)
{
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 8001, 0.5f, 0.5f));
    {
        const TouchCollection down = TouchPanel::GetState();
        ASSERT_EQ(down.getCountProperty(), 1);
        EXPECT_EQ(down[0].getStateProperty(), TouchLocationState::Pressed);
    }

    // Cancel instead of lifting the finger.
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Cancelled, 8001, 0.5f, 0.5f));

    // The touch is reported Released exactly once (not stuck Pressed/Moved), then disappears once
    // the frame advances.
    {
        const TouchCollection afterCancel = TouchPanel::GetState();
        ASSERT_EQ(afterCancel.getCountProperty(), 1);
        EXPECT_EQ(afterCancel[0].getStateProperty(), TouchLocationState::Released)
            << "a canceled finger must transition to Released, not stay Pressed/Moved";
    }
    TouchPanel::Update();
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0)
        << "a canceled finger must not remain tracked forever";
}

// Task 892: after a cancel, a platform may reuse the same finger id for a brand-new press; the internal
// finger->touch mapping must have been freed so the new press is a fresh Pressed touch, not a
// continuation.
TEST_F(PlatformInputBridgeTouchGestureTest, FingerIdReusableAfterCancel)
{
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 8100, 0.1f, 0.1f));
    (void)TouchPanel::GetState();
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Cancelled, 8100, 0.1f, 0.1f));
    (void)TouchPanel::GetState(); // reports the Released touch (pure read)
    TouchPanel::Update();         // retires it (RemoveAfterSnapshot)

    // Same platform finger id pressed again -> a fresh Pressed touch.
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 8100, 0.2f, 0.2f));
    const TouchCollection reuse = TouchPanel::GetState();
    ASSERT_EQ(reuse.getCountProperty(), 1);
    EXPECT_EQ(reuse[0].getStateProperty(), TouchLocationState::Pressed);
}

// INPUT-GESTURE-012: a real cancelled event arriving mid-drag must not wedge the gesture
// state machine — the detector must recover so a subsequent independent finger still produces a Tap.
// (At the detector a cancel is a Release, so the interrupted drag also reports DragComplete; this
// test is about clean recovery, so those interim gestures are drained.)
TEST_F(PlatformInputBridgeTouchGestureTest, FingerCanceledMidDragRecoversAndAllowsAFreshTap)
{
    TouchPanel::setEnabledGesturesProperty(
        GestureType::Tap | GestureType::FreeDrag | GestureType::DragComplete);

    auto drainGestures = []
    {
        while (TouchPanel::getIsGestureAvailableProperty())
            (void)TouchPanel::ReadGesture();
    };

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 8300, 0.5f, 0.5f));
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Motion, 8300, 0.6f, 0.5f, 0.1f, 0.0f));
    drainGestures(); // discard the FreeDrag from the motion.

    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Cancelled, 8300, 0.6f, 0.5f));
    drainGestures(); // discard whatever the cancel produced (DragComplete).

    // Fresh, independent finger -> a clean Tap, proving the state machine wasn't left dragging.
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Down, 8301, 0.2f, 0.2f));
    PlatformInputBridge::ProcessEvent(fingerEvent(TouchEventKind::Up, 8301, 0.2f, 0.2f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::Tap);
}

// N-006: platform finger pressure flows end-to-end (ProcessEvent -> TouchPanel::GetState)
// and is exposed via TouchLocation::getPressureEXT; a later MOTION event updates it.
TEST_F(PlatformInputBridgeTouchGestureTest, FingerPressureIsSurfacedThroughGetStateGetPressureEXT)
{
    PlatformInputBridge::ProcessEvent(
        fingerEvent(TouchEventKind::Down, 7001, 0.5f, 0.5f, 0.0f, 0.0f, 0.75f));
    const TouchCollection down = TouchPanel::GetState();
    ASSERT_EQ(down.getCountProperty(), 1);
    EXPECT_FLOAT_EQ(down[0].getPressureEXT(), 0.75f);

    PlatformInputBridge::ProcessEvent(
        fingerEvent(TouchEventKind::Motion, 7001, 0.6f, 0.5f, 0.1f, 0.0f, 0.30f));
    const TouchCollection moved = TouchPanel::GetState();
    ASSERT_EQ(moved.getCountProperty(), 1);
    EXPECT_FLOAT_EQ(moved[0].getPressureEXT(), 0.30f);
}
