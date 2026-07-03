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
                          const float dx = 0.0f, const float dy = 0.0f)
    {
        SDL_Event e{};
        e.type = type;
        e.tfinger.fingerID = fingerId;
        e.tfinger.x = x;
        e.tfinger.y = y;
        e.tfinger.dx = dx;
        e.tfinger.dy = dy;
        return e;
    }

    void DrainGestures()
    {
        while (TouchPanel::getIsGestureAvailableProperty())
        {
            (void)TouchPanel::ReadGesture();
        }
    }

    // GestureDetector's state machine lives in file-static variables with no reset hook (same
    // caveat as GestureDetectorTests.cpp). A neutral press/release cycle with all gestures
    // disabled reliably drives it back to idle between tests.
    void ResetGestureDetector()
    {
        TouchPanel::setEnabledGesturesProperty(GestureType::None);
        TouchPanel::INTERNAL_onTouchEvent(900101, TouchLocationState::Pressed, 0.0f, 0.0f, 0.0f, 0.0f);
        TouchPanel::INTERNAL_onTouchEvent(900101, TouchLocationState::Released, 0.0f, 0.0f, 0.0f, 0.0f);
        DrainGestures();
    }

    // InputManager's touch map is also process-wide static (matches TouchInputTests.cpp's own
    // ResetTouchState helper); a Released touch is only actually removed on its second
    // GetTouchState() read (RemoveAfterSnapshot), so flush any leftovers between tests here too.
    void ResetTouchState()
    {
        const auto currentSnapshot = InputManager::GetTouchState();
        for (const auto& touchLocation : currentSnapshot)
        {
            InputManager::SetTouchState(
                touchLocation.getIdProperty(),
                TouchLocationState::Released,
                touchLocation.getPositionProperty()
            );
        }
        (void)TouchPanel::GetState();
        (void)TouchPanel::GetState();
    }

    struct SdlInputBridgeTouchGestureTest : ::testing::Test
    {
        void SetUp() override
        {
            TouchPanel::setDisplayWidthProperty(DisplaySize);
            TouchPanel::setDisplayHeightProperty(DisplaySize);
            ResetGestureDetector();
            ResetTouchState();
        }

        void TearDown() override
        {
            ResetGestureDetector();
            ResetTouchState();
        }
    };
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
