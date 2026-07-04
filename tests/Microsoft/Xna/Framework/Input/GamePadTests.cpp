// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"

using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

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

// --- Task 816: GUID formatting (matches FNA's GetGamePadGUID, not the full SDL GUID string) ---

TEST(GamePadTest, FormatGUIDReturnsXinputWhenVendorAndProductAreZero)
{
    // XInput controllers report no USB vendor/product; FNA returns the literal "xinput".
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x0000, 0x0000), "xinput");
}

TEST(GamePadTest, FormatGUIDEmitsVendorThenProductLittleEndianHex)
{
    // Sony DualShock 4: vendor 0x054C, product 0x05C4 -> bytes 4c 05 c4 05 -> "4c05c405"
    // (the exact string FNA hard-codes for a Valve-re-exposed PS4 pad).
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x054C, 0x05C4), "4c05c405");

    // Sony DualSense (PS5): vendor 0x054C, product 0x0CE6 -> 4c 05 e6 0c -> "4c05e60c".
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x054C, 0x0CE6), "4c05e60c");

    // Microsoft Xbox 360 wired: vendor 0x045E, product 0x028E -> 5e 04 8e 02 -> "5e048e02".
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x045E, 0x028E), "5e048e02");

    // Arbitrary IDs, to pin the byte order unambiguously.
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x1234, 0x5678), "34127856");
}

TEST(GamePadTest, FormatGUIDIsAlwaysEightHexCharsForNonZeroIds)
{
    // Low ids must stay zero-padded to the full 8 chars (no truncation).
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x0001, 0x0002), "01000200");
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
