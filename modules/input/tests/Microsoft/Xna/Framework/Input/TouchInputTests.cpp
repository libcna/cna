// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "System/InvalidOperationException.hpp"

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

TEST(TouchInputTest, EnqueueGestureAndReadGestureFollowFifoOrder)
{
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

// P5-001 audit resolution: XNA/FNA's TouchCollection.IsReadOnly is hard-coded true, but that flag is
// advisory only — FNA still mutates the (non-null) backing List returned by TouchPanel.GetState. CNA is
// faithful: IsReadOnly stays true while Add/Clear/settable-indexer succeed. The one intentional C++
// deviation is that a default-constructed collection is empty+mutable (value-vector backing) rather than
// null-backed and throwing NullReferenceException on mutation as a default FNA collection would.
TEST(TouchCollectionTest, IsReadOnlyIsAdvisoryAndMutationStillSucceedsLikeFna)
{
    TouchCollection collection; // default: FNA would be null-backed; CNA is empty + mutable
    EXPECT_TRUE(collection.getIsReadOnlyProperty());

    EXPECT_NO_THROW(collection.Add(TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero)))
        << "mutating a default collection must not throw (FNA would NRE; CNA is empty+mutable)";
    EXPECT_EQ(collection.getCountProperty(), 1);
    EXPECT_TRUE(collection.getIsReadOnlyProperty()) << "IsReadOnly must remain true after mutation";

    collection[0] = TouchLocation(2, TouchLocationState::Moved, Vector2::Zero); // settable indexer
    EXPECT_EQ(collection[0].getIdProperty(), 2);

    EXPECT_NO_THROW(collection.Clear());
    EXPECT_TRUE(collection.getIsReadOnlyProperty());
}

TEST(TouchCollectionTest, IsConnectedReflectsTouchDeviceExistsFlag)
{
    TouchPanel::setTouchDeviceExistsProperty(true);
    const TouchCollection connected;
    EXPECT_TRUE(connected.getIsConnectedProperty());

    TouchPanel::setTouchDeviceExistsProperty(false);
    const TouchCollection disconnected;
    EXPECT_FALSE(disconnected.getIsConnectedProperty());
}

TEST(TouchCollectionTest, OperatorIndexConstAndMutableAccessTouchLocations)
{
    std::vector<TouchLocation> locations{
        TouchLocation(5, TouchLocationState::Pressed, Vector2(1.0f, 2.0f))
    };
    TouchCollection collection(locations);

    const TouchCollection& constRef = collection;
    EXPECT_EQ(constRef[0].getIdProperty(), 5);

    collection[0] = TouchLocation(6, TouchLocationState::Moved, Vector2(3.0f, 4.0f));
    EXPECT_EQ(constRef[0].getIdProperty(), 6);
    EXPECT_EQ(constRef[0].getStateProperty(), TouchLocationState::Moved);
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

// FNA writes the out-param unconditionally, even on the not-found path (TouchCollection.cs:112-131):
// the "not found" case still assigns touchLocation = new TouchLocation(-1, Invalid, Vector2.Zero)
// before returning false, mirroring the same pattern already fixed for
// TouchLocation::TryGetPreviousLocation (DEC-12). Verify CNA's FindById does the same rather than
// leaving the caller-supplied out-param untouched.
TEST(TouchCollectionTest, FindByIdWritesInvalidSentinelOnOutParamWhenMissing)
{
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(7, TouchLocationState::Pressed, Vector2(9.0f, 9.0f))
    });

    TouchLocation notFound(42, TouchLocationState::Moved, Vector2(1.0f, 2.0f));
    EXPECT_FALSE(collection.FindById(999, notFound));
    EXPECT_EQ(notFound.getIdProperty(), -1);
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
    EXPECT_EQ(notFound.getIdProperty(), -1);
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
    // hit std::vector::insert UB. It must now throw std::out_of_range.
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero)
    });

    std::vector<TouchLocation> destination(2); // size 2 -> valid indices are 0,1,2

    EXPECT_THROW(collection.CopyTo(destination, -1), std::out_of_range);
    EXPECT_THROW(collection.CopyTo(destination, 3), std::out_of_range);   // 3 > size()
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
    // Task 904: out-of-range indexer access must throw std::out_of_range, not read OOB.
    const TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(1, TouchLocationState::Pressed, Vector2::Zero)
    });
    EXPECT_NO_THROW((void)collection[0]);
    EXPECT_THROW((void)collection[1], std::out_of_range);
    EXPECT_THROW((void)collection[std::size_t{100}], std::out_of_range);

    const TouchCollection empty;
    EXPECT_EQ(empty.getCountProperty(), 0);
    EXPECT_TRUE(empty.getIsReadOnlyProperty()); // XNA/FNA: TouchCollection.IsReadOnly is always true
    EXPECT_THROW((void)empty[0], std::out_of_range);
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

TEST(TouchCollectionTest, MutableIterationVisitsEveryElementOnceInOrder)
{
    TouchCollection collection(std::vector<TouchLocation>{
        TouchLocation(5, TouchLocationState::Pressed, Vector2::Zero),
        TouchLocation(6, TouchLocationState::Pressed, Vector2::Zero),
    });

    std::vector<int> ids;
    for (TouchLocation& location : collection)
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

TEST(TouchCollectionTest, AddClearRemoveRemoveAtAndInsertMutateCollection)
{
    TouchCollection collection;
    const TouchLocation a(1, TouchLocationState::Pressed, Vector2::Zero);
    const TouchLocation b(2, TouchLocationState::Pressed, Vector2::Zero);
    const TouchLocation c(3, TouchLocationState::Pressed, Vector2::Zero);

    collection.Add(a);
    collection.Add(b);
    EXPECT_EQ(collection.getCountProperty(), 2);

    collection.Insert(1, c);
    ASSERT_EQ(collection.getCountProperty(), 3);
    EXPECT_EQ(collection[1].getIdProperty(), 3);

    EXPECT_TRUE(collection.Remove(c));
    EXPECT_FALSE(collection.Remove(c));
    EXPECT_EQ(collection.getCountProperty(), 2);

    collection.RemoveAt(0);
    ASSERT_EQ(collection.getCountProperty(), 1);
    EXPECT_EQ(collection[0].getIdProperty(), 2);

    collection.Clear();
    EXPECT_EQ(collection.getCountProperty(), 0);
    EXPECT_TRUE(collection.empty());
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

// DEC-12: matches FNA — TryGetPreviousLocation writes the out-param on the false path too (the Invalid
// previous location), rather than leaving it untouched.
TEST(TouchLocationTest, TryGetPreviousLocationFalsePathWritesInvalidPreviousLocationLikeFna)
{
    // A 3-arg location has no previous, so prevState defaults to Invalid.
    const TouchLocation location(7, TouchLocationState::Pressed, Vector2(3.0f, 4.0f));

    // Pre-seed the out-param with a distinct value to prove it gets overwritten (not left untouched).
    TouchLocation previous(99, TouchLocationState::Moved, Vector2(88.0f, 88.0f));
    EXPECT_FALSE(location.TryGetPreviousLocation(previous));

    // FNA writes new TouchLocation(Id, prevState, prevPosition) even when returning false.
    EXPECT_EQ(previous.getIdProperty(), 7);
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
    EXPECT_FALSE(a.Equals(differentState));
}

// TouchLocation::Equals also compares the PREVIOUS state/position (not just the current fields), so two
// locations that agree on id/state/position but differ in their previous location must be unequal.
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

    EXPECT_FALSE(base.Equals(differentPrevState));
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
    // State is not part of the hash (FNA), so these collide...
    EXPECT_EQ(pressed.GetHashCode(), moved.GetHashCode());
    // ...but they are NOT equal, because Equals compares State.
    EXPECT_FALSE(pressed.Equals(moved));
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
