// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    void ResetGamePadState()
    {
        CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, false);
        CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::Two, false);
        CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::Three, false);
        CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::Four, false);
    }
}

TEST(GamePadInputTest, GetStateReturnsDisconnectedWhenNoGamePadConnected)
{
    ResetGamePadState();

    const auto state = GamePad::GetState(PlayerIndex::One);

    EXPECT_FALSE(state.getIsConnectedProperty());
    EXPECT_EQ(state.getButtonsProperty().getAProperty(), ButtonState::Released);
    EXPECT_EQ(state.getButtonsProperty().getStartProperty(), ButtonState::Released);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 0.0f);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getRightProperty(), 0.0f);

    ResetGamePadState();
}

TEST(GamePadInputTest, GetStateReflectsMappedButtonsAndAxes)
{
    ResetGamePadState();

    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One, CNA::Internal::Input::GamePadButton::A,
                                                              ButtonState::Pressed);
    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One,
                                                              CNA::Internal::Input::GamePadButton::Back,
                                                              ButtonState::Pressed);
    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One,
                                                              CNA::Internal::Input::GamePadButton::DPadLeft,
                                                              ButtonState::Pressed);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::LeftThumbstickX, 0.25f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::LeftThumbstickY, -0.75f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::RightThumbstickX, -1.0f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::RightThumbstickY, 1.0f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::LeftTrigger, 0.2f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::RightTrigger, 1.0f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None);

    EXPECT_TRUE(state.getIsConnectedProperty());
    EXPECT_EQ(state.getButtonsProperty().getAProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getButtonsProperty().getBackProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getDPadProperty().getLeftProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getButtonsProperty().getBProperty(), ButtonState::Released);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().X, 0.25f);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().Y, -0.75f);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getRightProperty().X, -1.0f);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getRightProperty().Y, 1.0f);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 0.2f);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getRightProperty(), 1.0f);

    ResetGamePadState();
}

TEST(GamePadInputTest, SnapshotDoesNotChangeAfterInternalStateMutation)
{
    ResetGamePadState();

    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One, CNA::Internal::Input::GamePadButton::A,
                                                              ButtonState::Pressed);
    const auto snapshot = GamePad::GetState(PlayerIndex::One);

    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One, CNA::Internal::Input::GamePadButton::A,
                                                              ButtonState::Released);
    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One, CNA::Internal::Input::GamePadButton::B,
                                                              ButtonState::Pressed);

    EXPECT_EQ(snapshot.getButtonsProperty().getAProperty(), ButtonState::Pressed);
    EXPECT_EQ(snapshot.getButtonsProperty().getBProperty(), ButtonState::Released);

    const auto currentState = GamePad::GetState(PlayerIndex::One);
    EXPECT_EQ(currentState.getButtonsProperty().getAProperty(), ButtonState::Released);
    EXPECT_EQ(currentState.getButtonsProperty().getBProperty(), ButtonState::Pressed);

    ResetGamePadState();
}

TEST(GamePadInputTest, AxisValuesAreClampedAndInvalidPlayerReturnsDisconnectedState)
{
    ResetGamePadState();

    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::LeftThumbstickX, 3.5f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::RightThumbstickY, -2.0f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::LeftTrigger, -0.5f);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::RightTrigger, 4.0f);

    const auto clampedState = GamePad::GetState(PlayerIndex::One);
    EXPECT_FLOAT_EQ(clampedState.getThumbSticksProperty().getLeftProperty().X, 1.0f);
    EXPECT_FLOAT_EQ(clampedState.getThumbSticksProperty().getRightProperty().Y, -1.0f);
    EXPECT_FLOAT_EQ(clampedState.getTriggersProperty().getLeftProperty(), 0.0f);
    EXPECT_FLOAT_EQ(clampedState.getTriggersProperty().getRightProperty(), 1.0f);

    const auto invalidState = GamePad::GetState(static_cast<PlayerIndex>(99));
    EXPECT_FALSE(invalidState.getIsConnectedProperty());

    ResetGamePadState();
}

TEST(GamePadInputTest, PacketNumberBumpsOnConnectButtonAndAxisChangesOnly)
{
    ResetGamePadState();

    const auto disconnected = GamePad::GetState(PlayerIndex::One);
    EXPECT_EQ(disconnected.getPacketNumberProperty(), 0);

    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    const auto afterConnect = GamePad::GetState(PlayerIndex::One);
    const int packetAfterConnect = afterConnect.getPacketNumberProperty();
    EXPECT_GT(packetAfterConnect, 0);

    const auto unchanged = GamePad::GetState(PlayerIndex::One);
    EXPECT_EQ(unchanged.getPacketNumberProperty(), packetAfterConnect);

    CNA::Internal::Input::InputManager::SetGamePadButtonState(PlayerIndex::One, CNA::Internal::Input::GamePadButton::A,
                                                              ButtonState::Pressed);
    const auto afterButton = GamePad::GetState(PlayerIndex::One);
    const int packetAfterButton = afterButton.getPacketNumberProperty();
    EXPECT_GT(packetAfterButton, packetAfterConnect);

    CNA::Internal::Input::InputManager::SetGamePadAxisValue(PlayerIndex::One,
                                                            CNA::Internal::Input::GamePadAxis::LeftThumbstickX, 0.5f);
    const auto afterAxis = GamePad::GetState(PlayerIndex::One);
    EXPECT_GT(afterAxis.getPacketNumberProperty(), packetAfterButton);

    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, false);
    const auto afterDisconnect = GamePad::GetState(PlayerIndex::One);
    EXPECT_EQ(afterDisconnect.getPacketNumberProperty(), 0);

    ResetGamePadState();
}

// DEC-04 / task 916. CNA synthesizes GamePadState::PacketNumber (FNA leaves it hardcoded to 0); it
// increments on any RAW axis change, dead-zone-independent, matching XInput's dwPacketNumber. So a
// wobble that stays entirely within the dead zone still bumps PacketNumber, even though the
// default-dead-zoned thumbstick reads at rest both times. This pins that by-design behavior: the raw
// input changed, so PacketNumber reflects it; the dead zone is a GetState-time projection, not stored.
TEST(GamePadInputTest, PacketNumberBumpsOnWithinDeadZoneAxisWobbleWhileDeadZonedStateStaysAtRest)
{
    using CNA::Internal::Input::InputManager;
    using CNA::Internal::Input::GamePadAxis;

    ResetGamePadState();
    InputManager::SetGamePadConnection(PlayerIndex::One, true);

    // Left-stick dead zone is ~0.2395 (GamePad::LeftDeadZone = 7849/32768); both values are below it.
    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::LeftThumbstickX, 0.05f);
    const auto first = GamePad::GetState(PlayerIndex::One); // default = IndependentAxes dead zone
    EXPECT_FLOAT_EQ(first.getThumbSticksProperty().getLeftProperty().X, 0.0f);
    const int packetFirst = first.getPacketNumberProperty();

    InputManager::SetGamePadAxisValue(PlayerIndex::One, GamePadAxis::LeftThumbstickX, 0.15f);
    const auto second = GamePad::GetState(PlayerIndex::One);
    EXPECT_FLOAT_EQ(second.getThumbSticksProperty().getLeftProperty().X, 0.0f); // still at rest (dead-zoned)

    // The dead-zoned view is unchanged, but the raw axis changed, so PacketNumber bumped.
    EXPECT_GT(second.getPacketNumberProperty(), packetFirst);

    // Confirm the raw value actually changed (what drove the bump): with no dead zone it reads 0.15.
    const auto raw = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None);
    EXPECT_FLOAT_EQ(raw.getThumbSticksProperty().getLeftProperty().X, 0.15f);

    ResetGamePadState();
}
