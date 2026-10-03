// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/CannedGamepad.hpp"
#include "CNA/Platform/Input/KeyboardGamepad.hpp"
#include "CNA/Internal/Input/SystemInput.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
using CNA::Platform::KeyCode;
using CNA::Platform::KeyboardGamepad;
namespace
{
    class KeyboardGamepadTest : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedGamepadPlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;
        void SetUp() override
        {
            EXPECT_FALSE(GamePad::getKeyboardEmulationEnabledEXT());
            installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }
        void TearDown() override
        {
            CNA::Internal::Input::setSystemOwnsInput(false);
            GamePad::setKeyboardEmulationEnabledEXT(false);
            (void)GamePad::GetState(PlayerIndex::One);
        }
        void Press(std::initializer_list<KeyCode> keys, bool focused = true)
        {
            KeyboardGamepad::Update({keys, 0}, focused);
        }
        GamePadState Read() { return GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None); }
        void Physical(CNA::Platform::GamepadSnapshot state, CNA::Platform::GamepadCapabilities caps = {})
        {
            platform.Canned().SetPendingSnapshot(0, state);
            platform.Canned().SetCapabilities(0, caps);
            platform.Canned().Update();
        }
    };

    class KeyboardGamepadButtonTest : public KeyboardGamepadTest,
                                     public ::testing::WithParamInterface<std::pair<KeyCode, Buttons>> {};
    class KeyboardGamepadAxisTest : public KeyboardGamepadTest,
                                   public ::testing::WithParamInterface<std::pair<KeyCode, std::size_t>> {};
}

TEST_F(KeyboardGamepadTest, DisabledLeavesPhysicalStateAndCapabilitiesUnchanged)
{
    Press({KeyCode::K, KeyCode::W});
    EXPECT_FALSE(Read().getIsConnectedProperty());
    CNA::Platform::GamepadSnapshot raw;
    raw.connected = true; raw.buttons = static_cast<std::uint32_t>(Buttons::B); raw.packetNumber = 19;
    raw.axes[0] = 0.4f;
    CNA::Platform::GamepadCapabilities caps;
    caps.connected = true; caps.kind = CNA::Platform::GamepadKind::Wheel;
    Physical(raw, caps);
    const auto state = Read();
    EXPECT_TRUE(state.IsButtonDown(Buttons::B));
    EXPECT_TRUE(state.IsButtonUp(Buttons::A));
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().X, 0.4f);
    EXPECT_EQ(state.getPacketNumberProperty(), 19);
    EXPECT_EQ(GamePad::GetCapabilities(PlayerIndex::One).getGamePadTypeProperty(), GamePadType::Wheel);
    EXPECT_EQ(KeyboardGamepad::MergeSnapshot(raw).packetNumber, 19u);
    EXPECT_EQ(KeyboardGamepad::MergeCapabilities(caps).kind, caps.kind);
    EXPECT_FALSE(KeyboardGamepad::GetSnapshot().connected);
}

TEST_F(KeyboardGamepadTest, OnlyPlayerOneConnectsIncludingWithoutAPlatformGamepadService)
{
    platform.SetGamepadAvailable(false);
    GamePad::setKeyboardEmulationEnabledEXT(true);
    EXPECT_TRUE(GamePad::getKeyboardEmulationEnabledEXT());
    EXPECT_TRUE(KeyboardGamepad::IsEnabled());
    EXPECT_TRUE(Read().getIsConnectedProperty());
    Press({KeyCode::K});
    EXPECT_TRUE(Read().IsButtonDown(Buttons::A));
    for (auto player : {PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four,
                        static_cast<PlayerIndex>(-1), static_cast<PlayerIndex>(4)})
    {
        EXPECT_FALSE(GamePad::GetState(player).getIsConnectedProperty());
        EXPECT_FALSE(GamePad::GetCapabilities(player).getIsConnectedProperty());
    }
    EXPECT_FALSE(GamePad::SetVibration(PlayerIndex::One, 1, 1));
    GamePad::setKeyboardEmulationEnabledEXT(false);
    EXPECT_FALSE(Read().getIsConnectedProperty());
}

TEST_P(KeyboardGamepadButtonTest, MapsAndReleasesButton)
{
    GamePad::setKeyboardEmulationEnabledEXT(true);
    const auto [key, button] = GetParam();
    Press({key});
    EXPECT_TRUE(Read().IsButtonDown(button));
    EXPECT_EQ(KeyboardGamepad::GetSnapshot().buttons, static_cast<std::uint32_t>(button));
    Press({});
    EXPECT_TRUE(Read().IsButtonUp(button));
}
INSTANTIATE_TEST_SUITE_P(Controls, KeyboardGamepadButtonTest, ::testing::Values(
    std::pair{KeyCode::K, Buttons::A}, std::pair{KeyCode::L, Buttons::B},
    std::pair{KeyCode::J, Buttons::X}, std::pair{KeyCode::I, Buttons::Y},
    std::pair{KeyCode::Q, Buttons::LeftShoulder}, std::pair{KeyCode::E, Buttons::RightShoulder},
    std::pair{KeyCode::LeftShift, Buttons::LeftStick}, std::pair{KeyCode::RightShift, Buttons::RightStick},
    std::pair{KeyCode::Enter, Buttons::Start}, std::pair{KeyCode::Escape, Buttons::Back},
    std::pair{KeyCode::T, Buttons::DPadUp}, std::pair{KeyCode::F, Buttons::DPadLeft},
    std::pair{KeyCode::G, Buttons::DPadDown}, std::pair{KeyCode::H, Buttons::DPadRight}));

TEST_P(KeyboardGamepadAxisTest, MapsSignedAxesAndTriggers)
{
    GamePad::setKeyboardEmulationEnabledEXT(true);
    const auto [key, axis] = GetParam();
    Press({key});
    const float expected = key == KeyCode::A || key == KeyCode::S || key == KeyCode::Left || key == KeyCode::Down ? -1 : 1;
    const auto raw = KeyboardGamepad::GetSnapshot();
    for (std::size_t i = 0; i < raw.axes.size(); ++i)
        EXPECT_FLOAT_EQ(raw.axes[i], i == axis ? expected : 0);
    const auto state = Read();
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().X, raw.axes[0]);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().Y, raw.axes[1]);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getRightProperty().X, raw.axes[2]);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getRightProperty().Y, raw.axes[3]);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), raw.axes[4]);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getRightProperty(), raw.axes[5]);
}
INSTANTIATE_TEST_SUITE_P(Controls, KeyboardGamepadAxisTest, ::testing::Values(
    std::pair{KeyCode::A, std::size_t{0}}, std::pair{KeyCode::D, std::size_t{0}},
    std::pair{KeyCode::S, std::size_t{1}}, std::pair{KeyCode::W, std::size_t{1}},
    std::pair{KeyCode::Left, std::size_t{2}}, std::pair{KeyCode::Right, std::size_t{2}},
    std::pair{KeyCode::Down, std::size_t{3}}, std::pair{KeyCode::Up, std::size_t{3}},
    std::pair{KeyCode::Z, std::size_t{4}}, std::pair{KeyCode::C, std::size_t{5}}));

TEST_F(KeyboardGamepadTest, DiagonalsNormalizeOppositeKeysCancelAndDeadZonesStillApply)
{
    GamePad::setKeyboardEmulationEnabledEXT(true);
    Press({KeyCode::W, KeyCode::D, KeyCode::Up, KeyCode::Left});
    auto state = Read();
    EXPECT_NEAR(state.getThumbSticksProperty().getLeftProperty().Length(), 1, 0.00001f);
    EXPECT_NEAR(state.getThumbSticksProperty().getRightProperty().Length(), 1, 0.00001f);
    EXPECT_GT(state.getThumbSticksProperty().getLeftProperty().Y, 0);
    EXPECT_LT(state.getThumbSticksProperty().getRightProperty().X, 0);
    EXPECT_LT(GamePad::GetState(PlayerIndex::One).getThumbSticksProperty().getLeftProperty().X,
              state.getThumbSticksProperty().getLeftProperty().X);
    EXPECT_NEAR(GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular)
                    .getThumbSticksProperty().getLeftProperty().Length(), 1, 0.00001f);
    Press({KeyCode::W, KeyCode::S, KeyCode::A, KeyCode::D, KeyCode::Up, KeyCode::Down, KeyCode::Left, KeyCode::Right});
    EXPECT_EQ(Read().getThumbSticksProperty().getLeftProperty(), Vector2::Zero);
    EXPECT_EQ(Read().getThumbSticksProperty().getRightProperty(), Vector2::Zero);
}

TEST_F(KeyboardGamepadTest, FocusLossAndModeChangesClearInputButRepeatedEnableDoesNot)
{
    GamePad::setKeyboardEmulationEnabledEXT(true);
    Press({KeyCode::K, KeyCode::W, KeyCode::C});
    GamePad::setKeyboardEmulationEnabledEXT(true);
    EXPECT_TRUE(Read().IsButtonDown(Buttons::A));
    Press({KeyCode::K, KeyCode::W, KeyCode::C}, false);
    EXPECT_TRUE(Read().IsButtonUp(Buttons::A));
    EXPECT_EQ(Read().getThumbSticksProperty().getLeftProperty(), Vector2::Zero);
    EXPECT_FLOAT_EQ(Read().getTriggersProperty().getRightProperty(), 0);
    EXPECT_EQ(KeyboardGamepad::GetHeldButtons(), static_cast<std::uint32_t>(Buttons::A));
    Press({KeyCode::K});
    GamePad::setKeyboardEmulationEnabledEXT(false);
    GamePad::setKeyboardEmulationEnabledEXT(true);
    EXPECT_TRUE(Read().IsButtonUp(Buttons::A));
    EXPECT_EQ(KeyboardGamepad::GetHeldButtons(), 0u);
}

TEST_F(KeyboardGamepadTest, MergesPhysicalButtonsAndUsesKeyboardOnlyForNonzeroAxes)
{
    CNA::Platform::GamepadSnapshot raw;
    raw.connected = true; raw.buttons = static_cast<std::uint32_t>(Buttons::B);
    raw.axes = {0.3f, -0.4f, 0.6f, 0.2f, 0.4f, 0.7f};
    Physical(raw);
    GamePad::setKeyboardEmulationEnabledEXT(true);
    Press({KeyCode::K, KeyCode::W, KeyCode::Left, KeyCode::Z});
    auto state = Read();
    EXPECT_TRUE(state.IsButtonDown(Buttons::A | Buttons::B));
    EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2(0.3f, 1));
    EXPECT_EQ(state.getThumbSticksProperty().getRightProperty(), Vector2(-1, 0.2f));
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 1);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getRightProperty(), 0.7f);
    Press({});
    state = Read();
    EXPECT_TRUE(state.IsButtonDown(Buttons::B));
    EXPECT_TRUE(state.IsButtonUp(Buttons::A));
    EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2(0.3f, -0.4f));
}

TEST_F(KeyboardGamepadTest, PacketNumberChangesOnlyWithEffectiveState)
{
    GamePad::setKeyboardEmulationEnabledEXT(true);
    const auto initial = Read().getPacketNumberProperty();
    EXPECT_EQ(Read().getPacketNumberProperty(), initial);
    Press({KeyCode::K});
    const auto changed = Read().getPacketNumberProperty();
    EXPECT_NE(changed, initial);
    Press({KeyCode::K, KeyCode::P});
    EXPECT_EQ(Read().getPacketNumberProperty(), changed);
    Press({KeyCode::D});
    const auto stick = Read().getPacketNumberProperty();
    CNA::Platform::GamepadSnapshot raw; raw.connected = true; raw.axes[0] = 0.2f; raw.packetNumber = 50;
    Physical(raw);
    EXPECT_EQ(Read().getPacketNumberProperty(), stick);
    raw.axes[0] = 0.5f; raw.packetNumber = 51;
    Physical(raw);
    EXPECT_EQ(Read().getPacketNumberProperty(), stick);
    raw.axes[1] = 0.5f;
    Physical(raw);
    EXPECT_NE(Read().getPacketNumberProperty(), stick);
    Press({});
    EXPECT_NE(Read().getPacketNumberProperty(), stick);
}

TEST_F(KeyboardGamepadTest, GuideReadsOnlyPhysicalInputAndDismissalSuppressesHeldSoftwareButtons)
{
    GamePad::setKeyboardEmulationEnabledEXT(true);
    Press({KeyCode::Escape, KeyCode::W, KeyCode::C});
    CNA::Internal::Input::setSystemOwnsInput(true);
    Press({KeyCode::Escape, KeyCode::W, KeyCode::C}, false);
    EXPECT_TRUE(Read().getIsConnectedProperty());
    EXPECT_TRUE(Read().IsButtonUp(Buttons::Back));
    EXPECT_EQ(Read().getThumbSticksProperty().getLeftProperty(), Vector2::Zero);
    EXPECT_FALSE(CNA::Internal::Input::systemGamePadState(PlayerIndex::One).getIsConnectedProperty());
    CNA::Internal::Input::setSystemOwnsInput(false);
    Press({KeyCode::Escape, KeyCode::K});
    EXPECT_TRUE(Read().IsButtonUp(Buttons::Back));
    EXPECT_TRUE(Read().IsButtonDown(Buttons::A));
    Press({}); (void)Read();
    Press({KeyCode::Escape});
    EXPECT_TRUE(Read().IsButtonDown(Buttons::Back));
}

TEST_F(KeyboardGamepadTest, CapabilitiesAddAllMappedControlsAndPreservePhysicalExtras)
{
    CNA::Platform::GamepadCapabilities physical;
    physical.connected = true; physical.kind = CNA::Platform::GamepadKind::Wheel;
    physical.rumble = true; physical.gyroscope = true;
    physical.buttons = static_cast<std::uint32_t>(CNA::Platform::GamepadButton::BigButton);
    CNA::Platform::GamepadSnapshot raw; raw.connected = true;
    Physical(raw, physical);
    GamePad::setKeyboardEmulationEnabledEXT(true);
    auto caps = GamePad::GetCapabilities(PlayerIndex::One);
    EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::GamePad);
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_TRUE(caps.getHasAButtonProperty()); EXPECT_TRUE(caps.getHasBButtonProperty());
    EXPECT_TRUE(caps.getHasXButtonProperty()); EXPECT_TRUE(caps.getHasYButtonProperty());
    EXPECT_TRUE(caps.getHasStartButtonProperty()); EXPECT_TRUE(caps.getHasBackButtonProperty());
    EXPECT_TRUE(caps.getHasLeftShoulderButtonProperty()); EXPECT_TRUE(caps.getHasRightShoulderButtonProperty());
    EXPECT_TRUE(caps.getHasLeftStickButtonProperty()); EXPECT_TRUE(caps.getHasRightStickButtonProperty());
    EXPECT_TRUE(caps.getHasDPadUpButtonProperty()); EXPECT_TRUE(caps.getHasDPadDownButtonProperty());
    EXPECT_TRUE(caps.getHasDPadLeftButtonProperty()); EXPECT_TRUE(caps.getHasDPadRightButtonProperty());
    EXPECT_TRUE(caps.getHasLeftXThumbStickProperty()); EXPECT_TRUE(caps.getHasLeftYThumbStickProperty());
    EXPECT_TRUE(caps.getHasRightXThumbStickProperty()); EXPECT_TRUE(caps.getHasRightYThumbStickProperty());
    EXPECT_TRUE(caps.getHasLeftTriggerProperty()); EXPECT_TRUE(caps.getHasRightTriggerProperty());
    EXPECT_TRUE(caps.getHasLeftVibrationMotorProperty()); EXPECT_TRUE(caps.getHasGyroEXTProperty());
    EXPECT_TRUE(caps.getHasBigButtonProperty());
    platform.SetGamepadAvailable(false);
    caps = GamePad::GetCapabilities(PlayerIndex::One);
    EXPECT_FALSE(caps.getHasLeftVibrationMotorProperty()); EXPECT_FALSE(caps.getHasGyroEXTProperty());
    EXPECT_FALSE(caps.getHasBigButtonProperty());
}
