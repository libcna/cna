// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/CannedGamepad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include <memory>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
namespace {
class Tq075GamePadCapabilitiesPlatformTest : public ::testing::Test {
protected:
    CNA::Platform::Testing::CannedGamepadPlatform platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;
    void SetUp() override {
        installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
    }
};
}

TEST_F(Tq075GamePadCapabilitiesPlatformTest, Tq075CapabilitiesFollowFeaturesForEveryRepresentableKind)
{
    // Refresh GamePadCapabilities getters read native feature fields, never SubType:
    // button getters IL_0000..0019/001a/001d; axis/motor getters IL_0000..0016;
    // Voice IL_0000..0013 (Flags & 4). Type IL_0000..0033, connected IL_0000..0006.
    using Kind = CNA::Platform::GamepadKind;
    using C = GamePadCapabilities;
    const struct { Kind kind; GamePadType type; } kinds[] = {
        {Kind::Unknown, GamePadType::Unknown},
        {Kind::Gamepad, GamePadType::GamePad},
        {Kind::Wheel, GamePadType::Wheel},
        {Kind::ArcadeStick, GamePadType::ArcadeStick},
        {Kind::FlightStick, GamePadType::FlightStick},
        {Kind::DancePad, GamePadType::DancePad},
        {Kind::Guitar, GamePadType::Guitar},
        {Kind::DrumKit, GamePadType::DrumKit},
        {Kind::BigButtonPad, GamePadType::BigButtonPad},
    };
    bool (C::*getters[])() const = {
        &C::getHasAButtonProperty,
        &C::getHasBButtonProperty,
        &C::getHasXButtonProperty,
        &C::getHasYButtonProperty,
        &C::getHasBackButtonProperty,
        &C::getHasStartButtonProperty,
        &C::getHasBigButtonProperty,
        &C::getHasLeftShoulderButtonProperty,
        &C::getHasRightShoulderButtonProperty,
        &C::getHasLeftStickButtonProperty,
        &C::getHasRightStickButtonProperty,
        &C::getHasDPadUpButtonProperty,
        &C::getHasDPadDownButtonProperty,
        &C::getHasDPadLeftButtonProperty,
        &C::getHasDPadRightButtonProperty,
        &C::getHasLeftXThumbStickProperty,
        &C::getHasLeftYThumbStickProperty,
        &C::getHasRightXThumbStickProperty,
        &C::getHasRightYThumbStickProperty,
        &C::getHasLeftTriggerProperty,
        &C::getHasRightTriggerProperty,
        &C::getHasLeftVibrationMotorProperty,
        &C::getHasRightVibrationMotorProperty,
        &C::getHasVoiceSupportProperty,
    };
    const CNA::Platform::GamepadButton buttonBits[] = {
        CNA::Platform::GamepadButton::A,
        CNA::Platform::GamepadButton::B,
        CNA::Platform::GamepadButton::X,
        CNA::Platform::GamepadButton::Y,
        CNA::Platform::GamepadButton::Back,
        CNA::Platform::GamepadButton::Start,
        CNA::Platform::GamepadButton::BigButton,
        CNA::Platform::GamepadButton::LeftShoulder,
        CNA::Platform::GamepadButton::RightShoulder,
        CNA::Platform::GamepadButton::LeftStick,
        CNA::Platform::GamepadButton::RightStick,
        CNA::Platform::GamepadButton::DPadUp,
        CNA::Platform::GamepadButton::DPadDown,
        CNA::Platform::GamepadButton::DPadLeft,
        CNA::Platform::GamepadButton::DPadRight,
    };
    const CNA::Platform::GamepadAxis axisBits[] = {
        CNA::Platform::GamepadAxis::LeftThumbstickX,
        CNA::Platform::GamepadAxis::LeftThumbstickY,
        CNA::Platform::GamepadAxis::RightThumbstickX,
        CNA::Platform::GamepadAxis::RightThumbstickY,
        CNA::Platform::GamepadAxis::LeftTrigger,
        CNA::Platform::GamepadAxis::RightTrigger,
    };
    for (const auto& kind : kinds) {
        SCOPED_TRACE(static_cast<int>(kind.type));
        // Each feature and its complement; case 21 selects both motors, case 22 selects none.
        for (int selected = 0; selected <= 22; ++selected) {
            for (const bool complement : {false, true}) {
                CNA::Platform::GamepadCapabilities source;
                source.connected = true;
                source.kind = kind.kind;
                for (int i = 0; i < 15; ++i)
                    if ((i == selected) != complement)
                        source.buttons |= static_cast<std::uint32_t>(buttonBits[i]);
                for (int i = 0; i < 6; ++i)
                    if ((15 + i == selected) != complement)
                        source.axes |= CNA::Platform::GamepadAxisBit(axisBits[i]);
                source.rumble = (selected == 21) != complement;
                platform.Canned().SetCapabilities(0, source);
                const auto caps = GamePad::GetCapabilities(PlayerIndex::One);
                EXPECT_TRUE(caps.getIsConnectedProperty());
                EXPECT_EQ(caps.getGamePadTypeProperty(), kind.type);
                for (int i = 0; i < 24; ++i) {
                    // Platform has one rumble promise, no voice promise.
                    const bool expected = i == 23 ? false :
                        ((i == 22 ? 21 : i) == selected) != complement;
                    EXPECT_EQ((caps.*getters[i])(), expected) << selected << ':' << i;
                }
                // Stale features must disappear on disconnect, for every kind and pattern.
                source.connected = false;
                platform.Canned().SetCapabilities(0, source);
                const auto disconnected = GamePad::GetCapabilities(PlayerIndex::One);
                EXPECT_FALSE(disconnected.getIsConnectedProperty());
                EXPECT_EQ(disconnected.getGamePadTypeProperty(), GamePadType::Unknown);
                for (auto getter : getters) EXPECT_FALSE((disconnected.*getter)());
            }
        }
    }
}

TEST(Tq075GamePadCapabilitiesTest, EveryPublicTypeIncludingAlternateGuitarKeepsIndependentFeatures)
{
    // Refresh get_GamePadType IL_000e..0027 or IL_0028..0033 returns the native type;
    // Has* getters are independent. CNA's writable value properties preserve this separation.
    const GamePadType types[] = {GamePadType::Unknown, GamePadType::GamePad, GamePadType::Wheel,
        GamePadType::ArcadeStick, GamePadType::FlightStick, GamePadType::DancePad,
        GamePadType::Guitar, GamePadType::AlternateGuitar, GamePadType::DrumKit,
        GamePadType::BigButtonPad};
    for (auto type : types) {
        GamePadCapabilities caps;
        caps.setHasVoiceSupportProperty(true);
        caps.setHasLeftVibrationMotorProperty(true);
        caps.setGamePadTypeProperty(type);
        EXPECT_EQ(caps.getGamePadTypeProperty(), type);
        EXPECT_FALSE(caps.getIsConnectedProperty());
        EXPECT_TRUE(caps.getHasVoiceSupportProperty());
        EXPECT_TRUE(caps.getHasLeftVibrationMotorProperty());
        EXPECT_FALSE(caps.getHasRightVibrationMotorProperty());
        using C = GamePadCapabilities;
        const struct { void (C::*set)(bool); bool (C::*get)() const; } fields[] = {
            {&C::setHasAButtonProperty, &C::getHasAButtonProperty},
            {&C::setHasBButtonProperty, &C::getHasBButtonProperty},
            {&C::setHasXButtonProperty, &C::getHasXButtonProperty},
            {&C::setHasYButtonProperty, &C::getHasYButtonProperty},
            {&C::setHasBackButtonProperty, &C::getHasBackButtonProperty},
            {&C::setHasStartButtonProperty, &C::getHasStartButtonProperty},
            {&C::setHasBigButtonProperty, &C::getHasBigButtonProperty},
            {&C::setHasLeftShoulderButtonProperty, &C::getHasLeftShoulderButtonProperty},
            {&C::setHasRightShoulderButtonProperty, &C::getHasRightShoulderButtonProperty},
            {&C::setHasLeftStickButtonProperty, &C::getHasLeftStickButtonProperty},
            {&C::setHasRightStickButtonProperty, &C::getHasRightStickButtonProperty},
            {&C::setHasDPadUpButtonProperty, &C::getHasDPadUpButtonProperty},
            {&C::setHasDPadDownButtonProperty, &C::getHasDPadDownButtonProperty},
            {&C::setHasDPadLeftButtonProperty, &C::getHasDPadLeftButtonProperty},
            {&C::setHasDPadRightButtonProperty, &C::getHasDPadRightButtonProperty},
            {&C::setHasLeftXThumbStickProperty, &C::getHasLeftXThumbStickProperty},
            {&C::setHasLeftYThumbStickProperty, &C::getHasLeftYThumbStickProperty},
            {&C::setHasRightXThumbStickProperty, &C::getHasRightXThumbStickProperty},
            {&C::setHasRightYThumbStickProperty, &C::getHasRightYThumbStickProperty},
            {&C::setHasLeftTriggerProperty, &C::getHasLeftTriggerProperty},
            {&C::setHasRightTriggerProperty, &C::getHasRightTriggerProperty},
            {&C::setHasLeftVibrationMotorProperty, &C::getHasLeftVibrationMotorProperty},
            {&C::setHasRightVibrationMotorProperty, &C::getHasRightVibrationMotorProperty},
            {&C::setHasVoiceSupportProperty, &C::getHasVoiceSupportProperty},
        };
        for (int selected = 0; selected < 24; ++selected) {
            C value;
            value.setGamePadTypeProperty(type);
            (value.*fields[selected].set)(true);
            EXPECT_EQ(value.getGamePadTypeProperty(), type);
            for (int i = 0; i < 24; ++i)
                EXPECT_EQ((value.*fields[i].get)(), i == selected);
        }
    }
}
