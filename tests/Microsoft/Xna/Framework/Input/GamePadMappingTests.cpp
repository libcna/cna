// SPDX-License-Identifier: MS-PL
//
// Tasks 811-813, 815: gamepad connection/hotplug, button-flag mapping, axis normalization, and
// partial-capabilities tests.
//
// These drive the InputManager state-translation layer directly
// (InputManager::SetGamePad{Connection,ButtonState,AxisValue} + GamePad::GetState) — which is how
// CNA isolates gamepad state translation for headless testing without real SDL hardware (task
// 810). The SDL-device-bound paths (SDL_OpenGamepad slot assignment during hotplug, GUID / caps /
// sensor reads) return the disconnected fallback in this headless binary — covered by
// GamePadTests.cpp — and are otherwise hardware/manual-verified (plan_input.md tasks 810/811/816
// and the demo checklist, task 836).
//
// Not unit-testable headless: the SDL raw-int16 -> normalized-float axis conversion and the stick
// Y-axis negation both live in SdlInputBridge's SDL_EVENT_GAMEPAD_AXIS_MOTION handler, which only
// runs for an opened SDL_Gamepad. The axis tests below exercise the InputManager storage/clamp
// stage that the bridge feeds.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"

using CNA::Internal::Input::GamePadAxis;
using CNA::Internal::Input::GamePadButton;
using CNA::Internal::Input::InputManager;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    class GamePadMappingTest : public ::testing::Test
    {
    protected:
        void SetUp() override { InputManager::ResetForTests(); }
        void TearDown() override { InputManager::ResetForTests(); }
    };
}

// --- Task 811: connection / hotplug (InputManager slot layer) ---

TEST_F(GamePadMappingTest, ConnectionAffectsOnlyTheNamedSlot)
{
    InputManager::SetGamePadConnection(PlayerIndex::Two, true);

    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Three).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Four).getIsConnectedProperty());
}

TEST_F(GamePadMappingTest, AllFourSlotsConnectIndependently)
{
    for (const PlayerIndex p : {PlayerIndex::One, PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four})
        InputManager::SetGamePadConnection(p, true);

    for (const PlayerIndex p : {PlayerIndex::One, PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four})
        EXPECT_TRUE(GamePad::GetState(p).getIsConnectedProperty());
}

TEST_F(GamePadMappingTest, DisconnectThenReconnectRoundTrips)
{
    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    InputManager::SetGamePadConnection(PlayerIndex::One, false);
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
}

TEST_F(GamePadMappingTest, ConnectingBumpsPacketNumber)
{
    // (PacketNumber's change-only semantics across button/axis are covered separately by
    // InputManagerTests' PacketNumberBumpsOnConnectButtonAndAxisChangesOnly, task 729.)
    const int before = GamePad::GetState(PlayerIndex::One).getPacketNumberProperty();
    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    EXPECT_GT(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), before);
}

// --- Task 812: button -> XNA Buttons flag mapping ---

TEST_F(GamePadMappingTest, EveryButtonMapsToItsXnaFlag)
{
    struct Case { GamePadButton in; Buttons out; const char* name; };
    const Case cases[] = {
        {GamePadButton::A,             Buttons::A,             "A"},
        {GamePadButton::B,             Buttons::B,             "B"},
        {GamePadButton::X,             Buttons::X,             "X"},
        {GamePadButton::Y,             Buttons::Y,             "Y"},
        {GamePadButton::Back,          Buttons::Back,          "Back"},
        {GamePadButton::Start,         Buttons::Start,         "Start"},
        {GamePadButton::BigButton,     Buttons::BigButton,     "BigButton/Guide"},
        {GamePadButton::LeftShoulder,  Buttons::LeftShoulder,  "LeftShoulder"},
        {GamePadButton::RightShoulder, Buttons::RightShoulder, "RightShoulder"},
        {GamePadButton::LeftStick,     Buttons::LeftStick,     "LeftStick"},
        {GamePadButton::RightStick,    Buttons::RightStick,    "RightStick"},
        {GamePadButton::DPadUp,        Buttons::DPadUp,        "DPadUp"},
        {GamePadButton::DPadDown,      Buttons::DPadDown,      "DPadDown"},
        {GamePadButton::DPadLeft,      Buttons::DPadLeft,      "DPadLeft"},
        {GamePadButton::DPadRight,     Buttons::DPadRight,     "DPadRight"},
        {GamePadButton::Misc1EXT,      Buttons::Misc1EXT,      "Misc1EXT"},
        {GamePadButton::Paddle1EXT,    Buttons::Paddle1EXT,    "Paddle1EXT"},
        {GamePadButton::Paddle2EXT,    Buttons::Paddle2EXT,    "Paddle2EXT"},
        {GamePadButton::Paddle3EXT,    Buttons::Paddle3EXT,    "Paddle3EXT"},
        {GamePadButton::Paddle4EXT,    Buttons::Paddle4EXT,    "Paddle4EXT"},
        {GamePadButton::TouchPadEXT,   Buttons::TouchPadEXT,   "TouchPadEXT"},
    };

    InputManager::SetGamePadConnection(PlayerIndex::One, true);

    for (const Case& c : cases)
    {
        InputManager::SetGamePadButtonState(PlayerIndex::One, c.in, ButtonState::Pressed);
        EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonDown(c.out)) << c.name;

        InputManager::SetGamePadButtonState(PlayerIndex::One, c.in, ButtonState::Released);
        EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonUp(c.out)) << c.name;
    }
}

TEST_F(GamePadMappingTest, TwoButtonsPressedTogetherBothRegister)
{
    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    InputManager::SetGamePadButtonState(PlayerIndex::One, GamePadButton::A, ButtonState::Pressed);
    InputManager::SetGamePadButtonState(PlayerIndex::One, GamePadButton::Start, ButtonState::Pressed);

    const GamePadState state = GamePad::GetState(PlayerIndex::One);
    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
    EXPECT_TRUE(state.IsButtonDown(Buttons::Start));
    EXPECT_TRUE(state.IsButtonUp(Buttons::B));
}

// --- Task 813: axis normalization / clamp (InputManager storage stage) ---

TEST_F(GamePadMappingTest, ThumbstickAxesClampToSignedUnitRange)
{
    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::LeftThumbstickX, 2.0f);
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::LeftThumbstickY, -2.0f);

    const auto sticks = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
    EXPECT_FLOAT_EQ(sticks.getLeftProperty().X, 1.0f);
    EXPECT_FLOAT_EQ(sticks.getLeftProperty().Y, -1.0f);
}

TEST_F(GamePadMappingTest, RightThumbstickStoresMidAndZeroValues)
{
    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::RightThumbstickX, 0.5f);
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::RightThumbstickY, 0.0f);

    const auto sticks = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
    EXPECT_FLOAT_EQ(sticks.getRightProperty().X, 0.5f);
    EXPECT_FLOAT_EQ(sticks.getRightProperty().Y, 0.0f);
}

TEST_F(GamePadMappingTest, TriggerAxesClampToPositiveUnitRange)
{
    InputManager::SetGamePadConnection(PlayerIndex::One, true);
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::LeftTrigger, 1.5f);   // over max
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::RightTrigger, -0.5f); // below min

    const auto triggers = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getTriggersProperty();
    EXPECT_FLOAT_EQ(triggers.getLeftProperty(), 1.0f);
    EXPECT_FLOAT_EQ(triggers.getRightProperty(), 0.0f);
}

// --- Task 815: GamePadCapabilities with partial capabilities ---

TEST(GamePadCapabilitiesTest, PartialCapabilitiesLeaveUnsetFlagsFalse)
{
    // A controller that reports only some capabilities (e.g. an arcade stick: buttons but no
    // sticks/triggers/rumble) must leave every unset flag false and every set flag true.
    GamePadCapabilities caps;
    caps.setIsConnectedProperty(true);
    caps.setHasAButtonProperty(true);
    caps.setHasBButtonProperty(true);
    caps.setHasLeftTriggerProperty(true);
    caps.setHasGyroEXTProperty(true);
    caps.setGamePadTypeProperty(GamePadType::ArcadeStick);

    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_TRUE(caps.getHasAButtonProperty());
    EXPECT_TRUE(caps.getHasBButtonProperty());
    EXPECT_TRUE(caps.getHasLeftTriggerProperty());
    EXPECT_TRUE(caps.getHasGyroEXTProperty());
    EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::ArcadeStick);

    // Unset flags stay false — no setter bleeds into a neighbour.
    EXPECT_FALSE(caps.getHasXButtonProperty());
    EXPECT_FALSE(caps.getHasRightTriggerProperty());
    EXPECT_FALSE(caps.getHasLeftXThumbStickProperty());
    EXPECT_FALSE(caps.getHasLeftVibrationMotorProperty());
    EXPECT_FALSE(caps.getHasRightVibrationMotorProperty());
    EXPECT_FALSE(caps.getHasAccelerometerEXTProperty());
    EXPECT_FALSE(caps.getHasLightBarEXTProperty());
    EXPECT_FALSE(caps.getHasTouchPadEXTProperty());
}
