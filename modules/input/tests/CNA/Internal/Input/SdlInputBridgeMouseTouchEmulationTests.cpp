// SPDX-License-Identifier: MS-PL
//
// TouchPanel::MouseTouchEmulationEnabledEXT: a CNAEXT opt-in that reports left mouse
// button input as touch input, for hosts that have no touch digitiser. It is off by
// default, because XNA and FNA both feed TouchPanel from real finger events only.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Platform::MouseButtonEvent;
using CNA::Platform::MouseMotionEvent;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    constexpr int DisplaySize = 800;

    CNA::Platform::PlatformEvent buttonEvent(const bool pressed, const float x, const float y,
                                             const std::uint8_t button = 1)
    {
        MouseButtonEvent event{};
        event.button = button;
        event.pressed = pressed;
        event.x = x;
        event.y = y;
        return event;
    }

    CNA::Platform::PlatformEvent motionEvent(const float x, const float y,
                                             const float dx, const float dy)
    {
        MouseMotionEvent event{};
        event.x = x;
        event.y = y;
        event.deltaX = dx;
        event.deltaY = dy;
        return event;
    }

    struct MouseTouchEmulationTest : ::testing::Test
    {
        void SetUp() override
        {
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

TEST_F(MouseTouchEmulationTest, IsOffByDefaultSoMouseInputIsNotTouchInput)
{
    EXPECT_FALSE(TouchPanel::getMouseTouchEmulationEnabledEXT());

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f));
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0);

    PlatformInputBridge::ProcessEvent(motionEvent(200.0f, 150.0f, 100.0f, 50.0f));
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0);
}

TEST_F(MouseTouchEmulationTest, PressReportsAPressedTouchAtTheCursor)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 120.0f, 240.0f));

    const TouchCollection touches = TouchPanel::GetState();
    ASSERT_EQ(touches.getCountProperty(), 1);
    EXPECT_EQ(touches[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_FLOAT_EQ(touches[0].getPositionProperty().X, 120.0f);
    EXPECT_FLOAT_EQ(touches[0].getPositionProperty().Y, 240.0f);
}

TEST_F(MouseTouchEmulationTest, MotionWhileHeldReportsAMovedTouch)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f));
    PlatformInputBridge::ProcessEvent(motionEvent(160.0f, 130.0f, 60.0f, 30.0f));

    const TouchCollection touches = TouchPanel::GetState();
    ASSERT_EQ(touches.getCountProperty(), 1);
    EXPECT_EQ(touches[0].getStateProperty(), TouchLocationState::Moved);
    EXPECT_FLOAT_EQ(touches[0].getPositionProperty().X, 160.0f);
    EXPECT_FLOAT_EQ(touches[0].getPositionProperty().Y, 130.0f);
}

TEST_F(MouseTouchEmulationTest, MotionWithTheButtonUpReportsNothing)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);

    PlatformInputBridge::ProcessEvent(motionEvent(160.0f, 130.0f, 60.0f, 30.0f));

    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0);
}

TEST_F(MouseTouchEmulationTest, ReleaseEndsTheTouch)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f));
    PlatformInputBridge::ProcessEvent(buttonEvent(false, 100.0f, 100.0f));

    const TouchCollection touches = TouchPanel::GetState();
    ASSERT_EQ(touches.getCountProperty(), 1);
    EXPECT_EQ(touches[0].getStateProperty(), TouchLocationState::Released);
}

TEST_F(MouseTouchEmulationTest, OnlyTheLeftButtonCounts)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f, 3));

    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0);
}

TEST_F(MouseTouchEmulationTest, PressMakesATouchDeviceExist)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);
    ASSERT_FALSE(TouchPanel::getTouchDeviceExistsProperty());

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f));

    EXPECT_TRUE(TouchPanel::getTouchDeviceExistsProperty());
}

// The whole point of routing through the same entry points a real finger uses: the
// gesture recognizer cannot tell the difference, so a touch-only game that reads
// FreeDrag works with a pointer.
TEST_F(MouseTouchEmulationTest, ADragProducesAFreeDragGesture)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);
    TouchPanel::setEnabledGesturesProperty(GestureType::FreeDrag);

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f));
    PlatformInputBridge::ProcessEvent(motionEvent(180.0f, 160.0f, 80.0f, 60.0f));

    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    const GestureSample gesture = TouchPanel::ReadGesture();
    EXPECT_EQ(gesture.getGestureTypeProperty(), GestureType::FreeDrag);
    EXPECT_FLOAT_EQ(gesture.getPositionProperty().X, 180.0f);
    EXPECT_FLOAT_EQ(gesture.getPositionProperty().Y, 160.0f);
    EXPECT_GT(gesture.getDeltaProperty().LengthSquared(), 0.0f);
}

// A released touch stays in the collection until the frame ends -- that is TouchPanel's
// own behavior for a real finger too -- so the second press goes after a frame boundary.
TEST_F(MouseTouchEmulationTest, ASecondDragStartsAFreshTouchAfterRelease)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 100.0f, 100.0f));
    PlatformInputBridge::ProcessEvent(buttonEvent(false, 100.0f, 100.0f));
    TouchPanel::Update();

    PlatformInputBridge::ProcessEvent(buttonEvent(true, 300.0f, 300.0f));

    const TouchCollection touches = TouchPanel::GetState();
    ASSERT_EQ(touches.getCountProperty(), 1);
    EXPECT_EQ(touches[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_FLOAT_EQ(touches[0].getPositionProperty().X, 300.0f);
}

TEST_F(MouseTouchEmulationTest, ResetForTestsClearsTheOptIn)
{
    TouchPanel::setMouseTouchEmulationEnabledEXT(true);
    InputManager::ResetAllForTests();

    EXPECT_FALSE(TouchPanel::getMouseTouchEmulationEnabledEXT());
}
