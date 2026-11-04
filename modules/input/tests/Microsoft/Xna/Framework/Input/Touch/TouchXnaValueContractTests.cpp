// SPDX-License-Identifier: MS-PL
#include <any>
#include <vector>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

TEST(TouchLocationTest, Tq019NoPreviousHistoryUsesXnaSentinelForEveryCurrentState)
{
    // XNA TryGetPreviousLocation IL_0000-004a ignores current state/ID and invalid saved coordinates.
    for (const auto state : {TouchLocationState::Invalid, TouchLocationState::Pressed,
                             TouchLocationState::Moved, TouchLocationState::Released})
    {
        const TouchLocation current(17, state, Vector2(9, 12),
                                    TouchLocationState::Invalid, Vector2(88, 99));
        TouchLocation previous(99, TouchLocationState::Moved, Vector2(50, 60),
                                TouchLocationState::Pressed, Vector2(1, 2));
        EXPECT_FALSE(current.TryGetPreviousLocation(previous));
        EXPECT_EQ(previous.getIdProperty(), -1);
        EXPECT_EQ(previous.getStateProperty(), TouchLocationState::Invalid);
        EXPECT_EQ(previous.getPositionProperty(), Vector2::Zero);
        TouchLocation older(98, TouchLocationState::Released, Vector2(77, 66));
        EXPECT_FALSE(previous.TryGetPreviousLocation(older));
        EXPECT_EQ(older.getIdProperty(), -1);
        EXPECT_EQ(older.getStateProperty(), TouchLocationState::Invalid);
        EXPECT_EQ(older.getPositionProperty(), Vector2::Zero);
    }
}

TEST(TouchLocationTest, Tq019PreviousHistorySuccessUsesStoredStateForEveryCurrentState)
{
    // XNA TryGetPreviousLocation IL_004b-0099 tests saved state, not current state, and clears history.
    for (const auto currentState : {TouchLocationState::Invalid, TouchLocationState::Pressed,
                                    TouchLocationState::Moved, TouchLocationState::Released})
    {
        for (const auto savedState : {TouchLocationState::Pressed, TouchLocationState::Moved,
                                      TouchLocationState::Released})
        {
            const TouchLocation current(17, currentState, Vector2(9, 12), savedState, Vector2(3, 4));
            TouchLocation previous(99, TouchLocationState::Moved, Vector2(50, 60));
            ASSERT_TRUE(current.TryGetPreviousLocation(previous));
            EXPECT_EQ(previous.getIdProperty(), 17);
            EXPECT_EQ(previous.getStateProperty(), savedState);
            EXPECT_EQ(previous.getPositionProperty(), Vector2(3, 4));
            TouchLocation older(99, TouchLocationState::Moved, Vector2(50, 60));
            EXPECT_FALSE(previous.TryGetPreviousLocation(older));
            EXPECT_EQ(older.getIdProperty(), -1);
            EXPECT_EQ(older.getStateProperty(), TouchLocationState::Invalid);
            EXPECT_EQ(older.getPositionProperty(), Vector2::Zero);
        }
    }
}

TEST(TouchLocationTest, Tq019PreviousHistoryCanOverwriteItsOwnSource)
{
    // XNA TryGetPreviousLocation IL_004b-0099 copies saved fields before clearing saved history,
    // then returns true unconditionally; the source and out location may be the same value.
    TouchLocation location(17, TouchLocationState::Moved, Vector2(9, 12),
                            TouchLocationState::Pressed, Vector2(3, 4));
    ASSERT_TRUE(location.TryGetPreviousLocation(location));
    EXPECT_EQ(location.getIdProperty(), 17);
    EXPECT_EQ(location.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(location.getPositionProperty(), Vector2(3, 4));
    EXPECT_FALSE(location.TryGetPreviousLocation(location));
    EXPECT_EQ(location.getIdProperty(), -1);
    EXPECT_EQ(location.getStateProperty(), TouchLocationState::Invalid);
    EXPECT_EQ(location.getPositionProperty(), Vector2::Zero);
}

TEST(TouchLocationTest, Tq019TypedAndObjectEqualsOmitStatesWhileOperatorsRetainThem)
{
    // XNA typed Equals IL_0000-004d omits both states; Equals(object) IL_000a-0016 delegates to it.
    // op_Equality IL_0010-001e,0040-004e includes both states.
    const TouchLocation value(17, TouchLocationState::Moved, Vector2(9, 12),
                               TouchLocationState::Pressed, Vector2(3, 4));
    EXPECT_TRUE(value.Equals(value));
    EXPECT_TRUE(value.Equals(std::any(value)));
    EXPECT_TRUE(value == value);
    EXPECT_FALSE(value != value);
    for (const auto& changedState : {
            TouchLocation(17, TouchLocationState::Released, Vector2(9, 12),
                          TouchLocationState::Pressed, Vector2(3, 4)),
            TouchLocation(17, TouchLocationState::Moved, Vector2(9, 12),
                          TouchLocationState::Released, Vector2(3, 4))})
    {
        EXPECT_TRUE(value.Equals(changedState));
        EXPECT_TRUE(value.Equals(std::any(changedState)));
        EXPECT_FALSE(value == changedState);
        EXPECT_TRUE(value != changedState);
    }
    const std::vector<TouchLocation> different{
        TouchLocation(18, TouchLocationState::Moved, Vector2(9, 12), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(17, TouchLocationState::Moved, Vector2(10, 12), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(17, TouchLocationState::Moved, Vector2(9, 13), TouchLocationState::Pressed, Vector2(3, 4)),
        TouchLocation(17, TouchLocationState::Moved, Vector2(9, 12), TouchLocationState::Pressed, Vector2(5, 4)),
        TouchLocation(17, TouchLocationState::Moved, Vector2(9, 12), TouchLocationState::Pressed, Vector2(3, 5))
    };
    for (const auto& other : different)
    {
        EXPECT_FALSE(value.Equals(other));
        EXPECT_FALSE(value.Equals(std::any(other)));
        EXPECT_FALSE(value == other);
        EXPECT_TRUE(value != other);
    }
    EXPECT_FALSE(value.Equals(std::any{}));
    EXPECT_FALSE(value.Equals(std::any(17)));
}

TEST(TouchCollectionTest, Tq019MissingIdWritesDefaultXnaValueForEmptyAndNonemptyCollections)
{
    // XNA FindById(int,out) IL_0032-003a uses initobj (ID 0), unlike failed previous lookup (ID -1).
    for (const auto& collection : {TouchCollection(), TouchCollection(std::vector<TouchLocation>{
            TouchLocation(7, TouchLocationState::Pressed, Vector2(8, 9))})})
    {
        TouchLocation missing(99, TouchLocationState::Released, Vector2(50, 60),
                               TouchLocationState::Moved, Vector2(10, 20));
        EXPECT_FALSE(collection.FindById(100, missing));
        EXPECT_EQ(missing.getIdProperty(), 0);
        EXPECT_EQ(missing.getStateProperty(), TouchLocationState::Invalid);
        EXPECT_EQ(missing.getPositionProperty(), Vector2::Zero);
        TouchLocation previous(98, TouchLocationState::Moved, Vector2(70, 80));
        EXPECT_FALSE(missing.TryGetPreviousLocation(previous));
        EXPECT_EQ(previous.getIdProperty(), -1);
        EXPECT_EQ(previous.getStateProperty(), TouchLocationState::Invalid);
        EXPECT_EQ(previous.getPositionProperty(), Vector2::Zero);
    }
}
