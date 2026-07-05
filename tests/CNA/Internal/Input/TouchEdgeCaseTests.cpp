// SPDX-License-Identifier: MS-PL
//
// Tasks 825-828: touch pipeline edge cases.
//   825 - TouchPanel::GetState() touches_ vs InputManager fallback (exclusive, no double-report)
//   826 - multi-touch edges: >MAX_TOUCHES, release-unknown, repeated-down, id-reuse-after-release
//   827 - TouchPanel::GetCapabilities() before/after touch, after reset, and via fallback
//   828 - INTERNAL_onTouchEvent coordinate scaling: zero / normal / resized / non-integer display

#include <gtest/gtest.h>

#include <optional>

#include "CNA/Internal/Input/GestureDetector.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

using CNA::Internal::Input::GestureDetector;
using CNA::Internal::Input::InputManager;
using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    class TouchEdgeCaseTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            InputManager::ResetForTests();
            TouchPanel::ResetForTests();
            GestureDetector::ResetForTests();
            GestureDetector::EnableTestClock(); // deterministic gesture timing
        }

        void TearDown() override
        {
            GestureDetector::DisableTestClock();
            GestureDetector::ResetForTests();
            TouchPanel::ResetForTests();
            InputManager::ResetForTests();
        }
    };
}

// --- Task 825: GetState() fallback and exclusivity ---

TEST_F(TouchEdgeCaseTest, GetStateFallsBackToInputManagerWhenTouchesArrayEmpty)
{
    // The real (event-driven) path leaves touches_ empty and populates InputManager instead.
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(10, 20));

    const TouchCollection state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 1);
    EXPECT_EQ(state[0].getIdProperty(), 1);
    EXPECT_EQ(state[0].getPositionProperty(), Vector2(10, 20));
}

TEST_F(TouchEdgeCaseTest, GetStatePrefersTouchesArrayAndDoesNotDoubleReport)
{
    // touches_ (SetFinger) and InputManager both hold a touch. GetState is EXCLUSIVE: it returns
    // touches_ and ignores the InputManager fallback — never the union — so no double-reporting.
    InputManager::SetTouchState(99, TouchLocationState::Pressed, Vector2(1, 1)); // shadowed
    TouchPanel::SetFinger(0, 5, Vector2(50, 60));                                // wins

    const TouchCollection state = TouchPanel::GetState();
    EXPECT_EQ(state.getCountProperty(), 1);   // exactly one, not two
    EXPECT_EQ(state[0].getIdProperty(), 5);   // the touches_ finger, not id 99
}

// --- Task 826: multi-touch edge cases ---

TEST_F(TouchEdgeCaseTest, ReleasingAnUnknownFingerIsSafe)
{
    InputManager::SetTouchState(42, TouchLocationState::Released, Vector2(5, 5));
    EXPECT_NO_THROW((void)InputManager::GetTouchState());
    EXPECT_NO_THROW(TouchPanel::INTERNAL_onTouchEvent(42, TouchLocationState::Released, 0.5f, 0.5f, 0, 0));
}

TEST_F(TouchEdgeCaseTest, RepeatedFingerDownWithSameIdOverwritesRatherThanDuplicates)
{
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(10, 10));
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(20, 20));

    const TouchCollection state = InputManager::GetTouchState();
    ASSERT_EQ(state.getCountProperty(), 1);
    EXPECT_EQ(state[0].getPositionProperty(), Vector2(20, 20));
}

TEST_F(TouchEdgeCaseTest, FingerIdReusedAfterReleaseStartsFresh)
{
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(10, 10));
    InputManager::SetTouchState(1, TouchLocationState::Released, Vector2(10, 10));
    (void)InputManager::GetTouchState(); // first read reports the Released touch...
    (void)InputManager::GetTouchState(); // ...second read flushes it (RemoveAfterSnapshot)

    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(30, 30));
    const TouchCollection state = InputManager::GetTouchState();
    ASSERT_EQ(state.getCountProperty(), 1);
    EXPECT_EQ(state[0].getPositionProperty(), Vector2(30, 30));
}

TEST_F(TouchEdgeCaseTest, MoreThanMaxTouchesAreCappedAtMaxTouchesByTouchPanelGetState)
{
    // DEC-10: FNA's TouchPanel tracks a fixed TouchLocation[MAX_TOUCHES] array, so its public state
    // never exceeds MAX_TOUCHES (=8) simultaneous touches. CNA's event-driven InputManager map is
    // unbounded (an implementation detail), but TouchPanel::GetState() caps the public snapshot at
    // MAX_TOUCHES to match FNA.
    for (int i = 0; i < 10; ++i)
        InputManager::SetTouchState(i, TouchLocationState::Pressed, Vector2(static_cast<float>(i), 0));

    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), TouchPanel::MAX_TOUCHES);
}

// --- Tasks 868-871: event-driven path preserves TouchLocation previous-location ---

TEST_F(TouchEdgeCaseTest, EventDrivenPathPreservesPreviousLocation)
{
    // Drives the REAL event-driven path (InputManager::SetTouchState + GetTouchState), one event
    // per snapshot (= one per frame), and checks TryGetPreviousLocation at each transition.
    const Vector2 a(10, 10), b(20, 20), c(30, 30);
    TouchLocation prev;

    // Pressed: no previous.
    InputManager::SetTouchState(1, TouchLocationState::Pressed, a);
    {
        const TouchCollection s = InputManager::GetTouchState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Pressed);
        EXPECT_FALSE(s[0].TryGetPreviousLocation(prev));
    }

    // Pressed -> Moved: previous is the Pressed location.
    InputManager::SetTouchState(1, TouchLocationState::Moved, b);
    {
        const TouchCollection s = InputManager::GetTouchState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
        EXPECT_EQ(s[0].getPositionProperty(), b);
        ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
        EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Pressed);
        EXPECT_EQ(prev.getPositionProperty(), a);
    }

    // Moved -> Moved: previous is the prior Moved location.
    InputManager::SetTouchState(1, TouchLocationState::Moved, c);
    {
        const TouchCollection s = InputManager::GetTouchState();
        ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
        EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Moved);
        EXPECT_EQ(prev.getPositionProperty(), b);
    }

    // Moved -> Released: previous is the prior Moved location; then removed after one snapshot.
    InputManager::SetTouchState(1, TouchLocationState::Released, c);
    {
        const TouchCollection s = InputManager::GetTouchState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Released);
        ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
        EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Moved);
        EXPECT_EQ(prev.getPositionProperty(), c);
    }
    EXPECT_EQ(InputManager::GetTouchState().getCountProperty(), 0); // Released removed
}

TEST_F(TouchEdgeCaseTest, HeldTouchAutoPromotesToMovedWithPressedPrevious)
{
    // A Pressed touch held across two snapshots (no new SDL event) auto-promotes to Moved on the
    // second snapshot, with the previous being the Pressed location the game actually saw.
    const Vector2 a(5, 5);
    TouchLocation prev;

    InputManager::SetTouchState(1, TouchLocationState::Pressed, a);
    (void)InputManager::GetTouchState(); // frame 1: Pressed (no previous), promotes to Moved

    const TouchCollection s = InputManager::GetTouchState(); // frame 2: Moved
    ASSERT_EQ(s.getCountProperty(), 1);
    EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
    ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
    EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(prev.getPositionProperty(), a);
}

// --- Task 827: GetCapabilities() ---

TEST_F(TouchEdgeCaseTest, GetCapabilitiesIsDisconnectedBeforeAnyTouch)
{
    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 0); // 0 when disconnected (task 790)
}

TEST_F(TouchEdgeCaseTest, GetCapabilitiesIsConnectedOnceTouchDeviceExists)
{
    TouchPanel::setTouchDeviceExistsProperty(true);
    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4); // DEC-09: XNA/FNA always report 4
}

TEST_F(TouchEdgeCaseTest, GetCapabilitiesIsConnectedViaInputManagerFallbackWhenFlagUnset)
{
    // touchDeviceExists_ stays false, but a live touch in InputManager reports connected.
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(5, 5));
    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4); // DEC-09: XNA/FNA always report 4
}

TEST_F(TouchEdgeCaseTest, GetCapabilitiesHasNoSideEffectOnTouchState)
{
    // Regression (task 894/896): the InputManager-fallback branch of GetCapabilities() must NOT
    // consume a frame of touch state. Previously it called GetTouchState(), which advances
    // previous-location tracking and promotes Pressed->Moved, so a GetCapabilities() call silently
    // corrupted the next GetState(). With the non-mutating HasAnyTouch() peek, the Pressed touch
    // must survive unchanged.
    InputManager::SetTouchState(7, TouchLocationState::Pressed, Vector2(5, 5));

    // Call it repeatedly — none of these may mutate touch state.
    (void) TouchPanel::GetCapabilities();
    (void) TouchPanel::GetCapabilities();

    const TouchCollection state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 1);
    EXPECT_EQ(state[0].getIdProperty(), 7);
    EXPECT_EQ(state[0].getStateProperty(), TouchLocationState::Pressed)
        << "GetCapabilities() must not promote Pressed to Moved";
    TouchLocation prev;
    EXPECT_FALSE(state[0].TryGetPreviousLocation(prev))
        << "the first GetState after a press has no previous location";
}

TEST_F(TouchEdgeCaseTest, GetCapabilitiesReturnsToDisconnectedAfterReset)
{
    TouchPanel::setTouchDeviceExistsProperty(true);
    ASSERT_TRUE(TouchPanel::GetCapabilities().getIsConnectedProperty());

    TouchPanel::ResetForTests();
    InputManager::ResetForTests();

    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 0);
}

// --- Task 828: coordinate scaling in INTERNAL_onTouchEvent ---

namespace
{
    // Drives a Tap (press+release at the same spot) and returns its pixel position, or nullopt if
    // no gesture was produced. Requires the test clock (held time 0 < 1s -> Tap).
    std::optional<Vector2> TapAt(float normX, float normY)
    {
        TouchPanel::setEnabledGesturesProperty(GestureType::Tap);
        TouchPanel::INTERNAL_onTouchEvent(1, TouchLocationState::Pressed, normX, normY, 0, 0);
        TouchPanel::INTERNAL_onTouchEvent(1, TouchLocationState::Released, normX, normY, 0, 0);
        if (!TouchPanel::getIsGestureAvailableProperty())
            return std::nullopt;
        return TouchPanel::ReadGesture().getPositionProperty();
    }
}

TEST_F(TouchEdgeCaseTest, ScalingProducesNoGestureWhenDisplaySizeIsZero)
{
    // Guard (task 828): before the display size is published, a touch must NOT collapse to a
    // bogus (0,0) corner gesture — it is dropped instead.
    TouchPanel::setDisplayWidthProperty(0);
    TouchPanel::setDisplayHeightProperty(0);
    EXPECT_FALSE(TapAt(0.5f, 0.5f).has_value());
}

TEST_F(TouchEdgeCaseTest, ScalingUsesDisplaySizeForPixelPosition)
{
    TouchPanel::setDisplayWidthProperty(1000);
    TouchPanel::setDisplayHeightProperty(1000);

    const auto pos = TapAt(0.5f, 0.5f);
    ASSERT_TRUE(pos.has_value());
    EXPECT_FLOAT_EQ(pos->X, 500.0f);
    EXPECT_FLOAT_EQ(pos->Y, 500.0f);
}

TEST_F(TouchEdgeCaseTest, ScalingReflectsResizedDisplay)
{
    TouchPanel::setDisplayWidthProperty(500);
    TouchPanel::setDisplayHeightProperty(400);

    const auto pos = TapAt(0.5f, 0.5f);
    ASSERT_TRUE(pos.has_value());
    EXPECT_FLOAT_EQ(pos->X, 250.0f);
    EXPECT_FLOAT_EQ(pos->Y, 200.0f);
}

TEST_F(TouchEdgeCaseTest, ScalingRoundsNonIntegerNormalizedCoordinates)
{
    TouchPanel::setDisplayWidthProperty(1000);
    TouchPanel::setDisplayHeightProperty(1000);

    // 0.6667 * 1000 = 666.7 -> round -> 667;  0.3333 * 1000 = 333.3 -> round -> 333.
    const auto pos = TapAt(0.6667f, 0.3333f);
    ASSERT_TRUE(pos.has_value());
    EXPECT_FLOAT_EQ(pos->X, 667.0f);
    EXPECT_FLOAT_EQ(pos->Y, 333.0f);
}
