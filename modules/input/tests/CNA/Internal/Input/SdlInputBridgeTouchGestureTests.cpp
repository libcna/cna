// SPDX-License-Identifier: MS-PL
//
// Task 782: integration test proving the Phase I2 touch/gesture wiring works end-to-end from
// the real SDL event entry point (SdlInputBridge::ProcessEvent -> TouchPanel::INTERNAL_onTouchEvent
// -> GestureDetector -> TouchPanel::ReadGesture), not just via GestureDetector's direct API
// (already covered by GestureDetectorTests.cpp).

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    constexpr int DisplaySize = 1000;

    SDL_Event fingerEvent(const Uint32 type, const SDL_FingerID fingerId,
                          const float x, const float y,
                          const float dx = 0.0f, const float dy = 0.0f,
                          const float pressure = 0.0f)
    {
        SDL_Event e{};
        e.type = type;
        e.tfinger.fingerID = fingerId;
        e.tfinger.x = x;
        e.tfinger.y = y;
        e.tfinger.dx = dx;
        e.tfinger.dy = dy;
        e.tfinger.pressure = pressure;
        return e;
    }

    struct SdlInputBridgeTouchGestureTest : ::testing::Test
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
// the gesture path scales normalized SDL coords linearly by DisplayWidth/Height (round(x*W, y*H)), and the
// touch-state path (to_touch_pixel_position → to_logical_position) maps into the same logical space. This
// pins that a Tap's pixel position divided by the display metric equals the normalized point the
// touch-state snapshot reports for the same finger — i.e. both encode the identical logical point.
// (Headless has no window, so GetState reports the raw normalized coord; the gesture pixel is that coord
// scaled by DisplayWidth/Height, so gesturePos/metric == statePos is the basis-equality invariant.)
TEST_F(SdlInputBridgeTouchGestureTest, GestureAndTouchStateShareTheLogicalCoordinateBasis)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
    constexpr float nx = 0.5f, ny = 0.25f;

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 6001, nx, ny));
    const TouchCollection s = TouchPanel::GetState();
    ASSERT_EQ(s.getCountProperty(), 1);
    const Microsoft::Xna::Framework::Vector2 statePos = s[0].getPositionProperty();

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 6001, nx, ny));
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
TEST_F(SdlInputBridgeTouchGestureTest, TouchBeforeDisplaySizeIsKnownTracksTouchButSuppressesGestures)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
    TouchPanel::setDisplayWidthProperty(0);   // simulate pre-GraphicsDevice startup
    TouchPanel::setDisplayHeightProperty(0);

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 9001, 0.5f, 0.5f));
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 1)           // presence tracked despite 0 display
        << "touch presence must be tracked even before the display size is published";

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 9001, 0.5f, 0.5f));
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty())          // no bogus (0,0) corner gesture
        << "gestures must be suppressed until a valid display size exists";

    // Recovery: once the display size is published, gestures resume for a fresh finger.
    TouchPanel::setDisplayWidthProperty(DisplaySize);
    TouchPanel::setDisplayHeightProperty(DisplaySize);
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 9002, 0.4f, 0.6f));
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 9002, 0.4f, 0.6f));
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty())
        << "gestures must resume once the display size is known";
}

TEST_F(SdlInputBridgeTouchGestureTest, FingerDownUpThroughProcessEventProducesTap)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Tap);

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 5001, 0.5f, 0.5f));
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 5001, 0.5f, 0.5f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Tap);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().X, 500.0f);
    EXPECT_FLOAT_EQ(sample.getPositionProperty().Y, 500.0f);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

TEST_F(SdlInputBridgeTouchGestureTest, FingerMotionThroughProcessEventProducesFlick)
{
    TouchPanel::setEnabledGesturesProperty(GestureType::Flick);

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 5002, 0.0f, 0.0f));
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_MOTION, 5002, 0.5f, 0.0f, 0.5f, 0.0f));
    TouchPanel::Update(); // Baseline: no prior updateTimestamp yet, so no velocity computed.

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_MOTION, 5002, 1.0f, 0.0f, 0.5f, 0.0f));
    TouchPanel::Update(); // Computes a velocity far above MIN_FLICK_VELOCITY (500px in ~10ms).

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 5002, 1.0f, 0.0f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample sample = TouchPanel::ReadGesture();
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Flick);
    EXPECT_GE(sample.getDeltaProperty().Length(), 100.0f); // MIN_FLICK_VELOCITY (GestureDetector.cpp)
}

// Task 872: drive SDL_EVENT_FINGER_DOWN/MOTION/UP through the real bridge entry point and verify
// TouchPanel::GetState()'s previous-location behaviour through the public API (GetState falls back
// to InputManager's event-driven snapshot, which now carries previous state/position — task 870).
TEST_F(SdlInputBridgeTouchGestureTest, FingerEventsExposePreviousLocationThroughTouchPanelGetState)
{
    // DOWN -> GetState: Pressed, no previous. Capture the reported position.
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 7001, 0.25f, 0.75f));
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
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_MOTION, 7001, 0.5f, 0.9f, 0.25f, 0.15f));
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
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 7001, 0.5f, 0.9f));
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Released);
        TouchLocation prev;
        EXPECT_TRUE(s[0].TryGetPreviousLocation(prev));
    }
}

// Task 892/893: SDL_EVENT_FINGER_CANCELED must release the touch exactly like FINGER_UP — it may
// not leave a permanently pressed/moved touch, and it must free the internal finger mapping.
TEST_F(SdlInputBridgeTouchGestureTest, FingerCanceledReleasesTouchLikeFingerUp)
{
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 8001, 0.5f, 0.5f));
    {
        const TouchCollection down = TouchPanel::GetState();
        ASSERT_EQ(down.getCountProperty(), 1);
        EXPECT_EQ(down[0].getStateProperty(), TouchLocationState::Pressed);
    }

    // Cancel instead of lifting the finger.
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, 8001, 0.5f, 0.5f));

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

// Task 892: after a cancel, SDL may reuse the same finger id for a brand-new press; the internal
// finger->touch mapping must have been freed so the new press is a fresh Pressed touch, not a
// continuation.
TEST_F(SdlInputBridgeTouchGestureTest, FingerIdReusableAfterCancel)
{
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 8100, 0.1f, 0.1f));
    (void)TouchPanel::GetState();
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, 8100, 0.1f, 0.1f));
    (void)TouchPanel::GetState(); // reports the Released touch (pure read)
    TouchPanel::Update();         // retires it (RemoveAfterSnapshot)

    // Same SDL finger id pressed again -> a fresh Pressed touch.
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 8100, 0.2f, 0.2f));
    const TouchCollection reuse = TouchPanel::GetState();
    ASSERT_EQ(reuse.getCountProperty(), 1);
    EXPECT_EQ(reuse[0].getStateProperty(), TouchLocationState::Pressed);
}

// INPUT-GESTURE-012: a real SDL_EVENT_FINGER_CANCELED arriving mid-drag must not wedge the gesture
// state machine — the detector must recover so a subsequent independent finger still produces a Tap.
// (At the detector a cancel is a Release, so the interrupted drag also reports DragComplete; this
// test is about clean recovery, so those interim gestures are drained.)
TEST_F(SdlInputBridgeTouchGestureTest, FingerCanceledMidDragRecoversAndAllowsAFreshTap)
{
    TouchPanel::setEnabledGesturesProperty(
        GestureType::Tap | GestureType::FreeDrag | GestureType::DragComplete);

    auto drainGestures = []
    {
        while (TouchPanel::getIsGestureAvailableProperty())
            (void)TouchPanel::ReadGesture();
    };

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 8300, 0.5f, 0.5f));
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_MOTION, 8300, 0.6f, 0.5f, 0.1f, 0.0f));
    drainGestures(); // discard the FreeDrag from the motion.

    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, 8300, 0.6f, 0.5f));
    drainGestures(); // discard whatever the cancel produced (DragComplete).

    // Fresh, independent finger -> a clean Tap, proving the state machine wasn't left dragging.
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 8301, 0.2f, 0.2f));
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 8301, 0.2f, 0.2f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(TouchPanel::ReadGesture().getGestureTypeProperty(), GestureType::Tap);
}

// N-006: SDL finger pressure flows end-to-end (ProcessEvent -> InputManager -> TouchPanel::GetState)
// and is exposed via TouchLocation::getPressureEXT; a later MOTION event updates it.
TEST_F(SdlInputBridgeTouchGestureTest, FingerPressureIsSurfacedThroughGetStateGetPressureEXT)
{
    SdlInputBridge::ProcessEvent(
        fingerEvent(SDL_EVENT_FINGER_DOWN, 7001, 0.5f, 0.5f, 0.0f, 0.0f, 0.75f));
    const TouchCollection down = TouchPanel::GetState();
    ASSERT_EQ(down.getCountProperty(), 1);
    EXPECT_FLOAT_EQ(down[0].getPressureEXT(), 0.75f);

    SdlInputBridge::ProcessEvent(
        fingerEvent(SDL_EVENT_FINGER_MOTION, 7001, 0.6f, 0.5f, 0.1f, 0.0f, 0.30f));
    const TouchCollection moved = TouchPanel::GetState();
    ASSERT_EQ(moved.getCountProperty(), 1);
    EXPECT_FLOAT_EQ(moved[0].getPressureEXT(), 0.30f);
}
