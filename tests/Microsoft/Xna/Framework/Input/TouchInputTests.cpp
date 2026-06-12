// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    void ResetTouchState()
    {
        const auto currentSnapshot = CNA::Internal::Input::InputManager::GetTouchState();
        for (const auto& touchLocation : currentSnapshot)
        {
            CNA::Internal::Input::InputManager::SetTouchState(
                touchLocation.getIdProperty(),
                TouchLocationState::Released,
                touchLocation.getPositionProperty()
            );
        }

        (void)TouchPanel::GetState();
        (void)TouchPanel::GetState();
    }
}

TEST(TouchInputTest, GetStateReflectsCurrentTouchSnapshot)
{
    ResetTouchState();

    CNA::Internal::Input::InputManager::SetTouchState(11, TouchLocationState::Pressed, Vector2(100.5f, 200.25f));

    const auto state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 1);

    const auto& touchLocation = state[0];
    EXPECT_EQ(touchLocation.getIdProperty(), 11);
    EXPECT_EQ(touchLocation.getStateProperty(), TouchLocationState::Pressed);
    EXPECT_FLOAT_EQ(touchLocation.getPositionProperty().X, 100.5f);
    EXPECT_FLOAT_EQ(touchLocation.getPositionProperty().Y, 200.25f);

    const auto nextState = TouchPanel::GetState();
    ASSERT_EQ(nextState.getCountProperty(), 1);
    EXPECT_EQ(nextState[0].getIdProperty(), 11);
    EXPECT_EQ(nextState[0].getStateProperty(), TouchLocationState::Moved);

    ResetTouchState();
}

TEST(TouchInputTest, ReleasedTouchIsReturnedOnceAndThenRemoved)
{
    ResetTouchState();

    CNA::Internal::Input::InputManager::SetTouchState(21, TouchLocationState::Pressed, Vector2(30.0f, 40.0f));
    (void)TouchPanel::GetState();

    CNA::Internal::Input::InputManager::SetTouchState(21, TouchLocationState::Released, Vector2(35.0f, 45.0f));

    const auto releasedState = TouchPanel::GetState();
    ASSERT_EQ(releasedState.getCountProperty(), 1);
    EXPECT_EQ(releasedState[0].getIdProperty(), 21);
    EXPECT_EQ(releasedState[0].getStateProperty(), TouchLocationState::Released);

    const auto afterReleasedState = TouchPanel::GetState();
    EXPECT_EQ(afterReleasedState.getCountProperty(), 0);

    ResetTouchState();
}

TEST(TouchInputTest, GetStateHandlesMultipleTouchIdsAndKeepsDeterministicOrder)
{
    ResetTouchState();

    CNA::Internal::Input::InputManager::SetTouchState(42, TouchLocationState::Moved, Vector2(400.0f, 500.0f));
    CNA::Internal::Input::InputManager::SetTouchState(7, TouchLocationState::Pressed, Vector2(70.0f, 80.0f));

    const auto state = TouchPanel::GetState();
    ASSERT_EQ(state.getCountProperty(), 2);

    EXPECT_EQ(state[0].getIdProperty(), 7);
    EXPECT_EQ(state[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_EQ(state[1].getIdProperty(), 42);
    EXPECT_EQ(state[1].getStateProperty(), TouchLocationState::Moved);

    ResetTouchState();
}
