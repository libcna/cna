// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/GamePadButtonLabel.hpp"
#include "CNA/Input/GamePadConnectionState.hpp"
#include "CNA/Input/PowerState.hpp"
#include "CNA/Platform/CannedGamepad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"

#include <memory>

using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    class GamePadPlatformTest : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedGamepadPlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }

        void Connect(const CNA::Platform::GamepadSnapshot& snapshot,
                     const CNA::Platform::GamepadCapabilities& capabilities = {})
        {
            platform.Canned().SetPendingSnapshot(0, snapshot);
            platform.Canned().SetCapabilities(0, capabilities);
            platform.Canned().Update();
        }
    };
}

// --- GamePad::ExcludeAxisDeadZone ---

TEST(GamePadTest, ExcludeAxisDeadZoneReturnsZeroWithinDeadZone)
{
    EXPECT_FLOAT_EQ(GamePad::ExcludeAxisDeadZone(0.0f, 0.25f), 0.0f);
    EXPECT_FLOAT_EQ(GamePad::ExcludeAxisDeadZone(0.25f, 0.25f), 0.0f);
    EXPECT_FLOAT_EQ(GamePad::ExcludeAxisDeadZone(-0.25f, 0.25f), 0.0f);
}

TEST(GamePadTest, ExcludeAxisDeadZoneRescalesPositiveValueAboveDeadZone)
{
    const float result = GamePad::ExcludeAxisDeadZone(0.5f, 0.25f);
    const float expected = (0.5f - 0.25f) / (1.0f - 0.25f);
    EXPECT_FLOAT_EQ(result, expected);
}

TEST(GamePadTest, ExcludeAxisDeadZoneRescalesNegativeValueBelowNegatedDeadZone)
{
    const float result = GamePad::ExcludeAxisDeadZone(-0.5f, 0.25f);
    const float expected = (-0.5f + 0.25f) / (1.0f - 0.25f);
    EXPECT_FLOAT_EQ(result, expected);
}

TEST(GamePadTest, ExcludeAxisDeadZoneMapsMaxMagnitudeToMaxOutput)
{
    EXPECT_FLOAT_EQ(GamePad::ExcludeAxisDeadZone(1.0f, 0.25f), 1.0f);
    EXPECT_FLOAT_EQ(GamePad::ExcludeAxisDeadZone(-1.0f, 0.25f), -1.0f);
}

// --- GamePad: no-hardware fallback paths (no SDL gamepad is ever opened in this test binary) ---

TEST(GamePadTest, GetCapabilitiesReturnsDisconnectedCapabilitiesWhenNoGamePadConnected)
{
    const GamePadCapabilities caps = GamePad::GetCapabilities(PlayerIndex::One);

    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_FALSE(caps.getHasAButtonProperty());
    EXPECT_FALSE(caps.getHasLightBarEXTProperty());
    EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::Unknown);
}

TEST(GamePadTest, SetVibrationReturnsFalseWhenNoGamePadConnected)
{
    EXPECT_FALSE(GamePad::SetVibration(PlayerIndex::One, 0.5f, 0.5f));
}

TEST(GamePadTest, SetTriggerVibrationEXTReturnsFalseWhenNoGamePadConnected)
{
    EXPECT_FALSE(GamePad::SetTriggerVibrationEXT(PlayerIndex::One, 0.5f, 0.5f));
}

TEST(GamePadTest, SetLightBarEXTIsNoOpWhenNoGamePadConnected)
{
    EXPECT_NO_THROW(GamePad::SetLightBarEXT(PlayerIndex::One, Color::Red));
}

TEST(GamePadTest, GetGyroEXTReturnsFalseAndZeroesOutputWhenNoGamePadConnected)
{
    Vector3 gyro(1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(GamePad::GetGyroEXT(PlayerIndex::One, gyro));
    EXPECT_EQ(gyro, Vector3::Zero);
}

TEST(GamePadTest, GetAccelerometerEXTReturnsFalseAndZeroesOutputWhenNoGamePadConnected)
{
    Vector3 accel(1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(GamePad::GetAccelerometerEXT(PlayerIndex::One, accel));
    EXPECT_EQ(accel, Vector3::Zero);
}

TEST(GamePadTest, GetGUIDEXTReturnsEmptyStringWhenNoGamePadConnected)
{
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "");
}

TEST_F(GamePadPlatformTest, StateReadsOneImmutableWholePlatformSnapshot)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    snapshot.buttons = static_cast<std::uint32_t>(CNA::Platform::GamepadButton::A)
        | static_cast<std::uint32_t>(CNA::Platform::GamepadButton::DPadLeft);
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftThumbstickX)] = 0.5f;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftThumbstickY)] = -0.25f;
    snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftTrigger)] = 0.75f;
    snapshot.packetNumber = 17;
    Connect(snapshot);

    const GamePadState state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None);
    EXPECT_TRUE(state.getIsConnectedProperty());
    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
    EXPECT_EQ(state.getDPadProperty().getLeftProperty(), ButtonState::Pressed);
    // XNA ApplyStickDeadZone None IL_009d-00c8: trunc(axis*32767)/32767.
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().X, 16383.0f / 32767.0f);
    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().Y, -8191.0f / 32767.0f);
    // XNA ApplyTriggerDeadZone None IL_0003-0014: byte/255; 0.75 is byte trunc(191.25) = 191.
    EXPECT_EQ(state.getTriggersProperty().getLeftProperty(), 191.0f / 255.0f);
    EXPECT_EQ(state.getPacketNumberProperty(), 17);
}

TEST_F(GamePadPlatformTest, GuidFormattingCoversXinputHexPaddingAndValveOverrides)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    Connect(snapshot);

    CNA::Platform::GamepadInfo info;
    platform.Canned().SetInfo(0, info);
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "xinput");

    info.vendor = 0x0001;
    info.product = 0x0002;
    platform.Canned().SetInfo(0, info);
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "01000200");

    info.vendor = 0x1234;
    info.product = 0x5678;
    platform.Canned().SetInfo(0, info);
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "34127856");

    info.vendor = 0x28de;
    info.product = 1;
    info.model = CNA::Platform::GamepadModel::PlayStation5;
    platform.Canned().SetInfo(0, info);
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "4c05e60c");

    info.model = CNA::Platform::GamepadModel::XboxOne;
    platform.Canned().SetInfo(0, info);
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "xinput");
}

TEST_F(GamePadPlatformTest, CapabilitiesMapEveryCategoryWithoutNativeQueries)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    CNA::Platform::GamepadCapabilities source;
    source.connected = true;
    source.kind = CNA::Platform::GamepadKind::ArcadeStick;
    source.buttons = static_cast<std::uint32_t>(CNA::Platform::GamepadButton::A)
        | static_cast<std::uint32_t>(CNA::Platform::GamepadButton::Paddle3);
    source.axes = CNA::Platform::GamepadAxisBit(CNA::Platform::GamepadAxis::LeftThumbstickX)
        | CNA::Platform::GamepadAxisBit(CNA::Platform::GamepadAxis::RightTrigger);
    source.rumble = true;
    source.triggerRumble = true;
    source.lightBar = true;
    source.touchpad = true;
    source.gyroscope = true;
    source.accelerometer = true;
    Connect(snapshot, source);

    const GamePadCapabilities caps = GamePad::GetCapabilities(PlayerIndex::One);
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::ArcadeStick);
    EXPECT_TRUE(caps.getHasAButtonProperty());
    EXPECT_TRUE(caps.getHasPaddle3EXTProperty());
    EXPECT_FALSE(caps.getHasBButtonProperty());
    EXPECT_TRUE(caps.getHasLeftXThumbStickProperty());
    EXPECT_TRUE(caps.getHasRightTriggerProperty());
    EXPECT_FALSE(caps.getHasLeftTriggerProperty());
    EXPECT_TRUE(caps.getHasLeftVibrationMotorProperty());
    EXPECT_TRUE(caps.getHasRightVibrationMotorProperty());
    EXPECT_TRUE(caps.getHasTriggerVibrationMotorsEXTProperty());
    EXPECT_TRUE(caps.getHasLightBarEXTProperty());
    EXPECT_TRUE(caps.getHasTouchPadEXTProperty());
    EXPECT_TRUE(caps.getHasGyroEXTProperty());
    EXPECT_TRUE(caps.getHasAccelerometerEXTProperty());
}

TEST_F(GamePadPlatformTest, ActuatorCommandsReachTheNamedPlatformSlot)
{
    platform.Canned().SetRumbleResult(true);
    platform.Canned().SetTriggerRumbleResult(true);
    platform.Canned().SetLightBarResult(true);

    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::Three, 0.25f, 0.75f));
    EXPECT_EQ(platform.Canned().LastSlot(), 2);
    EXPECT_FLOAT_EQ(platform.Canned().LastLeft(), 0.25f);
    EXPECT_FLOAT_EQ(platform.Canned().LastRight(), 0.75f);
    EXPECT_EQ(platform.Canned().LastDuration(), 0u);

    EXPECT_TRUE(GamePad::SetTriggerVibrationEXT(PlayerIndex::Two, 0.4f, 0.6f));
    EXPECT_EQ(platform.Canned().LastSlot(), 1);
    EXPECT_EQ(platform.Canned().TriggerRumbleCalls(), 1);

    GamePad::SetLightBarEXT(PlayerIndex::Four, Color(10, 20, 30));
    EXPECT_EQ(platform.Canned().LastSlot(), 3);
    EXPECT_EQ(platform.Canned().LastRed(), 10);
    EXPECT_EQ(platform.Canned().LastGreen(), 20);
    EXPECT_EQ(platform.Canned().LastBlue(), 30);
}

TEST_F(GamePadPlatformTest, IdentityPowerConnectionAndLabelsUsePlatformVocabulary)
{
    CNA::Platform::GamepadSnapshot snapshot;
    snapshot.connected = true;
    Connect(snapshot);
    CNA::Platform::GamepadInfo info;
    info.name = "Canned Pad";
    info.path = "/dev/canned";
    info.serial = "ABC123";
    info.vendor = 0x054c;
    info.product = 0x0ce6;
    info.firmwareVersion = 42;
    info.steamHandle = 99;
    info.connectionState = CNA::Platform::GamepadConnectionState::Wireless;
    platform.Canned().SetInfo(0, info);
    platform.Canned().SetPowerInfo(0, {CNA::Platform::GamepadPowerState::Charging, 64});
    platform.Canned().SetButtonLabel(0, CNA::Platform::GamepadButtonLabel::Cross);

    EXPECT_EQ(GamePad::GetNameEXT(PlayerIndex::One), "Canned Pad");
    EXPECT_EQ(GamePad::GetPathEXT(PlayerIndex::One), "/dev/canned");
    EXPECT_EQ(GamePad::GetSerialEXT(PlayerIndex::One), "ABC123");
    EXPECT_EQ(GamePad::GetGUIDEXT(PlayerIndex::One), "4c05e60c");
    EXPECT_EQ(GamePad::GetFirmwareVersionEXT(PlayerIndex::One), 42);
    EXPECT_EQ(GamePad::GetSteamHandleEXT(PlayerIndex::One), 99u);
    EXPECT_EQ(GamePad::GetConnectionStateEXT(PlayerIndex::One),
              CNA::Input::GamePadConnectionStateEXT::Wireless);
    int percent = -1;
    EXPECT_EQ(GamePad::GetPowerInfoEXT(PlayerIndex::One, percent),
              CNA::Input::PowerStateEXT::Charging);
    EXPECT_EQ(percent, 64);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::A),
              CNA::Input::GamePadButtonLabelEXT::Cross);
    EXPECT_EQ(platform.Canned().LastLabelButton(), CNA::Platform::GamepadButton::A);
}

TEST_F(GamePadPlatformTest, SensorsPlayerIndexAndTouchpadRoundTripThroughTheContract)
{
    platform.Canned().SetSensor(CNA::Platform::GamepadSensor::Gyroscope, {1.0f, 2.0f, 3.0f});
    platform.Canned().SetSensor(CNA::Platform::GamepadSensor::Accelerometer, {4.0f, 5.0f, 6.0f});
    platform.Canned().SetPlayerIndexResult(true);
    platform.Canned().SetPlayerIndexValue(0, 3);
    platform.Canned().SetTouchpad(0, 1, 2, {true, 0.25f, 0.5f, 0.75f});

    Vector3 gyro;
    Vector3 acceleration;
    EXPECT_TRUE(GamePad::GetGyroEXT(PlayerIndex::One, gyro));
    EXPECT_TRUE(GamePad::GetAccelerometerEXT(PlayerIndex::One, acceleration));
    EXPECT_EQ(gyro, Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(acceleration, Vector3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(GamePad::GetPlayerIndexEXT(PlayerIndex::One), 3);
    EXPECT_TRUE(GamePad::SetPlayerIndexEXT(PlayerIndex::One, 1));
    EXPECT_EQ(GamePad::GetPlayerIndexEXT(PlayerIndex::One), 1);
    EXPECT_EQ(GamePad::GetTouchpadCountEXT(PlayerIndex::One), 1);
    EXPECT_EQ(GamePad::GetTouchpadFingerCountEXT(PlayerIndex::One, 0), 2);
    bool down = false;
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 0.0f;
    EXPECT_TRUE(GamePad::GetTouchpadFingerEXT(PlayerIndex::One, 0, 0,
                                              down, x, y, pressure));
    EXPECT_TRUE(down);
    EXPECT_FLOAT_EQ(x, 0.25f);
    EXPECT_FLOAT_EQ(y, 0.5f);
    EXPECT_FLOAT_EQ(pressure, 0.75f);
}

TEST_F(GamePadPlatformTest, MissingServiceRefusesEveryOperationDeterministically)
{
    platform.SetGamepadAvailable(false);
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetCapabilities(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::SetVibration(PlayerIndex::One, 1.0f, 1.0f));
    EXPECT_FALSE(GamePad::SetTriggerVibrationEXT(PlayerIndex::One, 1.0f, 1.0f));
    EXPECT_EQ(GamePad::GetNameEXT(PlayerIndex::One), "");
}

// --- GamePadCapabilities ---

TEST(GamePadCapabilitiesTest, DefaultConstructorHasAllFlagsFalseAndTypeUnknown)
{
    const GamePadCapabilities caps;

    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_FALSE(caps.getHasAButtonProperty());
    EXPECT_FALSE(caps.getHasVoiceSupportProperty());
    EXPECT_FALSE(caps.getHasAccelerometerEXTProperty());
    EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::Unknown);
}

// A4-005: strict isolation guard — setting exactly ONE bool capability must flip ONLY its own getter and
// leave every other getter false. This is stronger than EveryGetterAndSetterRoundTrips (which sets
// cumulatively, so a getter mis-wired to an already-set field would still read true); here each getter is
// checked while only its own setter has fired, so any copy-paste getter↔field mis-wiring across the 35
// bool properties is caught.
TEST(GamePadCapabilitiesTest, EachBoolCapabilitySetterAffectsOnlyItsOwnGetter)
{
    using C = GamePadCapabilities;
    struct Pair { void (C::*set)(bool); bool (C::*get)() const; const char* name; };
    static const Pair pairs[] = {
        {&C::setIsConnectedProperty, &C::getIsConnectedProperty, "IsConnected"},
        {&C::setHasAButtonProperty, &C::getHasAButtonProperty, "HasAButton"},
        {&C::setHasBackButtonProperty, &C::getHasBackButtonProperty, "HasBackButton"},
        {&C::setHasBButtonProperty, &C::getHasBButtonProperty, "HasBButton"},
        {&C::setHasDPadDownButtonProperty, &C::getHasDPadDownButtonProperty, "HasDPadDownButton"},
        {&C::setHasDPadLeftButtonProperty, &C::getHasDPadLeftButtonProperty, "HasDPadLeftButton"},
        {&C::setHasDPadRightButtonProperty, &C::getHasDPadRightButtonProperty, "HasDPadRightButton"},
        {&C::setHasDPadUpButtonProperty, &C::getHasDPadUpButtonProperty, "HasDPadUpButton"},
        {&C::setHasLeftShoulderButtonProperty, &C::getHasLeftShoulderButtonProperty, "HasLeftShoulderButton"},
        {&C::setHasLeftStickButtonProperty, &C::getHasLeftStickButtonProperty, "HasLeftStickButton"},
        {&C::setHasRightShoulderButtonProperty, &C::getHasRightShoulderButtonProperty, "HasRightShoulderButton"},
        {&C::setHasRightStickButtonProperty, &C::getHasRightStickButtonProperty, "HasRightStickButton"},
        {&C::setHasStartButtonProperty, &C::getHasStartButtonProperty, "HasStartButton"},
        {&C::setHasXButtonProperty, &C::getHasXButtonProperty, "HasXButton"},
        {&C::setHasYButtonProperty, &C::getHasYButtonProperty, "HasYButton"},
        {&C::setHasBigButtonProperty, &C::getHasBigButtonProperty, "HasBigButton"},
        {&C::setHasLeftXThumbStickProperty, &C::getHasLeftXThumbStickProperty, "HasLeftXThumbStick"},
        {&C::setHasLeftYThumbStickProperty, &C::getHasLeftYThumbStickProperty, "HasLeftYThumbStick"},
        {&C::setHasRightXThumbStickProperty, &C::getHasRightXThumbStickProperty, "HasRightXThumbStick"},
        {&C::setHasRightYThumbStickProperty, &C::getHasRightYThumbStickProperty, "HasRightYThumbStick"},
        {&C::setHasLeftTriggerProperty, &C::getHasLeftTriggerProperty, "HasLeftTrigger"},
        {&C::setHasRightTriggerProperty, &C::getHasRightTriggerProperty, "HasRightTrigger"},
        {&C::setHasLeftVibrationMotorProperty, &C::getHasLeftVibrationMotorProperty, "HasLeftVibrationMotor"},
        {&C::setHasRightVibrationMotorProperty, &C::getHasRightVibrationMotorProperty, "HasRightVibrationMotor"},
        {&C::setHasVoiceSupportProperty, &C::getHasVoiceSupportProperty, "HasVoiceSupport"},
        {&C::setHasLightBarEXTProperty, &C::getHasLightBarEXTProperty, "HasLightBarEXT"},
        {&C::setHasTriggerVibrationMotorsEXTProperty, &C::getHasTriggerVibrationMotorsEXTProperty, "HasTriggerVibrationMotorsEXT"},
        {&C::setHasMisc1EXTProperty, &C::getHasMisc1EXTProperty, "HasMisc1EXT"},
        {&C::setHasPaddle1EXTProperty, &C::getHasPaddle1EXTProperty, "HasPaddle1EXT"},
        {&C::setHasPaddle2EXTProperty, &C::getHasPaddle2EXTProperty, "HasPaddle2EXT"},
        {&C::setHasPaddle3EXTProperty, &C::getHasPaddle3EXTProperty, "HasPaddle3EXT"},
        {&C::setHasPaddle4EXTProperty, &C::getHasPaddle4EXTProperty, "HasPaddle4EXT"},
        {&C::setHasTouchPadEXTProperty, &C::getHasTouchPadEXTProperty, "HasTouchPadEXT"},
        {&C::setHasGyroEXTProperty, &C::getHasGyroEXTProperty, "HasGyroEXT"},
        {&C::setHasAccelerometerEXTProperty, &C::getHasAccelerometerEXTProperty, "HasAccelerometerEXT"},
    };
    constexpr std::size_t n = sizeof(pairs) / sizeof(pairs[0]);
    static_assert(n == 35, "GamePadCapabilities has 35 bool capability flags");

    for (std::size_t i = 0; i < n; ++i)
    {
        GamePadCapabilities caps;
        (caps.*pairs[i].set)(true);
        for (std::size_t j = 0; j < n; ++j)
        {
            EXPECT_EQ((caps.*pairs[j].get)(), i == j)
                << "set " << pairs[i].name << " -> unexpected value of getter " << pairs[j].name;
        }
    }
}

TEST(GamePadCapabilitiesTest, EveryGetterAndSetterRoundTrips)
{
    GamePadCapabilities caps;

    caps.setIsConnectedProperty(true);
    EXPECT_TRUE(caps.getIsConnectedProperty());
    caps.setHasAButtonProperty(true);
    EXPECT_TRUE(caps.getHasAButtonProperty());
    caps.setHasBackButtonProperty(true);
    EXPECT_TRUE(caps.getHasBackButtonProperty());
    caps.setHasBButtonProperty(true);
    EXPECT_TRUE(caps.getHasBButtonProperty());
    caps.setHasDPadDownButtonProperty(true);
    EXPECT_TRUE(caps.getHasDPadDownButtonProperty());
    caps.setHasDPadLeftButtonProperty(true);
    EXPECT_TRUE(caps.getHasDPadLeftButtonProperty());
    caps.setHasDPadRightButtonProperty(true);
    EXPECT_TRUE(caps.getHasDPadRightButtonProperty());
    caps.setHasDPadUpButtonProperty(true);
    EXPECT_TRUE(caps.getHasDPadUpButtonProperty());
    caps.setHasLeftShoulderButtonProperty(true);
    EXPECT_TRUE(caps.getHasLeftShoulderButtonProperty());
    caps.setHasLeftStickButtonProperty(true);
    EXPECT_TRUE(caps.getHasLeftStickButtonProperty());
    caps.setHasRightShoulderButtonProperty(true);
    EXPECT_TRUE(caps.getHasRightShoulderButtonProperty());
    caps.setHasRightStickButtonProperty(true);
    EXPECT_TRUE(caps.getHasRightStickButtonProperty());
    caps.setHasStartButtonProperty(true);
    EXPECT_TRUE(caps.getHasStartButtonProperty());
    caps.setHasXButtonProperty(true);
    EXPECT_TRUE(caps.getHasXButtonProperty());
    caps.setHasYButtonProperty(true);
    EXPECT_TRUE(caps.getHasYButtonProperty());
    caps.setHasBigButtonProperty(true);
    EXPECT_TRUE(caps.getHasBigButtonProperty());
    caps.setHasLeftXThumbStickProperty(true);
    EXPECT_TRUE(caps.getHasLeftXThumbStickProperty());
    caps.setHasLeftYThumbStickProperty(true);
    EXPECT_TRUE(caps.getHasLeftYThumbStickProperty());
    caps.setHasRightXThumbStickProperty(true);
    EXPECT_TRUE(caps.getHasRightXThumbStickProperty());
    caps.setHasRightYThumbStickProperty(true);
    EXPECT_TRUE(caps.getHasRightYThumbStickProperty());
    caps.setHasLeftTriggerProperty(true);
    EXPECT_TRUE(caps.getHasLeftTriggerProperty());
    caps.setHasRightTriggerProperty(true);
    EXPECT_TRUE(caps.getHasRightTriggerProperty());
    caps.setHasLeftVibrationMotorProperty(true);
    EXPECT_TRUE(caps.getHasLeftVibrationMotorProperty());
    caps.setHasRightVibrationMotorProperty(true);
    EXPECT_TRUE(caps.getHasRightVibrationMotorProperty());
    caps.setHasVoiceSupportProperty(true);
    EXPECT_TRUE(caps.getHasVoiceSupportProperty());
    caps.setGamePadTypeProperty(GamePadType::Wheel);
    EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::Wheel);

    caps.setHasLightBarEXTProperty(true);
    EXPECT_TRUE(caps.getHasLightBarEXTProperty());
    caps.setHasTriggerVibrationMotorsEXTProperty(true);
    EXPECT_TRUE(caps.getHasTriggerVibrationMotorsEXTProperty());
    caps.setHasMisc1EXTProperty(true);
    EXPECT_TRUE(caps.getHasMisc1EXTProperty());
    caps.setHasPaddle1EXTProperty(true);
    EXPECT_TRUE(caps.getHasPaddle1EXTProperty());
    caps.setHasPaddle2EXTProperty(true);
    EXPECT_TRUE(caps.getHasPaddle2EXTProperty());
    caps.setHasPaddle3EXTProperty(true);
    EXPECT_TRUE(caps.getHasPaddle3EXTProperty());
    caps.setHasPaddle4EXTProperty(true);
    EXPECT_TRUE(caps.getHasPaddle4EXTProperty());
    caps.setHasTouchPadEXTProperty(true);
    EXPECT_TRUE(caps.getHasTouchPadEXTProperty());
    caps.setHasGyroEXTProperty(true);
    EXPECT_TRUE(caps.getHasGyroEXTProperty());
    caps.setHasAccelerometerEXTProperty(true);
    EXPECT_TRUE(caps.getHasAccelerometerEXTProperty());
}


TEST_F(GamePadPlatformTest, Tq025VibrationPropagatesFailureAndCanRetry)
{
    // IPlatformGamepad.SetRumble returns whether the request succeeded; XNA GamePad
    // SetVibration IL_0040..0064 reports success / disconnected-or-busy failure as bool.
    platform.Canned().SetRumbleResult(false);
    EXPECT_FALSE(GamePad::SetVibration(PlayerIndex::Four, 0.0f, 1.0f));
    EXPECT_EQ(platform.Canned().LastSlot(), 3);
    EXPECT_EQ(platform.Canned().RumbleCalls(), 1);
    EXPECT_FLOAT_EQ(platform.Canned().LastLeft(), 0.0f);
    EXPECT_FLOAT_EQ(platform.Canned().LastRight(), 1.0f);
    platform.Canned().SetRumbleResult(true);
    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::Four, 1.0f, 0.0f));
    EXPECT_EQ(platform.Canned().RumbleCalls(), 2);
    EXPECT_FLOAT_EQ(platform.Canned().LastLeft(), 1.0f);
    EXPECT_FLOAT_EQ(platform.Canned().LastRight(), 0.0f);
}


TEST_F(GamePadPlatformTest, Tq025DisconnectedSlotsReturnCompleteRestSnapshots)
{
    // FNA SDL3_FNAPlatform.GetGamePadState/GetGamePadCapabilities return default values
    // for a disconnected slot. XNA GamePad.GetState/GetCapabilities IL_0000..0008
    // likewise initializes native storage to zero before polling. CNA's platform service
    // can retain stale fields, which must not leak into the public disconnected snapshot.
    CNA::Platform::GamepadSnapshot stale;
    stale.buttons = 0xffff;
    stale.axes.fill(1.0f);
    stale.packetNumber = 123;
    CNA::Platform::GamepadCapabilities staleCaps;
    staleCaps.buttons = 0xffff;
    staleCaps.axes = 0x3f;
    staleCaps.kind = CNA::Platform::GamepadKind::Gamepad;
    staleCaps.rumble = true;
    for (int slot = 0; slot < 4; ++slot)
    {
        platform.Canned().SetPendingSnapshot(slot, stale);
        platform.Canned().SetCapabilities(slot, staleCaps);
    }
    platform.Canned().Update();
    for (const auto player : {PlayerIndex::One, PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four})
    {
        for (const auto mode : {GamePadDeadZone::None, GamePadDeadZone::IndependentAxes, GamePadDeadZone::Circular})
        {
            const auto state = GamePad::GetState(player, mode);
            EXPECT_FALSE(state.getIsConnectedProperty());
            EXPECT_EQ(state.getPacketNumberProperty(), 0);
            EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2::Zero);
            EXPECT_EQ(state.getThumbSticksProperty().getRightProperty(), Vector2::Zero);
            EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 0.0f);
            EXPECT_FLOAT_EQ(state.getTriggersProperty().getRightProperty(), 0.0f);
            EXPECT_EQ(state.getButtonsProperty().getAProperty(), ButtonState::Released);
            EXPECT_EQ(state.ToString(), "{IsConnected:False}");
        }
        const auto caps = GamePad::GetCapabilities(player);
        EXPECT_EQ(caps.getGamePadTypeProperty(), GamePadType::Unknown);
        EXPECT_FALSE(caps.getIsConnectedProperty());
        EXPECT_FALSE(caps.getHasAButtonProperty());
        EXPECT_FALSE(caps.getHasBackButtonProperty());
        EXPECT_FALSE(caps.getHasBButtonProperty());
        EXPECT_FALSE(caps.getHasDPadDownButtonProperty());
        EXPECT_FALSE(caps.getHasDPadLeftButtonProperty());
        EXPECT_FALSE(caps.getHasDPadRightButtonProperty());
        EXPECT_FALSE(caps.getHasDPadUpButtonProperty());
        EXPECT_FALSE(caps.getHasLeftShoulderButtonProperty());
        EXPECT_FALSE(caps.getHasLeftStickButtonProperty());
        EXPECT_FALSE(caps.getHasRightShoulderButtonProperty());
        EXPECT_FALSE(caps.getHasRightStickButtonProperty());
        EXPECT_FALSE(caps.getHasStartButtonProperty());
        EXPECT_FALSE(caps.getHasXButtonProperty());
        EXPECT_FALSE(caps.getHasYButtonProperty());
        EXPECT_FALSE(caps.getHasBigButtonProperty());
        EXPECT_FALSE(caps.getHasLeftXThumbStickProperty());
        EXPECT_FALSE(caps.getHasLeftYThumbStickProperty());
        EXPECT_FALSE(caps.getHasRightXThumbStickProperty());
        EXPECT_FALSE(caps.getHasRightYThumbStickProperty());
        EXPECT_FALSE(caps.getHasLeftTriggerProperty());
        EXPECT_FALSE(caps.getHasRightTriggerProperty());
        EXPECT_FALSE(caps.getHasLeftVibrationMotorProperty());
        EXPECT_FALSE(caps.getHasRightVibrationMotorProperty());
        EXPECT_FALSE(caps.getHasVoiceSupportProperty());
    }
}


TEST_F(GamePadPlatformTest, Tq025VirtualButtonsUseRawAxesInEveryDeadZoneMode)
{
    // XNA GamePadState.IsButtonDown IL_0017..02bd always applies IndependentAxes to
    // _state's raw axes, independently of the mode used for the exposed analog values.
    using Axis = CNA::Platform::GamepadAxis;
    for (float sign : {-1.0f, 1.0f})
    {
        CNA::Platform::GamepadSnapshot snapshot;
        snapshot.connected = true;
        snapshot.axes.fill(sign * 0.3f);
        snapshot.axes[static_cast<std::size_t>(Axis::LeftTrigger)] = 0.15f;
        snapshot.axes[static_cast<std::size_t>(Axis::RightTrigger)] = 0.15f;
        Connect(snapshot);
        const Buttons down = Buttons::LeftTrigger | Buttons::RightTrigger |
            (sign < 0 ? Buttons::LeftThumbstickLeft | Buttons::LeftThumbstickDown |
                        Buttons::RightThumbstickLeft | Buttons::RightThumbstickDown
                      : Buttons::LeftThumbstickRight | Buttons::LeftThumbstickUp |
                        Buttons::RightThumbstickRight | Buttons::RightThumbstickUp);
        const Buttons up = sign < 0 ? Buttons::LeftThumbstickRight | Buttons::LeftThumbstickUp |
                                     Buttons::RightThumbstickRight | Buttons::RightThumbstickUp
                                   : Buttons::LeftThumbstickLeft | Buttons::LeftThumbstickDown |
                                     Buttons::RightThumbstickLeft | Buttons::RightThumbstickDown;
        for (const auto mode : {GamePadDeadZone::None, GamePadDeadZone::IndependentAxes,
                                GamePadDeadZone::Circular})
        {
            SCOPED_TRACE(static_cast<int>(mode));
            const auto state = GamePad::GetState(PlayerIndex::One, mode);
            EXPECT_TRUE(state.IsButtonDown(down));
            EXPECT_FALSE(state.IsButtonUp(down));
            EXPECT_TRUE(state.IsButtonUp(up));
            EXPECT_FALSE(state.IsButtonDown(up));
        }
    }
}
