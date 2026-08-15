// SPDX-License-Identifier: MS-PL
//
// Tasks 811-813, 815: gamepad connection/hotplug, button-flag mapping, axis normalization, and
// partial-capabilities tests.
//
// These publish whole snapshots through a canned IPlatformGamepad. That is the production seam
// GamePad reads, so the tests cover slot isolation and the platform->XNA mapping without native
// hardware or the legacy SDL event accumulator.

#include <gtest/gtest.h>

#include "CNA/Platform/CannedGamepad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    class GamePadMappingTest : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedGamepadPlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }

        void Publish(const PlayerIndex player, const CNA::Platform::GamepadSnapshot& snapshot)
        {
            platform.Canned().SetPendingSnapshot(static_cast<int>(player), snapshot);
            platform.Canned().Update();
        }
    };
}

// --- Task 811: connection / hotplug (platform slot layer) ---

TEST_F(GamePadMappingTest, ConnectionAffectsOnlyTheNamedSlot)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    Publish(PlayerIndex::Two, snapshot);

    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Three).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Four).getIsConnectedProperty());
}

TEST_F(GamePadMappingTest, AllFourSlotsConnectIndependently)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    for (const PlayerIndex p : {PlayerIndex::One, PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four})
        platform.Canned().SetPendingSnapshot(static_cast<int>(p), snapshot);
    platform.Canned().Update();

    for (const PlayerIndex p : {PlayerIndex::One, PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four})
        EXPECT_TRUE(GamePad::GetState(p).getIsConnectedProperty());
}

TEST_F(GamePadMappingTest, DisconnectThenReconnectRoundTrips)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    Publish(PlayerIndex::One, snapshot);
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    snapshot.connected = false;
    Publish(PlayerIndex::One, snapshot);
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    snapshot.connected = true;
    Publish(PlayerIndex::One, snapshot);
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
}

TEST_F(GamePadMappingTest, ConnectingBumpsPacketNumber)
{
    // Sdl3Gamepad owns change-only packet increments; this pins their public propagation.
    const int before = GamePad::GetState(PlayerIndex::One).getPacketNumberProperty();
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    snapshot.packetNumber = 1;
    Publish(PlayerIndex::One, snapshot);
    EXPECT_GT(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), before);
}

// --- Task 812: button -> XNA Buttons flag mapping ---

TEST_F(GamePadMappingTest, EveryButtonMapsToItsXnaFlag)
{
    struct Case { CNA::Platform::GamepadButton in; Buttons out; const char* name; };
    const Case cases[] = {
        {CNA::Platform::GamepadButton::A,             Buttons::A,             "A"},
        {CNA::Platform::GamepadButton::B,             Buttons::B,             "B"},
        {CNA::Platform::GamepadButton::X,             Buttons::X,             "X"},
        {CNA::Platform::GamepadButton::Y,             Buttons::Y,             "Y"},
        {CNA::Platform::GamepadButton::Back,          Buttons::Back,          "Back"},
        {CNA::Platform::GamepadButton::Start,         Buttons::Start,         "Start"},
        {CNA::Platform::GamepadButton::BigButton,     Buttons::BigButton,     "BigButton/Guide"},
        {CNA::Platform::GamepadButton::LeftShoulder,  Buttons::LeftShoulder,  "LeftShoulder"},
        {CNA::Platform::GamepadButton::RightShoulder, Buttons::RightShoulder, "RightShoulder"},
        {CNA::Platform::GamepadButton::LeftStick,     Buttons::LeftStick,     "LeftStick"},
        {CNA::Platform::GamepadButton::RightStick,    Buttons::RightStick,    "RightStick"},
        {CNA::Platform::GamepadButton::DPadUp,        Buttons::DPadUp,        "DPadUp"},
        {CNA::Platform::GamepadButton::DPadDown,      Buttons::DPadDown,      "DPadDown"},
        {CNA::Platform::GamepadButton::DPadLeft,      Buttons::DPadLeft,      "DPadLeft"},
        {CNA::Platform::GamepadButton::DPadRight,     Buttons::DPadRight,     "DPadRight"},
        {CNA::Platform::GamepadButton::Misc1,         Buttons::Misc1EXT,      "Misc1EXT"},
        {CNA::Platform::GamepadButton::Paddle1,       Buttons::Paddle1EXT,    "Paddle1EXT"},
        {CNA::Platform::GamepadButton::Paddle2,       Buttons::Paddle2EXT,    "Paddle2EXT"},
        {CNA::Platform::GamepadButton::Paddle3,       Buttons::Paddle3EXT,    "Paddle3EXT"},
        {CNA::Platform::GamepadButton::Paddle4,       Buttons::Paddle4EXT,    "Paddle4EXT"},
        {CNA::Platform::GamepadButton::TouchPad,      Buttons::TouchPadEXT,   "TouchPadEXT"},
    };

    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;

    for (const Case& c : cases)
    {
        snapshot.buttons = static_cast<std::uint32_t>(c.in);
        Publish(PlayerIndex::One, snapshot);
        EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonDown(c.out)) << c.name;

        snapshot.buttons = 0;
        Publish(PlayerIndex::One, snapshot);
        EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonUp(c.out)) << c.name;
    }
}

TEST_F(GamePadMappingTest, TwoButtonsPressedTogetherBothRegister)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    snapshot.buttons = static_cast<std::uint32_t>(CNA::Platform::GamepadButton::A)
        | static_cast<std::uint32_t>(CNA::Platform::GamepadButton::Start);
    Publish(PlayerIndex::One, snapshot);

    const GamePadState state = GamePad::GetState(PlayerIndex::One);
    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
    EXPECT_TRUE(state.IsButtonDown(Buttons::Start));
    EXPECT_TRUE(state.IsButtonUp(Buttons::B));
}

// --- Task 813: axis normalization / public-state clamp ---

TEST_F(GamePadMappingTest, ThumbstickAxesClampToSignedUnitRange)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftThumbstickX)] = 2.0f;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftThumbstickY)] = -2.0f;
    Publish(PlayerIndex::One, snapshot);

    const auto sticks = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
    EXPECT_FLOAT_EQ(sticks.getLeftProperty().X, 1.0f);
    EXPECT_FLOAT_EQ(sticks.getLeftProperty().Y, -1.0f);
}

TEST_F(GamePadMappingTest, RightThumbstickStoresMidAndZeroValues)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::RightThumbstickX)] = 0.5f;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::RightThumbstickY)] = 0.0f;
    Publish(PlayerIndex::One, snapshot);

    const auto sticks = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
    EXPECT_FLOAT_EQ(sticks.getRightProperty().X, 0.5f);
    EXPECT_FLOAT_EQ(sticks.getRightProperty().Y, 0.0f);
}

TEST_F(GamePadMappingTest, TriggerAxesClampToPositiveUnitRange)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftTrigger)] = 1.5f;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::RightTrigger)] = -0.5f;
    Publish(PlayerIndex::One, snapshot);

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
