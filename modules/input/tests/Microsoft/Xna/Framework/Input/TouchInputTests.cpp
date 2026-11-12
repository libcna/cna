// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include <type_traits>
#include <utility>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ArgumentException.hpp"
#include "CNA/Internal/Input/InputManager.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    void ResetTouchState()
    {
        TouchPanel::ResetForTests();
    }
}

TEST(TouchInputTest, GetStateReflectsCurrentTouchSnapshot)
{
    ResetTouchState();

    TouchPanel::INTERNAL_setTouchState(11, TouchLocationState::Pressed, Vector2(100.5f, 200.25f));

    const auto state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 1);

    const auto& touchLocation = state[0];
    EXPECT_EQ(touchLocation.getIdProperty(), 11);
    EXPECT_EQ(touchLocation.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_FLOAT_EQ(touchLocation.getPositionProperty().X, 100.5f);
    EXPECT_FLOAT_EQ(touchLocation.getPositionProperty().Y, 200.25f);

    // GetState() is a pure read (INP-AUD-001): a repeated call without an intervening frame
    // advance must return the identical snapshot, not silently promote Pressed -> Moved.
    const auto stillPressed = TouchPanel::GetState();
    ASSERT_EQ(stillPressed.getCountProperty(), 1);
    EXPECT_EQ(stillPressed[0].getStateProperty(), TouchLocationState::Pressed);

    TouchPanel::Update(); // advances exactly one frame: Pressed -> Moved

    const auto nextState = TouchPanel::GetState();
    ASSERT_EQ(nextState.getCountProperty(), 1);
    EXPECT_EQ(nextState[0].getIdProperty(), 11);
    EXPECT_EQ(nextState[0].getStateProperty(), TouchLocationState::Moved);

    ResetTouchState();
}

TEST(TouchInputTest, ReleasedTouchIsReturnedOnceAndThenRemoved)
{
    ResetTouchState();

    TouchPanel::INTERNAL_setTouchState(21, TouchLocationState::Pressed, Vector2(30.0f, 40.0f));
    (void)TouchPanel::GetState();

    TouchPanel::INTERNAL_setTouchState(21, TouchLocationState::Released, Vector2(35.0f, 45.0f));

    const auto releasedState = TouchPanel::GetState();
    ASSERT_EQ(releasedState.getCountProperty(), 1);
    EXPECT_EQ(releasedState[0].getIdProperty(), 21);
    EXPECT_EQ(releasedState[0].getStateProperty(), TouchLocationState::Released);

    // A repeated read within the same frame must still report the Released touch (pure read).
    const auto releasedStateAgain = TouchPanel::GetState();
    ASSERT_EQ(releasedStateAgain.getCountProperty(), 1);
    EXPECT_EQ(releasedStateAgain[0].getStateProperty(), TouchLocationState::Released);

    TouchPanel::Update(); // advances one frame: Released touches are retired

    const auto afterReleasedState = TouchPanel::GetState();
    EXPECT_EQ(afterReleasedState.getCountProperty(), 0);

    ResetTouchState();
}

TEST(TouchInputTest, GetStateHandlesMultipleTouchIdsAndKeepsDeterministicOrder)
{
    ResetTouchState();

    TouchPanel::INTERNAL_setTouchState(42, TouchLocationState::Moved, Vector2(400.0f, 500.0f));
    TouchPanel::INTERNAL_setTouchState(7, TouchLocationState::Pressed, Vector2(70.0f, 80.0f));

    const auto state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 2);

    EXPECT_EQ(state[0].getIdProperty(), 7);
    EXPECT_EQ(state[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(state[1].getIdProperty(), 42);
    EXPECT_EQ(state[1].getStateProperty(), TouchLocationState::Moved);

    ResetTouchState();
}

// P5-012 / DEC-20: the event-driven fallback orders the collection by ASCENDING touch id, not by
// insertion order. Insert three ids out of order (30, 5, 17) and assert they come back 5, 17, 30 —
// proving the order is a deterministic id sort, not an accident of insertion sequence.
TEST(TouchInputTest, GetStateOrdersMultipleTouchesByAscendingIdRegardlessOfInsertionOrder)
{
    ResetTouchState();

    TouchPanel::INTERNAL_setTouchState(30, TouchLocationState::Pressed, Vector2(30.0f, 0.0f));
    TouchPanel::INTERNAL_setTouchState(5, TouchLocationState::Pressed, Vector2(5.0f, 0.0f));
    TouchPanel::INTERNAL_setTouchState(17, TouchLocationState::Pressed, Vector2(17.0f, 0.0f));

    const auto state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 3);
    EXPECT_EQ(state[0].getIdProperty(), 5);
    EXPECT_EQ(state[1].getIdProperty(), 17);
    EXPECT_EQ(state[2].getIdProperty(), 30);

    ResetTouchState();
}

// RV-INPUT review #4: XNA's gesture reads refuse until EnabledGestures has been assigned
// (get_IsGestureAvailable / ReadGesture IL_0000-0011, FrameworkResources.GesturesNotEnabled); any
// accepted assignment counts, None included (set_EnabledGestures IL_001a-001b). An empty queue then
// names GesturesNotAvailable (ReadGesture IL_0012-001c).
TEST(TouchInputTest, GestureReadsRefuseUntilEnabledGesturesIsAssigned)
{
    CNA::Internal::Input::InputManager::ResetAllForTests();
    const std::string notEnabled =
        "This operation cannot be completed until TouchPanel.EnabledGestures is assigned.";
    try { (void)TouchPanel::getIsGestureAvailableProperty(); ADD_FAILURE() << "IsGestureAvailable accepted"; }
    catch (const System::InvalidOperationException& e) { EXPECT_EQ(std::string(e.what()), notEnabled); }
    try { (void)TouchPanel::ReadGesture(); ADD_FAILURE() << "ReadGesture accepted"; }
    catch (const System::InvalidOperationException& e) { EXPECT_EQ(std::string(e.what()), notEnabled); }

    TouchPanel::setEnabledGesturesProperty(GestureType::None);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
    try { (void)TouchPanel::ReadGesture(); ADD_FAILURE() << "ReadGesture of an empty queue accepted"; }
    catch (const System::InvalidOperationException& e)
    {
        EXPECT_EQ(std::string(e.what()),
                  "No gestures are available at this time.  TouchPanel.ReadGesture should only be "
                  "called when TouchPanel.IsGestureAvailable is true.");
    }
    CNA::Internal::Input::InputManager::ResetAllForTests();
}

// RV-INPUT review #7: Helpers.ValidateOrientation IL_000f-0019 throws ArgumentException with
// FrameworkResources.InvalidDisplayOrientation, keeping the previous orientation.
TEST(TouchInputTest, AnInvalidOrientationNamesXnasMessage)
{
    const DisplayOrientation previous = TouchPanel::getDisplayOrientationProperty();
    try
    {
        TouchPanel::setDisplayOrientationProperty(static_cast<DisplayOrientation>(3));
        ADD_FAILURE() << "orientation 3 accepted";
    }
    catch (const System::ArgumentException& e)
    {
        EXPECT_EQ(e.getMessageProperty(), "The specified DisplayOrientation is invalid.");
    }
    EXPECT_EQ(TouchPanel::getDisplayOrientationProperty(), previous);
}

TEST(TouchInputTest, EnqueueGestureAndReadGestureFollowFifoOrder)
{
    // XNA refuses gesture reads until EnabledGestures has been assigned; an unchanged value counts.
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    while (TouchPanel::getIsGestureAvailableProperty())
    {
        (void)TouchPanel::ReadGesture();
    }

    const GestureSample first(GestureType::Tap, System::TimeSpan::FromMilliseconds(1.0),
                               Vector2(1.0f, 2.0f), Vector2::Zero, Vector2::Zero, Vector2::Zero);
    const GestureSample second(GestureType::Hold, System::TimeSpan::FromMilliseconds(2.0),
                                Vector2(3.0f, 4.0f), Vector2::Zero, Vector2::Zero, Vector2::Zero);

    TouchPanel::EnqueueGesture(first);
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    TouchPanel::EnqueueGesture(second);

    const GestureSample readFirst = TouchPanel::ReadGesture();
    EXPECT_EQ(readFirst.getGestureTypeProperty(), GestureType::Tap);

    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample readSecond = TouchPanel::ReadGesture();
    EXPECT_EQ(readSecond.getGestureTypeProperty(), GestureType::Hold);

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
}

// P6-003: IsGestureAvailable exactly mirrors "queue is non-empty" — false when empty, true once a gesture
// is enqueued, and false again after the last one is read (FNA: `IsGestureAvailable => gestures.Count > 0`).
TEST(TouchInputTest, IsGestureAvailableReflectsQueueState)
{
    // XNA refuses gesture reads until EnabledGestures has been assigned; an unchanged value counts.
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    while (TouchPanel::getIsGestureAvailableProperty())
    {
        (void)TouchPanel::ReadGesture();
    }
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());   // empty queue

    TouchPanel::EnqueueGesture(GestureSample(GestureType::Tap, System::TimeSpan::Zero,
                                             Vector2::Zero, Vector2::Zero, Vector2::Zero, Vector2::Zero));
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty());    // has an entry

    (void)TouchPanel::ReadGesture();
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());   // drained again
}

TEST(TouchInputTest, ReadGestureThrowsInvalidOperationExceptionWhenQueueIsEmpty)
{
    // XNA refuses gesture reads until EnabledGestures has been assigned; an unchanged value counts.
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    while (TouchPanel::getIsGestureAvailableProperty())
    {
        (void)TouchPanel::ReadGesture();
    }

    EXPECT_THROW((void)TouchPanel::ReadGesture(), System::InvalidOperationException);
}

TEST(TouchInputTest, GetCapabilitiesReportsConnectedWhenTouchDeviceExistsFlagIsSet)
{
    TouchPanel::setTouchDeviceExistsProperty(true);

    const auto caps = TouchPanel::GetCapabilities();
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4); // DEC-09: XNA/FNA always report 4

    TouchPanel::setTouchDeviceExistsProperty(false);
}

TEST(TouchInputTest, GetCapabilitiesFallsBackToPanelTouchStateWhenFlagIsUnset)
{
    TouchPanel::setTouchDeviceExistsProperty(false);
    ResetTouchState();

    const auto disconnected = TouchPanel::GetCapabilities();
    EXPECT_FALSE(disconnected.getIsConnectedProperty());
    EXPECT_EQ(disconnected.getMaximumTouchCountProperty(), 0); // matches FNA: 0 when disconnected

    TouchPanel::INTERNAL_setTouchState(99, TouchLocationState::Pressed, Vector2(1.0f, 1.0f));
    const auto connected = TouchPanel::GetCapabilities();
    EXPECT_TRUE(connected.getIsConnectedProperty());
    EXPECT_EQ(connected.getMaximumTouchCountProperty(), 4); // DEC-09: XNA/FNA always report 4

    ResetTouchState();
}

// P6-002: default EnabledGestures is None — matches FNA's plain static `GestureType EnabledGestures`
// auto-property (no initializer / static ctor → default(GestureType) == 0). Gestures are opt-in.
TEST(TouchInputTest, DefaultEnabledGesturesIsNone)
{
    TouchPanel::ResetForTests();
    EXPECT_EQ(TouchPanel::getEnabledGesturesProperty(), GestureType::None);
}

// P6-002: changing EnabledGestures must NOT clear already-queued gestures. FNA's setter only mutates the
// flag; the queue is drained solely by ReadGesture / reset. So disabling everything (or switching sets)
// leaves pending gestures readable.
TEST(TouchInputTest, ChangingEnabledGesturesDoesNotClearTheQueue)
{
    const GestureType previous = TouchPanel::getEnabledGesturesProperty();
    // XNA refuses gesture reads until EnabledGestures has been assigned; an unchanged value counts.
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    while (TouchPanel::getIsGestureAvailableProperty())
    {
        (void)TouchPanel::ReadGesture();
    }

    TouchPanel::EnqueueGesture(GestureSample(GestureType::Tap, System::TimeSpan::Zero,
                                             Vector2::Zero, Vector2::Zero, Vector2::Zero, Vector2::Zero));
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());

    TouchPanel::setEnabledGesturesProperty(GestureType::None);   // "disable" everything
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty())
        << "changing EnabledGestures must not drop already-queued gestures";
    TouchPanel::setEnabledGesturesProperty(GestureType::Pinch);  // switch to a different set
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty());

    (void)TouchPanel::ReadGesture();
    TouchPanel::setEnabledGesturesProperty(previous);
}

TEST(TouchInputTest, EnabledGesturesGetterAndSetterRoundTrip)
{
    const GestureType previous = TouchPanel::getEnabledGesturesProperty();

    TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::Hold);
    EXPECT_EQ(TouchPanel::getEnabledGesturesProperty(), GestureType::Tap | GestureType::Hold);

    TouchPanel::setEnabledGesturesProperty(GestureType::None);
    EXPECT_EQ(TouchPanel::getEnabledGesturesProperty(), GestureType::None);

    TouchPanel::setEnabledGesturesProperty(previous);
}

TEST(TouchInputTest, DisplayWidthHeightAndOrientationGetterAndSetterRoundTrip)
{
    const int previousWidth = TouchPanel::getDisplayWidthProperty();
    const int previousHeight = TouchPanel::getDisplayHeightProperty();
    const DisplayOrientation previousOrientation = TouchPanel::getDisplayOrientationProperty();

    TouchPanel::setDisplayWidthProperty(1920);
    TouchPanel::setDisplayHeightProperty(1080);
    TouchPanel::setDisplayOrientationProperty(DisplayOrientation::LandscapeRight);

    EXPECT_EQ(TouchPanel::getDisplayWidthProperty(), 1920);
    EXPECT_EQ(TouchPanel::getDisplayHeightProperty(), 1080);
    EXPECT_EQ(TouchPanel::getDisplayOrientationProperty(), DisplayOrientation::LandscapeRight);

    TouchPanel::setDisplayWidthProperty(previousWidth);
    TouchPanel::setDisplayHeightProperty(previousHeight);
    TouchPanel::setDisplayOrientationProperty(previousOrientation);
}

// A5-005: TouchPanel::WindowHandle get/set functional round-trip. Previously only the signatures were pinned
// (PublicApiInputSignatureFreezeTests) — nothing exercised the actual stored value. Mirrors FNA's
// `TouchPanel.WindowHandle` (an opaque IntPtr the platform stores).
TEST(TouchInputTest, WindowHandleGetterAndSetterRoundTrip)
{
    const std::uintptr_t previous = TouchPanel::getWindowHandleProperty();

    TouchPanel::setWindowHandleProperty(std::uintptr_t{0xABCDEF01u});
    EXPECT_EQ(TouchPanel::getWindowHandleProperty(), std::uintptr_t{0xABCDEF01u});

    TouchPanel::setWindowHandleProperty(0);
    EXPECT_EQ(TouchPanel::getWindowHandleProperty(), std::uintptr_t{0});

    TouchPanel::setWindowHandleProperty(previous);
}

// --- TouchCollection ---

TEST(TouchCollectionTest, CountAndEmptyReflectContents)
{
    const TouchCollection empty;
    EXPECT_EQ(empty.getCountProperty(), 0);
    EXPECT_TRUE(empty.empty());

    const std::vector<TouchLocation> locations{
        TouchLocation(1, TouchLocationState::Pressed, Vector2(10.0f, 20.0f)),
        TouchLocation(2, TouchLocationState::Moved, Vector2(30.0f, 40.0f))
    };
    const TouchCollection collection(locations);
    EXPECT_EQ(collection.getCountProperty(), 2);
    EXPECT_FALSE(collection.empty());
}

TEST(TouchCollectionTest, IsReadOnlyIsAlwaysTrue)
{
    const TouchCollection collection;
    EXPECT_TRUE(collection.getIsReadOnlyProperty());
}

// XNA TouchCollection mutators and set_Item IL_0000-0005: always NotSupportedException.
TEST(TouchCollectionTest, Tq036DefaultCollectionRejectsEveryMutation)
{
    TouchCollection collection;
    const TouchLocation item(1, TouchLocationState::Pressed, Vector2(2, 3));
    EXPECT_TRUE(collection.getIsReadOnlyProperty());
    EXPECT_THROW(collection.Add(item), System::NotSupportedException);
    EXPECT_THROW(collection.Clear(), System::NotSupportedException);
    EXPECT_THROW((void)collection.Remove(item), System::NotSupportedException);
    EXPECT_THROW(collection.RemoveAt(0), System::NotSupportedException);
    EXPECT_THROW(collection.Insert(0, item), System::NotSupportedException);
    EXPECT_THROW(collection.setItemProperty(0, item), System::NotSupportedException);
    EXPECT_TRUE(collection.empty());
    EXPECT_FALSE(collection.getIsConnectedProperty());
}

// XNA TouchCollection.get_IsConnected IL_0000-0006; array ctor IL_001f-0021 sets true.
TEST(TouchCollectionTest, Tq036ConnectionBelongsToConstructedValue)
{
    ResetTouchState();
    TouchPanel::setTouchDeviceExistsProperty(true);
    const TouchCollection defaultValue;
    EXPECT_FALSE(defaultValue.getIsConnectedProperty());
    const std::vector<TouchLocation> empty;
    const TouchCollection copiedEmpty(empty);
    const TouchCollection movedEmpty(std::vector<TouchLocation>{});
    const std::vector<TouchLocation> values{TouchLocation(7, TouchLocationState::Pressed, Vector2(2, 3))};
    const TouchCollection copied(values);
    const TouchCollection moved(std::vector<TouchLocation>{values[0]});
    TouchPanel::setTouchDeviceExistsProperty(false);
    EXPECT_TRUE(copiedEmpty.getIsConnectedProperty());
    EXPECT_TRUE(movedEmpty.getIsConnectedProperty());
    EXPECT_TRUE(copied.getIsConnectedProperty());
    EXPECT_TRUE(moved.getIsConnectedProperty());
    EXPECT_TRUE(copiedEmpty.empty());
    EXPECT_TRUE(movedEmpty.empty());
    EXPECT_EQ(copied[0], values[0]);
    EXPECT_EQ(moved[0], values[0]);
    const TouchCollection copy = copied;
    TouchCollection assigned;
    assigned = copied;
    EXPECT_TRUE(copy.getIsConnectedProperty());
    EXPECT_TRUE(assigned.getIsConnectedProperty());
    ResetTouchState();
}

// XNA TouchCollection.Update stores connection; TouchPanel.GetState IL_0050-0055 returns a value.
TEST(TouchCollectionTest, Tq036PanelSnapshotsCaptureConnectionOnEveryReturnPath)
{
    ResetTouchState();
    const auto disconnected = TouchPanel::GetState();
    TouchPanel::setTouchDeviceExistsProperty(true);
    const auto connectedEmpty = TouchPanel::GetState();
    TouchPanel::SetFinger(0, 42, Vector2(11, 12));
    const auto explicitSlots = TouchPanel::GetState();
    TouchPanel::setInputSuppressedEXT(true);
    const auto suppressed = TouchPanel::GetState();
    TouchPanel::setInputSuppressedEXT(false); // ResetForTests does not own system-input suppression.
    ResetTouchState();
    TouchPanel::INTERNAL_setTouchState(9, TouchLocationState::Pressed, Vector2(20, 30));
    TouchPanel::setTouchDeviceExistsProperty(true);
    const auto eventState = TouchPanel::GetState();
    TouchPanel::setTouchDeviceExistsProperty(false);
    const auto latest = TouchPanel::GetState();
    ResetTouchState();
    EXPECT_FALSE(disconnected.getIsConnectedProperty());
    EXPECT_TRUE(connectedEmpty.getIsConnectedProperty());
    EXPECT_TRUE(connectedEmpty.empty());
    EXPECT_TRUE(explicitSlots.getIsConnectedProperty());
    ASSERT_EQ(explicitSlots.getCountProperty(), 1);
    EXPECT_EQ(explicitSlots[0].getIdProperty(), 42);
    EXPECT_TRUE(suppressed.getIsConnectedProperty());
    EXPECT_TRUE(suppressed.empty());
    EXPECT_TRUE(eventState.getIsConnectedProperty());
    ASSERT_EQ(eventState.getCountProperty(), 1);
    EXPECT_EQ(eventState[0].getIdProperty(), 9);
    EXPECT_FALSE(latest.getIsConnectedProperty());
}

// XNA set_Item IL_0000-0005 throws; reading an in-range item still returns the original value.
TEST(TouchCollectionTest, Tq036IndexerReadsOriginalAndSetterAlwaysThrows)
{
    const TouchLocation original(5, TouchLocationState::Pressed, Vector2(1, 2));
    const TouchLocation replacement(6, TouchLocationState::Moved, Vector2(3, 4));
    TouchCollection collection(std::vector<TouchLocation>{original});
    const TouchCollection& constRef = collection;
    static_assert(std::is_same_v<decltype(collection[0]), const TouchLocation&>);
    static_assert(!std::is_assignable_v<decltype(collection[0]), TouchLocation>);
    EXPECT_EQ(collection[0], original);
    EXPECT_THROW(collection.setItemProperty(0, replacement), System::NotSupportedException);
    EXPECT_THROW(collection.setItemProperty(100, replacement), System::NotSupportedException);
    EXPECT_EQ(constRef[0], original);
}

TEST(TouchCollectionTest, ContainsFindsMatchingLocation)
{
    const TouchLocation present(1, TouchLocationState::Pressed, Vector2(1.0f, 1.0f));
    const TouchLocation absent(2, TouchLocationState::Pressed, Vector2(2.0f, 2.0f));
    const TouchCollection collection(std::vector<TouchLocation>{present});

    EXPECT_TRUE(collection.Contains(present));
    EXPECT_FALSE(collection.Contains(absent));
}

TEST(TouchCollectionTest, FindByIdReturnsMatchAndFalseWhenMissing)
{
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(7, TouchLocationState::Pressed, Vector2(9.0f, 9.0f))
    });

    TouchLocation found;
    EXPECT_TRUE(collection.FindById(7, found));
    EXPECT_EQ(found.getIdProperty(), 7);

    TouchLocation notFound;
    EXPECT_FALSE(collection.FindById(999, notFound));
}

// XNA TouchCollection.FindById(int,out) IL_0032-003a assigns default(TouchLocation) on a miss:
// ID 0, Invalid, Zero. Unlike FNA's -1 sentinel, this differs from TryGetPreviousLocation's sentinel.
TEST(TouchCollectionTest, FindByIdWritesInvalidSentinelOnOutParamWhenMissing)
{
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(7, TouchLocationState::Pressed, Vector2(9.0f, 9.0f))
    });

    TouchLocation notFound(42, TouchLocationState::Moved, Vector2(1.0f, 2.0f));
    EXPECT_FALSE(collection.FindById(999, notFound));
    EXPECT_EQ(notFound.getIdProperty(), 0); // XNA FindById IL_0032-003a: initobj
    EXPECT_EQ(notFound.getStateProperty(), TouchLocationState::Invalid);
    EXPECT_EQ(notFound.getPositionProperty(), Vector2::Zero);
}

// Same not-found out-param contract when the collection itself is default-constructed/empty
// (CNA's stand-in for FNA's null-backed default TouchCollection, TouchCollection.cs:114 `touches != null`).
TEST(TouchCollectionTest, FindByIdWritesInvalidSentinelOnOutParamForEmptyCollection)
{
    const TouchCollection empty;

    TouchLocation notFound(3, TouchLocationState::Released, Vector2(5.0f, 6.0f));
    EXPECT_FALSE(empty.FindById(1, notFound));
    EXPECT_EQ(notFound.getIdProperty(), 0); // XNA FindById IL_0032-003a: initobj
    EXPECT_EQ(notFound.getStateProperty(), TouchLocationState::Invalid);
    EXPECT_EQ(notFound.getPositionProperty(), Vector2::Zero);
}

TEST(TouchCollectionTest, CopyToAppendsAllElementsInOrder)
{
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero),
        TouchLocation(2, TouchLocationState::Pressed, Vector2::Zero)
    });

    std::vector<TouchLocation> destination;
    collection.CopyTo(destination, 0);

    ASSERT_EQ(destination.size(), 2u);
    EXPECT_EQ(destination[0].getIdProperty(), 1);
    EXPECT_EQ(destination[1].getIdProperty(), 2);
}

// P5-002: an empty collection copies nothing and leaves the destination untouched (FNA's CopyTo is a
// null/empty no-op). A valid index into a non-empty destination stays valid because nothing is inserted.
TEST(TouchCollectionTest, CopyToFromEmptyCollectionIsANoOp)
{
    const TouchCollection empty;
    std::vector<TouchLocation> destination{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero),
        TouchLocation(2, TouchLocationState::Pressed, Vector2::Zero)
    };

    EXPECT_NO_THROW(empty.CopyTo(destination, 0));
    EXPECT_NO_THROW(empty.CopyTo(destination, 2)); // index == size() is valid even with nothing to copy
    ASSERT_EQ(destination.size(), 2u) << "copying an empty collection must not change the destination";
    EXPECT_EQ(destination[0].getIdProperty(), 1);
    EXPECT_EQ(destination[1].getIdProperty(), 2);
}

TEST(TouchCollectionTest, CopyToThrowsOnOutOfRangeIndexInsteadOfUndefinedBehavior)
{
    // Task 902: a negative or past-the-end arrayIndex previously formed an invalid iterator and
    // hit std::vector::insert UB. It must now throw System::ArgumentOutOfRangeException.
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero)
    });

    std::vector<TouchLocation> destination(2); // size 2 -> valid indices are 0,1,2

    EXPECT_THROW(collection.CopyTo(destination, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(collection.CopyTo(destination, 3), System::ArgumentOutOfRangeException);   // 3 > size()
    EXPECT_NO_THROW(collection.CopyTo(destination, 2));                   // 2 == size() is valid (append)
}

TEST(TouchCollectionTest, CopyToInsertsAtValidNonZeroIndex)
{
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(9, TouchLocationState::Pressed, Vector2::Zero)
    });
    std::vector<TouchLocation> destination{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero),
        TouchLocation(2, TouchLocationState::Pressed, Vector2::Zero)
    };

    collection.CopyTo(destination, 1);

    ASSERT_EQ(destination.size(), 3u);
    EXPECT_EQ(destination[0].getIdProperty(), 1);
    EXPECT_EQ(destination[1].getIdProperty(), 9);
    EXPECT_EQ(destination[2].getIdProperty(), 2);
}

TEST(TouchCollectionTest, IndexerThrowsOnOutOfRangeAccess)
{
    // Task 904: out-of-range indexer access must throw System::ArgumentOutOfRangeException, not read OOB.
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero)
    });
    EXPECT_NO_THROW((void)collection[0]);
    EXPECT_THROW((void)collection[1], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)collection[std::size_t{100}], System::ArgumentOutOfRangeException);

    const TouchCollection empty;
    EXPECT_EQ(empty.getCountProperty(), 0);
    EXPECT_TRUE(empty.getIsReadOnlyProperty()); // XNA/FNA: TouchCollection.IsReadOnly is always true
    EXPECT_THROW((void)empty[0], System::ArgumentOutOfRangeException);
}

TEST(TouchCollectionTest, IndexOfReturnsPositionOrNegativeOne)
{
    const TouchLocation first(1, TouchLocationState::Pressed, Vector2::Zero);
    const TouchLocation second(2, TouchLocationState::Pressed, Vector2::Zero);
    const TouchLocation missing(3, TouchLocationState::Pressed, Vector2::Zero);
    const TouchCollection collection(std::vector<TouchLocation>{first, second});

    EXPECT_EQ(collection.IndexOf(first), 0);
    EXPECT_EQ(collection.IndexOf(second), 1);
    EXPECT_EQ(collection.IndexOf(missing), -1);
}

// P5-003: FNA's TouchCollection.Enumerator yields collection[position] for position 0..Count-1
// (TouchCollection.cs:190-206), i.e. index/insertion order. A range-based for over begin()/end()
// must reproduce that deterministic order for both the const and mutable overloads.
TEST(TouchCollectionTest, RangeIterationYieldsElementsInInsertionOrder)
{
    const TouchLocation first(1, TouchLocationState::Pressed, Vector2(10.0f, 11.0f));
    const TouchLocation second(2, TouchLocationState::Moved, Vector2(20.0f, 21.0f));
    const TouchLocation third(3, TouchLocationState::Released, Vector2(30.0f, 31.0f));
    const TouchCollection collection(std::vector<TouchLocation>{first, second, third});

    std::vector<int> idsInIterationOrder;
    for (const TouchLocation& location : collection)
    {
        idsInIterationOrder.push_back(location.getIdProperty());
    }

    EXPECT_EQ(idsInIterationOrder, (std::vector<int>{1, 2, 3}));
    // Iteration order matches indexed access exactly (no reordering).
    ASSERT_EQ(collection.getCountProperty(), 3);
    for (int i = 0; i < collection.getCountProperty(); ++i)
    {
        EXPECT_EQ(collection[static_cast<std::size_t>(i)].getIdProperty(), idsInIterationOrder[static_cast<std::size_t>(i)]);
    }
}

TEST(TouchCollectionTest, Tq036NonConstCollectionIterationIsReadOnly)
{
    TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(5, TouchLocationState::Pressed, Vector2::Zero),
        TouchLocation(6, TouchLocationState::Pressed, Vector2::Zero),
    });

    std::vector<int> ids;
    static_assert(std::is_same_v<decltype(*collection.begin()), const TouchLocation&>);
    for (const TouchLocation& location : collection)
    {
        ids.push_back(location.getIdProperty());
    }

    EXPECT_EQ(ids, (std::vector<int>{5, 6}));
    EXPECT_EQ(collection.end() - collection.begin(), collection.getCountProperty());
}

TEST(TouchCollectionTest, EmptyCollectionIterationIsANoOp)
{
    const TouchCollection empty;
    int visited = 0;
    for (const TouchLocation& location : empty)
    {
        (void)location;
        ++visited;
    }
    EXPECT_EQ(visited, 0);
    EXPECT_TRUE(empty.begin() == empty.end());
}

// XNA Add/Clear/Remove/RemoveAt/Insert IL_0000-0005, including missing items and bad indices.
TEST(TouchCollectionTest, Tq036PopulatedCollectionRejectsMutationWithoutChangingSnapshot)
{
    const TouchLocation first(1, TouchLocationState::Pressed, Vector2(10, 20));
    const TouchLocation second(2, TouchLocationState::Moved, Vector2(30, 40));
    const TouchLocation missing(3, TouchLocationState::Released, Vector2(50, 60));
    TouchCollection collection(std::vector<TouchLocation>{first, second});
    const auto unchanged = [&] {
        ASSERT_EQ(collection.getCountProperty(), 2);
        EXPECT_EQ(collection[0], first);
        EXPECT_EQ(collection[1], second);
        EXPECT_TRUE(collection.getIsReadOnlyProperty());
        EXPECT_TRUE(collection.getIsConnectedProperty());
    };
    EXPECT_THROW(collection.Add(missing), System::NotSupportedException); unchanged();
    EXPECT_THROW(collection.Clear(), System::NotSupportedException); unchanged();
    EXPECT_THROW((void)collection.Remove(first), System::NotSupportedException); unchanged();
    EXPECT_THROW((void)collection.Remove(missing), System::NotSupportedException); unchanged();
    for (const int index : {-1, 0, 2, 100})
    {
        EXPECT_THROW(collection.RemoveAt(index), System::NotSupportedException); unchanged();
        EXPECT_THROW(collection.Insert(index, missing), System::NotSupportedException); unchanged();
    }
}

// --- TouchLocation ---

TEST(TouchLocationTest, DefaultConstructorProducesInvalidLocation)
{
    const TouchLocation location;
    EXPECT_EQ(location.getIdProperty(), 0);
    EXPECT_EQ(location.getStateProperty(), TouchLocationState::Invalid);
    EXPECT_EQ(location.getPositionProperty(), Vector2::Zero);
}

TEST(TouchLocationTest, ThreeArgConstructorSetsIdStateAndPosition)
{
    const TouchLocation location(4, TouchLocationState::Pressed, Vector2(11.0f, 12.0f));
    EXPECT_EQ(location.getIdProperty(), 4);
    EXPECT_EQ(location.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(location.getPositionProperty(), Vector2(11.0f, 12.0f));

    TouchLocation previous;
    EXPECT_FALSE(location.TryGetPreviousLocation(previous));
}

TEST(TouchLocationTest, FiveArgConstructorTracksPreviousStateAndPosition)
{
    const TouchLocation location(4, TouchLocationState::Moved, Vector2(11.0f, 12.0f),
                                  TouchLocationState::Pressed, Vector2(1.0f, 2.0f));

    TouchLocation previous;
    ASSERT_TRUE(location.TryGetPreviousLocation(previous));
    EXPECT_EQ(previous.getIdProperty(), 4);
    EXPECT_EQ(previous.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(previous.getPositionProperty(), Vector2(1.0f, 2.0f));
}

// DEC-12 updated by TQ-019: XNA TryGetPreviousLocation IL_0008-004a writes ID -1, Invalid, Zero
// on failure, rather than retaining the current ID as FNA does.
TEST(TouchLocationTest, TryGetPreviousLocationFalsePathWritesInvalidPreviousLocationLikeXna)
{
    // A 3-arg location has no previous, so prevState defaults to Invalid.
    const TouchLocation location(7, TouchLocationState::Pressed, Vector2(3.0f, 4.0f));

    // Pre-seed the out-param with a distinct value to prove it gets overwritten (not left untouched).
    TouchLocation previous(99, TouchLocationState::Moved, Vector2(88.0f, 88.0f));
    EXPECT_FALSE(location.TryGetPreviousLocation(previous));

    // XNA TryGetPreviousLocation IL_0008-000a writes the failure ID independently of the current ID.
    EXPECT_EQ(previous.getIdProperty(), -1);
    EXPECT_EQ(previous.getStateProperty(), TouchLocationState::Invalid);
    EXPECT_EQ(previous.getPositionProperty(), Vector2::Zero);
}

TEST(TouchLocationTest, EqualityOperatorsForEqualAndDifferingInstances)
{
    const TouchLocation a(1, TouchLocationState::Pressed, Vector2(1.0f, 2.0f));
    const TouchLocation sameAsA(1, TouchLocationState::Pressed, Vector2(1.0f, 2.0f));
    const TouchLocation differentId(2, TouchLocationState::Pressed, Vector2(1.0f, 2.0f));
    const TouchLocation differentPosition(1, TouchLocationState::Pressed, Vector2(9.0f, 9.0f));
    const TouchLocation differentState(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f));

    EXPECT_TRUE(a.Equals(sameAsA));
    EXPECT_TRUE(a == sameAsA);
    EXPECT_FALSE(a != sameAsA);

    EXPECT_FALSE(a.Equals(differentId));
    EXPECT_TRUE(a != differentId);
    EXPECT_FALSE(a == differentId);

    EXPECT_FALSE(a.Equals(differentPosition));
    // XNA typed Equals IL_0000-004d omits State; op_Equality IL_0010-001e includes it.
    EXPECT_TRUE(a.Equals(differentState));
    EXPECT_FALSE(a == differentState);
    EXPECT_TRUE(a != differentState);
}

// XNA typed Equals IL_0000-004d compares previous coordinates but omits previous state.
// op_Equality IL_0040-004e includes previous state, so the two equality routes intentionally differ.
TEST(TouchLocationTest, EqualityDistinguishesPreviousStateAndPosition)
{
    const TouchLocation base(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f),
                             TouchLocationState::Pressed, Vector2(3.0f, 4.0f));
    const TouchLocation samePrev(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f),
                                 TouchLocationState::Pressed, Vector2(3.0f, 4.0f));
    const TouchLocation differentPrevState(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f),
                                            TouchLocationState::Moved, Vector2(3.0f, 4.0f));
    const TouchLocation differentPrevPosition(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f),
                                              TouchLocationState::Pressed, Vector2(9.0f, 9.0f));

    EXPECT_TRUE(base.Equals(samePrev));
    EXPECT_TRUE(base == samePrev);

    EXPECT_TRUE(base.Equals(differentPrevState)); // XNA typed Equals omits previous state.
    EXPECT_TRUE(base != differentPrevState);
    EXPECT_FALSE(base.Equals(differentPrevPosition));
    EXPECT_TRUE(base != differentPrevPosition);
}

TEST(TouchLocationTest, GetHashCodeIsConsistentForEqualInstances)
{
    const TouchLocation a(1, TouchLocationState::Pressed, Vector2(5.0f, 6.0f));
    const TouchLocation sameAsA(1, TouchLocationState::Pressed, Vector2(5.0f, 6.0f));

    EXPECT_EQ(a.GetHashCode(), sameAsA.GetHashCode());
}

// P5-004: FNA's GetHashCode is exactly `Id.GetHashCode() + Position.GetHashCode()`
// (TouchLocation.cs:94-97) — it deliberately excludes State and the previous location. So two
// locations sharing id+position but differing only in State collide in hash yet are unequal; pin that.
TEST(TouchLocationTest, GetHashCodeMatchesFnaIdPlusPositionFormula)
{
    const Vector2 position(5.0f, 6.0f);
    const TouchLocation pressed(1, TouchLocationState::Pressed, position);
    const TouchLocation moved(1, TouchLocationState::Moved, position);

    const int expected = 1 + position.GetHashCode();
    EXPECT_EQ(pressed.GetHashCode(), expected);
    // XNA TouchLocation.GetHashCode IL_0000-0023: id + SingleHash(x) + SingleHash(y), wrapping.
    // 1 + bits(5)=0x40a00000 + bits(6)=0x40c00000 = 0x81600001 = -2124414975.
    EXPECT_EQ(pressed.GetHashCode(), -2124414975);
    // State is not part of the hash (FNA), so these collide...
    EXPECT_EQ(pressed.GetHashCode(), moved.GetHashCode());
    // XNA typed Equals IL_0000-004d omits State; op_Equality IL_0010-001e includes it.
    EXPECT_TRUE(pressed.Equals(moved));
    EXPECT_FALSE(pressed == moved);
}

TEST(TouchLocationTest, ToStringContainsPositionValues)
{
    const TouchLocation location(1, TouchLocationState::Pressed, Vector2(7.0f, 8.0f));
    const std::string s = location.ToString();

    EXPECT_EQ(s.rfind("{Position:", 0), 0u);
    EXPECT_NE(s.find('7'), std::string::npos);
    EXPECT_NE(s.find('8'), std::string::npos);
}

// Exact format parity with FNA: "{Position:" + Position.ToString() + "}", where Vector2.ToString() is
// "{X:.. Y:..}" — so the whole string is byte-for-byte "{Position:{X:7 Y:8}}".
TEST(TouchLocationTest, ToStringMatchesFnaFormatExactly)
{
    const TouchLocation location(1, TouchLocationState::Pressed, Vector2(7.0f, 8.0f));
    EXPECT_EQ(location.ToString(), "{Position:{X:7 Y:8}}");
}

// N-006: the XNA constructors leave getPressureEXT at 0 (XNA 4.0 dropped Pressure); the CNAEXT
// pressure-carrying constructors set it.
TEST(TouchLocationTest, PressureDefaultsToZeroForXnaConstructorsAndIsSetByEXTConstructors)
{
    EXPECT_FLOAT_EQ(TouchLocation().getPressureEXT(), 0.0f);
    EXPECT_FLOAT_EQ(TouchLocation(1, TouchLocationState::Pressed, Vector2(1.0f, 2.0f)).getPressureEXT(), 0.0f);
    EXPECT_FLOAT_EQ(TouchLocation(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f),
                                  TouchLocationState::Pressed, Vector2(3.0f, 4.0f)).getPressureEXT(), 0.0f);

    // 4-arg pressure ctor.
    const TouchLocation p(1, TouchLocationState::Pressed, Vector2(1.0f, 2.0f), 0.5f);
    EXPECT_FLOAT_EQ(p.getPressureEXT(), 0.5f);
    EXPECT_EQ(p.getPositionProperty(), Vector2(1.0f, 2.0f));

    // 6-arg pressure ctor (with previous location).
    const TouchLocation pPrev(1, TouchLocationState::Moved, Vector2(1.0f, 2.0f),
                              TouchLocationState::Pressed, Vector2(3.0f, 4.0f), 0.9f);
    EXPECT_FLOAT_EQ(pPrev.getPressureEXT(), 0.9f);
    TouchLocation previous;
    ASSERT_TRUE(pPrev.TryGetPreviousLocation(previous));
    EXPECT_EQ(previous.getPositionProperty(), Vector2(3.0f, 4.0f));
}

// N-006: pressure is deliberately excluded from Equals/GetHashCode/ToString so those stay
// FNA-frozen — two locations that differ only in pressure remain equal.
TEST(TouchLocationTest, PressureIsExcludedFromEqualityHashAndToString)
{
    const TouchLocation lo(1, TouchLocationState::Pressed, Vector2(7.0f, 8.0f), 0.1f);
    const TouchLocation hi(1, TouchLocationState::Pressed, Vector2(7.0f, 8.0f), 0.9f);

    EXPECT_TRUE(lo.Equals(hi));
    EXPECT_TRUE(lo == hi);
    EXPECT_FALSE(lo != hi);
    EXPECT_EQ(lo.GetHashCode(), hi.GetHashCode());
    EXPECT_EQ(lo.ToString(), hi.ToString());
}

// --- GestureSample ---

TEST(GestureSampleTest, DefaultConstructorProducesZeroedNoneSample)
{
    const GestureSample sample;
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::None);
    EXPECT_EQ(sample.getTimestampProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(sample.getPositionProperty(), Vector2::Zero);
    EXPECT_EQ(sample.getPosition2Property(), Vector2::Zero);
    EXPECT_EQ(sample.getDeltaProperty(), Vector2::Zero);
    EXPECT_EQ(sample.getDelta2Property(), Vector2::Zero);
    EXPECT_EQ(sample.getFingerIdEXTProperty(), TouchPanel::NO_FINGER);
    EXPECT_EQ(sample.getFingerId2EXTProperty(), TouchPanel::NO_FINGER);
}

TEST(GestureSampleTest, PublicConstructorSetsFieldsAndDefaultsFingerIdsToNoFinger)
{
    const GestureSample sample(GestureType::Tap, System::TimeSpan::FromMilliseconds(5.0),
                                Vector2(1.0f, 2.0f), Vector2(3.0f, 4.0f),
                                Vector2(5.0f, 6.0f), Vector2(7.0f, 8.0f));

    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Tap);
    EXPECT_EQ(sample.getTimestampProperty(), System::TimeSpan::FromMilliseconds(5.0));
    EXPECT_EQ(sample.getPositionProperty(), Vector2(1.0f, 2.0f));
    EXPECT_EQ(sample.getPosition2Property(), Vector2(3.0f, 4.0f));
    EXPECT_EQ(sample.getDeltaProperty(), Vector2(5.0f, 6.0f));
    EXPECT_EQ(sample.getDelta2Property(), Vector2(7.0f, 8.0f));
    EXPECT_EQ(sample.getFingerIdEXTProperty(), TouchPanel::NO_FINGER);
    EXPECT_EQ(sample.getFingerId2EXTProperty(), TouchPanel::NO_FINGER);
}

TEST(GestureSampleTest, InternalConstructorSetsExplicitFingerIds)
{
    const GestureSample sample(GestureType::Pinch, System::TimeSpan::Zero,
                                Vector2::Zero, Vector2::Zero, Vector2::Zero, Vector2::Zero,
                                10, 20);

    EXPECT_EQ(sample.getFingerIdEXTProperty(), 10);
    EXPECT_EQ(sample.getFingerId2EXTProperty(), 20);
}

// --- TouchPanelCapabilities ---

TEST(TouchPanelCapabilitiesTest, DefaultConstructorProducesDisconnectedZeroCapacity)
{
    const TouchPanelCapabilities caps;
    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 0);
}

TEST(TouchPanelCapabilitiesTest, ParameterizedConstructorSetsConnectionAndMaxTouchCount)
{
    const TouchPanelCapabilities caps(true, 4);
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getMaximumTouchCountProperty(), 4);
}

TEST(TouchLocationTest, Tq002PreviousLocationContainsExactlyOneHistoryStep)
{
    // XNA TryGetPreviousLocation IL_004b-0099 copies one history step and clears older history.
    // Both calls overwrite the output; failure uses ID -1 (IL_0008-000a).
    const TouchLocation current(17, TouchLocationState::Moved, Vector2(9, 12),
                                TouchLocationState::Pressed, Vector2(3, 4));
    TouchLocation previous(99, TouchLocationState::Released, Vector2(50, 60));
    ASSERT_TRUE(current.TryGetPreviousLocation(previous));
    EXPECT_EQ(previous.getIdProperty(), 17);
    EXPECT_EQ(previous.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(previous.getPositionProperty(), Vector2(3, 4));
    TouchLocation older(99, TouchLocationState::Released, Vector2(50, 60));
    EXPECT_FALSE(previous.TryGetPreviousLocation(older));
    EXPECT_EQ(older.getIdProperty(), -1); // XNA TryGetPreviousLocation IL_0008-000a.
    EXPECT_EQ(older.getStateProperty(), TouchLocationState::Invalid);
    EXPECT_EQ(older.getPositionProperty(), Vector2::Zero);
}

TEST(TouchCollectionTest, Tq019ContainsAndIndexOfUseFullOperatorEqualityRatherThanOnlyId)
{
    // XNA TouchCollection.IndexOf IL_0004-0014 calls op_Equality, not TouchLocation.Equals.
    // XNA TouchLocation.op_Equality IL_0000-0072 includes both states and both positions.
    const TouchLocation value(401, TouchLocationState::Moved, Vector2(10, 20),
                               TouchLocationState::Pressed, Vector2(3, 4));
    const TouchCollection collection(std::vector<TouchLocation>{value});
    EXPECT_TRUE(collection.Contains(value));
    EXPECT_EQ(collection.IndexOf(value), 0);
    const std::vector<TouchLocation> different{
        TouchLocation(402, TouchLocationState::Moved, Vector2(10, 20), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(401, TouchLocationState::Released, Vector2(10, 20), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(401, TouchLocationState::Moved, Vector2(11, 20), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(401, TouchLocationState::Moved, Vector2(10, 21), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(401, TouchLocationState::Moved, Vector2(10, 20), TouchLocationState::Moved, Vector2(3, 4)),
        TouchLocation(401, TouchLocationState::Moved, Vector2(10, 20), TouchLocationState::Pressed, Vector2(5, 4)),
        TouchLocation(401, TouchLocationState::Moved, Vector2(10, 20), TouchLocationState::Pressed, Vector2(3, 5))
    };
    for (const auto& other : different)
    {
        EXPECT_FALSE(collection.Contains(other));
        EXPECT_EQ(collection.IndexOf(other), -1);
    }
}

TEST(TouchCollectionTest, Tq019FindByIdCopiesFirstMatchIncludingHistory)
{
    // XNA TouchCollection.FindById(int,out) IL_0004-0024 assigns the first matching full value.
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(403, TouchLocationState::Pressed, Vector2(1, 2)),
        TouchLocation(404, TouchLocationState::Released, Vector2(30, 40),
                      TouchLocationState::Moved, Vector2(10, 20)),
        TouchLocation(404, TouchLocationState::Pressed, Vector2(90, 91))
    });
    TouchLocation found(999, TouchLocationState::Invalid, Vector2(-1, -2));
    ASSERT_TRUE(collection.FindById(404, found));
    EXPECT_EQ(found.getIdProperty(), 404);
    EXPECT_EQ(found.getStateProperty(), TouchLocationState::Released);
    EXPECT_EQ(found.getPositionProperty(), Vector2(30, 40));
    TouchLocation previous;
    ASSERT_TRUE(found.TryGetPreviousLocation(previous));
    EXPECT_EQ(previous.getIdProperty(), 404);
    EXPECT_EQ(previous.getStateProperty(), TouchLocationState::Moved);
    EXPECT_EQ(previous.getPositionProperty(), Vector2(10, 20));
}

TEST(TouchCollectionTest, Tq019BothIndexerOverloadsRejectCountAndWrappedNegativeIndex)
{
    // XNA TouchCollection.get_Item IL_0000-0017 checks index < 0 or index >= Count.
    TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(405, TouchLocationState::Pressed, Vector2(10, 20)),
        TouchLocation(406, TouchLocationState::Moved, Vector2(30, 40))
    });
    const auto& constant = collection;
    EXPECT_EQ(collection[1].getIdProperty(), 406);
    EXPECT_EQ(constant[1].getIdProperty(), 406);
    for (const auto index : {std::size_t{2}, static_cast<std::size_t>(-1)})
    {
        EXPECT_THROW((void)collection[index], System::ArgumentOutOfRangeException);
        EXPECT_THROW((void)constant[index], System::ArgumentOutOfRangeException);
    }
    ASSERT_EQ(constant.getCountProperty(), 2);
    EXPECT_EQ(constant[0].getIdProperty(), 405);
    EXPECT_EQ(constant[0].getPositionProperty(), Vector2(10, 20));
    EXPECT_EQ(constant[1].getIdProperty(), 406);
    EXPECT_EQ(constant[1].getPositionProperty(), Vector2(30, 40));
}

TEST(GestureSampleTest, Tq019EightArgumentConstructorPreservesEveryPublicField)
{
    // XNA GestureSample..ctor IL_0000-002d stores all six XNA fields unchanged;
    // the CNAEXT overload additionally stores both finger IDs.
    const GestureSample sample(GestureType::Pinch, System::TimeSpan::FromTicks(123456789),
                               Vector2(-1, 2), Vector2(3, -4), Vector2(-5, 6), Vector2(7, -8), 407, 408);
    EXPECT_EQ(sample.getGestureTypeProperty(), GestureType::Pinch);
    EXPECT_EQ(sample.getTimestampProperty().getTicksProperty(), 123456789);
    EXPECT_EQ(sample.getPositionProperty(), Vector2(-1, 2));
    EXPECT_EQ(sample.getPosition2Property(), Vector2(3, -4));
    EXPECT_EQ(sample.getDeltaProperty(), Vector2(-5, 6));
    EXPECT_EQ(sample.getDelta2Property(), Vector2(7, -8));
    EXPECT_EQ(sample.getFingerIdEXTProperty(), 407);
    EXPECT_EQ(sample.getFingerId2EXTProperty(), 408);
}

// XNA mutators and set_Item IL_0000-0005 throw even when the eight-slot constructor limit is reached.
TEST(TouchCollectionTest, Tq036FullCollectionRejectsMutationBeforeCapacityOrIndexChecks)
{
    const TouchLocation item(1, TouchLocationState::Pressed, Vector2(2, 3));
    TouchCollection full(std::vector<TouchLocation>(8, item));
    EXPECT_THROW(full.Add(item), System::NotSupportedException);
    EXPECT_THROW(full.Insert(8, item), System::NotSupportedException);
    EXPECT_THROW(full.RemoveAt(-1), System::NotSupportedException);
    EXPECT_THROW((void)full.Remove(item), System::NotSupportedException);
    EXPECT_THROW(full.Clear(), System::NotSupportedException);
    EXPECT_THROW(full.setItemProperty(8, item), System::NotSupportedException);
    ASSERT_EQ(full.getCountProperty(), 8);
    for (const auto& value : full) EXPECT_EQ(value, item);
    EXPECT_TRUE(full.getIsConnectedProperty());
}
