// SPDX-License-Identifier: MS-PL
//
// Tasks 825-828: touch pipeline edge cases.
//   825 - TouchPanel::GetState() touches_ vs InputManager fallback (exclusive, no double-report)
//   826 - multi-touch edges: >MAX_TOUCHES, release-unknown, repeated-down, id-reuse-after-release
//   827 - TouchPanel::GetCapabilities() before/after touch, after reset, and via fallback
//   828 - INTERNAL_onTouchEvent coordinate scaling: zero / normal / resized / non-integer display

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "CNA/Input/InputDeviceInfo.hpp"
#include "CNA/Internal/Input/GestureDetector.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Platform/CannedInputDevices.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

using CNA::Input::InputDeviceInfoEXT;
using CNA::Internal::Input::GestureDetector;
using CNA::Internal::Input::InputManager;
using CNA::Platform::InputDeviceKind;
using CNA::Platform::Testing::CannedInputDevicePlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;
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

// DEC-13: TouchPanel::Update() copies touches_ -> previousTouches_ (reversed vs FNA's gesture-first
// order, but inert because GestureDetector::OnUpdate touches neither array). This pins the slot-path
// continuity the copy provides: SetFinger reads previousTouches_, so after Update() a moving finger sees
// its prior pressed location. If Update() failed to copy, the second SetFinger would see Invalid and
// produce a fresh Pressed instead of a Moved-with-previous.
TEST_F(TouchEdgeCaseTest, UpdatePropagatesTouchesToPreviousForSlotPathContinuity)
{
    // Frame 1: finger down at slot 0 (previousTouches_[0] is Invalid, so this is a Pressed).
    TouchPanel::SetFinger(0, 5, Vector2(10, 20));
    ASSERT_EQ(TouchPanel::GetState()[0].getStateProperty(), TouchLocationState::Pressed);

    // Update() copies touches_ -> previousTouches_.
    TouchPanel::Update();

    // Frame 2: same finger moves; SetFinger reads previousTouches_ (Pressed@(10,20)) to build a Moved.
    TouchPanel::SetFinger(0, 5, Vector2(30, 40));

    const TouchCollection s = TouchPanel::GetState();
    ASSERT_EQ(s.getCountProperty(), 1);
    EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
    TouchLocation prev;
    ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
    EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(prev.getPositionProperty(), Vector2(10, 20));
}

// P5-007: the fingerId == NO_FINGER release path (FNA TouchPanel.cs:167-195), "was there a finger here
// and the user just released it?". A held finger that lifts becomes a single Released touch carrying its
// previous location. Exercises the first branch of the release condition. (Regression guard for the
// historical duplicated nested condition — the current code must have exactly one.)
TEST_F(TouchEdgeCaseTest, SetFingerReleaseOfHeldFingerProducesReleasedWithPreviousLocation)
{
    TouchPanel::SetFinger(0, 7, Vector2(10, 20)); // Pressed
    TouchPanel::Update();
    TouchPanel::SetFinger(0, 7, Vector2(11, 21)); // Moved
    ASSERT_EQ(TouchPanel::GetState()[0].getStateProperty(), TouchLocationState::Moved);
    TouchPanel::Update();

    // Finger lifted: previous was Moved (not Invalid/Released) -> Released, position/previous preserved.
    TouchPanel::SetFinger(0, TouchPanel::NO_FINGER, Vector2::Zero);

    const TouchCollection s = TouchPanel::GetState();
    ASSERT_EQ(s.getCountProperty(), 1);
    EXPECT_EQ(s[0].getIdProperty(), 7);
    EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Released);
    EXPECT_EQ(s[0].getPositionProperty(), Vector2(11, 21));
    TouchLocation prev;
    ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
    EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Moved);
}

// P5-007: the else branch of the release condition — releasing a slot that had no prior finger
// (previous is Invalid) inserts Invalid data so the slot is excluded from GetState() (FNA:181-192).
TEST_F(TouchEdgeCaseTest, SetFingerReleaseWithNoPriorFingerInsertsInvalidAndReportsNothing)
{
    TouchPanel::SetFinger(0, TouchPanel::NO_FINGER, Vector2::Zero); // previous Invalid -> stays Invalid
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0)
        << "an empty slot released again must not create a ghost touch";
}

// --- Task 826: multi-touch edge cases ---

TEST_F(TouchEdgeCaseTest, ReleasingAnUnknownFingerIsSafe)
{
    InputManager::SetTouchState(42, TouchLocationState::Released, Vector2(5, 5));
    EXPECT_NO_THROW((void)InputManager::GetTouchState());
    EXPECT_NO_THROW(TouchPanel::INTERNAL_onTouchEvent(42, TouchLocationState::Released, 0.5f, 0.5f, 0, 0));
}

// P5-008: an unknown finger that is Released without a prior Pressed must not fabricate a bogus
// previous location. It surfaces at most as a single Released whose PreviousState stayed Invalid
// (TryGetPreviousLocation == false), then is flushed after one snapshot — never a Moved and never a
// Released carrying a garbage previous.
TEST_F(TouchEdgeCaseTest, UnknownReleasedFingerHasNoBogusPreviousAndClears)
{
    InputManager::SetTouchState(42, TouchLocationState::Released, Vector2(5, 5));

    TouchLocation prev;
    const TouchCollection first = InputManager::GetTouchState();
    ASSERT_EQ(first.getCountProperty(), 1);
    EXPECT_EQ(first[0].getStateProperty(), TouchLocationState::Released);
    EXPECT_EQ(first[0].getPositionProperty(), Vector2(5, 5));
    EXPECT_FALSE(first[0].TryGetPreviousLocation(prev)); // no bogus previous — previous is Invalid

    // A repeated read within the same frame is unaffected (pure read)...
    EXPECT_EQ(InputManager::GetTouchState().getCountProperty(), 1);

    // ...but is flushed once the frame is advanced; nothing lingers.
    InputManager::AdvanceTouchFrame();
    EXPECT_EQ(InputManager::GetTouchState().getCountProperty(), 0);
}

// P5-009: a repeated Pressed for the same finger id REPLACES (does not duplicate, ignore, or transition):
// in production `get_or_create_touch_id` keeps the same touch id until FINGER_UP, so a second FINGER_DOWN
// overwrites position while the state stays Pressed (no phantom second touch, no premature Moved). SDL
// never actually emits down-after-down for a live finger, so this is a defensive superset of FNA.
TEST_F(TouchEdgeCaseTest, RepeatedFingerDownWithSameIdOverwritesRatherThanDuplicates)
{
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(10, 10));
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(20, 20));

    TouchLocation prev;
    const TouchCollection state = InputManager::GetTouchState();
    ASSERT_EQ(state.getCountProperty(), 1);                                // replaced, not duplicated
    EXPECT_EQ(state[0].getStateProperty(), TouchLocationState::Pressed);   // still Pressed, not transitioned
    EXPECT_EQ(state[0].getPositionProperty(), Vector2(20, 20));            // last position wins
    EXPECT_FALSE(state[0].TryGetPreviousLocation(prev));                   // a fresh Pressed has no previous
}

TEST_F(TouchEdgeCaseTest, FingerIdReusedAfterReleaseStartsFresh)
{
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(10, 10));
    InputManager::SetTouchState(1, TouchLocationState::Released, Vector2(10, 10));
    (void)InputManager::GetTouchState(); // reports the Released touch (pure read)
    InputManager::AdvanceTouchFrame();   // retires it (RemoveAfterSnapshot)

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

    const TouchCollection capped = TouchPanel::GetState();
    ASSERT_EQ(capped.getCountProperty(), TouchPanel::MAX_TOUCHES);

    // P5-011: the truncation is DETERMINISTIC, not arbitrary. GetTouchState() sorts touch ids ascending
    // and GetState() keeps the first MAX_TOUCHES, so ids 0..MAX_TOUCHES-1 survive and the highest ids
    // (8, 9) are dropped. (Position.x == id in this test, so it doubles as an id check.)
    for (int i = 0; i < TouchPanel::MAX_TOUCHES; ++i)
    {
        EXPECT_EQ(capped[static_cast<std::size_t>(i)].getIdProperty(), i);
        EXPECT_EQ(capped[static_cast<std::size_t>(i)].getPositionProperty().X, static_cast<float>(i));
    }
}

// P5-011: the slot-path SetFinger writes touches_[index]; an out-of-range index must throw
// std::out_of_range rather than write past the fixed MAX_TOUCHES array (no OOB). The boundary indices
// 0 and MAX_TOUCHES-1 are the valid extremes and must NOT throw.
TEST_F(TouchEdgeCaseTest, SetFingerRejectsOutOfRangeSlotIndexWithoutOutOfBoundsWrite)
{
    EXPECT_THROW(TouchPanel::SetFinger(-1, 1, Vector2::Zero), std::out_of_range);
    EXPECT_THROW(TouchPanel::SetFinger(TouchPanel::MAX_TOUCHES, 1, Vector2::Zero), std::out_of_range);
    EXPECT_THROW(TouchPanel::SetFinger(TouchPanel::MAX_TOUCHES + 5, 1, Vector2::Zero), std::out_of_range);

    EXPECT_NO_THROW(TouchPanel::SetFinger(0, 1, Vector2::Zero));
    EXPECT_NO_THROW(TouchPanel::SetFinger(TouchPanel::MAX_TOUCHES - 1, 2, Vector2::Zero));
}

// --- Tasks 868-871: event-driven path preserves TouchLocation previous-location ---

TEST_F(TouchEdgeCaseTest, EventDrivenPathPreservesPreviousLocation)
{
    // Drives the REAL event-driven path (InputManager::SetTouchState + GetTouchState), reading
    // (pure) then explicitly advancing (InputManager::AdvanceTouchFrame) one frame at a time, and
    // checks TryGetPreviousLocation at each transition.
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
    InputManager::AdvanceTouchFrame();

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
    InputManager::AdvanceTouchFrame();

    // Moved -> Moved: previous is the prior Moved location.
    InputManager::SetTouchState(1, TouchLocationState::Moved, c);
    {
        const TouchCollection s = InputManager::GetTouchState();
        ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
        EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Moved);
        EXPECT_EQ(prev.getPositionProperty(), b);
    }
    InputManager::AdvanceTouchFrame();

    // Moved -> Released: previous is the prior Moved location; then removed after one advance.
    InputManager::SetTouchState(1, TouchLocationState::Released, c);
    {
        const TouchCollection s = InputManager::GetTouchState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Released);
        ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
        EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Moved);
        EXPECT_EQ(prev.getPositionProperty(), c);
    }
    InputManager::AdvanceTouchFrame();
    EXPECT_EQ(InputManager::GetTouchState().getCountProperty(), 0); // Released removed
}

TEST_F(TouchEdgeCaseTest, HeldTouchAutoPromotesToMovedWithPressedPrevious)
{
    // A Pressed touch held across two frames (no new SDL event) auto-promotes to Moved once the
    // frame is advanced, with the previous being the Pressed location the game actually saw.
    const Vector2 a(5, 5);
    TouchLocation prev;

    InputManager::SetTouchState(1, TouchLocationState::Pressed, a);
    {
        const TouchCollection frame1 = InputManager::GetTouchState(); // Pressed, no previous
        ASSERT_EQ(frame1.getCountProperty(), 1);
        EXPECT_EQ(frame1[0].getStateProperty(), TouchLocationState::Pressed);
    }
    InputManager::AdvanceTouchFrame(); // promotes to Moved; previous becomes the Pressed location

    const TouchCollection s = InputManager::GetTouchState(); // frame 2: Moved
    ASSERT_EQ(s.getCountProperty(), 1);
    EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
    ASSERT_TRUE(s[0].TryGetPreviousLocation(prev));
    EXPECT_EQ(prev.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(prev.getPositionProperty(), a);
}

// INP-AUD-001 regression: GetTouchState() must be a pure read. Two consecutive calls with no
// intervening AdvanceTouchFrame() must return identical snapshots, regardless of how many times
// (zero, one, or many) it is called within the frame.
TEST_F(TouchEdgeCaseTest, GetTouchStateIsPureAndRepeatedReadsWithinAFrameAreIdentical)
{
    const Vector2 a(1, 2);
    InputManager::SetTouchState(1, TouchLocationState::Pressed, a);

    const TouchCollection first = InputManager::GetTouchState();
    const TouchCollection second = InputManager::GetTouchState();
    const TouchCollection third = InputManager::GetTouchState();

    ASSERT_EQ(first.getCountProperty(), 1);
    ASSERT_EQ(second.getCountProperty(), 1);
    ASSERT_EQ(third.getCountProperty(), 1);
    EXPECT_EQ(first[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(second[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(third[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(first[0].getPositionProperty(), a);
    EXPECT_EQ(second[0].getPositionProperty(), a);
    EXPECT_EQ(third[0].getPositionProperty(), a);
}

// INP-AUD-001 regression: a frame with zero GetTouchState() reads still advances correctly once
// AdvanceTouchFrame() runs — a held Pressed touch is Moved on the very next read even if nothing
// read it during the frame it was pressed in.
TEST_F(TouchEdgeCaseTest, AdvanceTouchFrameWorksEvenWithoutAnIntermediateRead)
{
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(3, 4));

    InputManager::AdvanceTouchFrame(); // no GetTouchState() call happened this frame

    const TouchCollection s = InputManager::GetTouchState();
    ASSERT_EQ(s.getCountProperty(), 1);
    EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
}

// INP-AUD-001 regression: a Released touch remains visible for exactly one post-advance read,
// no matter how many times it was read *before* that advance.
TEST_F(TouchEdgeCaseTest, ReleasedTouchIsVisibleForExactlyOnePostAdvanceReadRegardlessOfPriorReads)
{
    InputManager::SetTouchState(1, TouchLocationState::Released, Vector2(7, 8));

    // Read it many times before advancing — must not consume/flush it early.
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(InputManager::GetTouchState().getCountProperty(), 1);
    }

    InputManager::AdvanceTouchFrame();
    EXPECT_EQ(InputManager::GetTouchState().getCountProperty(), 0);
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

// --- INP-AUD-003: GetCapabilities() consults SDL touch-device enumeration ---

namespace
{
    // The enumeration is scripted so GetCapabilities()'s enumeration path is exercised
    // deterministically instead of depending on whether the CI machine happens to have a
    // touchscreen.
    class TouchCapabilitiesEnumerationTest : public TouchEdgeCaseTest
    {
    protected:
        CannedInputDevicePlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            TouchEdgeCaseTest::SetUp();
            installed = std::make_unique<ScopedCurrentPlatform>(platform);
        }

        void TearDown() override
        {
            installed.reset();
            TouchEdgeCaseTest::TearDown();
        }
    };
}

TEST_F(TouchCapabilitiesEnumerationTest, ReportsConnectedFromEnumerationBeforeAnyTouchIsObserved)
{
    // A touchscreen SDL already enumerates, but nobody has touched it yet: touchDeviceExists_ is
    // still false and there is no live touch in InputManager. Before the fix this reported
    // disconnected; GetCapabilities() must now trust the enumeration.
    platform.Canned().Set(InputDeviceKind::Touch, {{1, InputDeviceKind::Touch, "Fake Touchscreen"}});

    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4); // DEC-09: XNA/FNA always report 4
}

TEST_F(TouchCapabilitiesEnumerationTest, ReportsDisconnectedWhenEnumerationEmptyAndNoTouchObserved)
{
    // No enumerable device, no sticky flag, no live touch: genuinely disconnected.
    platform.Canned().Clear();

    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 0);
}

TEST_F(TouchCapabilitiesEnumerationTest, FallsBackToStickyFlagWhenEnumerationLagsInteractionWindowsStyle)
{
    // FNA notes Windows only enumerates a touch device after it has actually been touched. Model
    // that here: enumeration stays empty, but touchDeviceExists_ was set by an earlier FINGER_DOWN.
    platform.Canned().Clear();
    TouchPanel::setTouchDeviceExistsProperty(true);

    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4);
}

TEST_F(TouchCapabilitiesEnumerationTest, FallsBackToLiveTouchWhenEnumerationEmptyAndFlagUnset)
{
    // Same late-enumeration scenario, but observed only via a live touch in InputManager rather
    // than the sticky TouchPanel flag (e.g. a test/backend that calls InputManager directly).
    platform.Canned().Clear();
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(5, 5));

    const TouchPanelCapabilities caps = TouchPanel::GetCapabilities();
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4);
}

TEST_F(TouchCapabilitiesEnumerationTest, EnumerationQueryDoesNotMutateTouchState)
{
    // GetCapabilities() must remain non-mutating even with the new enumeration check added.
    platform.Canned().Set(InputDeviceKind::Touch, {{1, InputDeviceKind::Touch, "Fake Touchscreen"}});
    InputManager::SetTouchState(7, TouchLocationState::Pressed, Vector2(5, 5));

    (void) TouchPanel::GetCapabilities();
    (void) TouchPanel::GetCapabilities();

    const TouchCollection state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 1);
    EXPECT_EQ(state[0].getStateProperty(), TouchLocationState::Pressed)
        << "GetCapabilities() must not promote Pressed to Moved";
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
