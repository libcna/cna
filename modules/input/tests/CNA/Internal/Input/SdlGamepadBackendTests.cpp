// SPDX-License-Identifier: MS-PL
//
// Phase I15: device-level SDL gamepad tests using the injectable fake backend (no real hardware).
// Covers hot-plug/slot assignment, FNA_GAMEPAD_NUM_GAMEPADS, button/axis mapping, capabilities,
// rumble/LED/sensor support, and GUID formatting — driving the real SdlInputBridge event path with
// a fake ISdlGamepadBackend swapped in.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <limits>

#include "CNA/Input/GamePadButtonLabel.hpp"
#include "CNA/Input/GamePadConnectionState.hpp"
#include "CNA/Input/PowerState.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Input/SdlGamepadBackend.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include "FakeSdlGamepadBackend.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Internal::Input::SdlInputBridge;
using CNA::Internal::Input::SetSdlGamepadBackendForTests;
using CNA::Internal::Input::test_support::FakeGamepadConfig;
using CNA::Internal::Input::test_support::FakeSdlGamepadBackend;
using CNA::Internal::Input::test_support::FullyFeaturedGamepad;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Input::Buttons;
using Microsoft::Xna::Framework::Input::GamePad;
using Microsoft::Xna::Framework::Input::GamePadType;

namespace
{
    SDL_Event addedEvent(SDL_JoystickID which)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_ADDED;
        e.gdevice.which = which;
        return e;
    }
    SDL_Event removedEvent(SDL_JoystickID which)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_REMOVED;
        e.gdevice.which = which;
        return e;
    }
    SDL_Event buttonEvent(bool down, SDL_JoystickID which, SDL_GamepadButton button)
    {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.which = which;
        e.gbutton.button = static_cast<Uint8>(button);
        return e;
    }
    SDL_Event axisEvent(SDL_JoystickID which, SDL_GamepadAxis axis, Sint16 value)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        e.gaxis.which = which;
        e.gaxis.axis = static_cast<Uint8>(axis);
        e.gaxis.value = value;
        return e;
    }

    struct FakeGamepadTest : ::testing::Test
    {
        FakeSdlGamepadBackend fake;

        void SetUp() override
        {
            InputManager::ResetAllForTests();          // restores the real backend + clears slot maps
            SetSdlGamepadBackendForTests(&fake);        // then install the fake for this test
        }
        void TearDown() override
        {
            SetSdlGamepadBackendForTests(nullptr);      // restore real BEFORE fake is destroyed
            InputManager::ResetAllForTests();
        }
    };
}

// --- hot-plug / slots (908/910/911/912) ---

TEST_F(FakeGamepadTest, PadConnectedBeforeFirstFrameBecomesVisible)
{
    // A pad present at subsystem-init produces a queued SDL_EVENT_GAMEPAD_ADDED; delivering it
    // before any Update() makes the pad visible to GamePad::GetState.
    fake.Register(10, FullyFeaturedGamepad());
    ASSERT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_EQ(fake.openCount, 1);
}

TEST_F(FakeGamepadTest, PlatformEventsDriveConnectionButtonsAxesAndRemoval)
{
    using CNA::Platform::ControllerAxisEvent;
    using CNA::Platform::ControllerButtonEvent;
    using CNA::Platform::DeviceEvent;
    using CNA::Platform::GamepadAxis;
    using CNA::Platform::GamepadButton;
    using CNA::Platform::InputDeviceKind;

    fake.Register(10, FullyFeaturedGamepad());
    PlatformInputBridge::ProcessEvent(DeviceEvent{10, InputDeviceKind::Gamepad, true});
    ASSERT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    struct ButtonCase
    {
        GamepadButton platform;
        Buttons xna;
    };
    const ButtonCase buttons[] = {
        {GamepadButton::A, Buttons::A},
        {GamepadButton::B, Buttons::B},
        {GamepadButton::X, Buttons::X},
        {GamepadButton::Y, Buttons::Y},
        {GamepadButton::Back, Buttons::Back},
        {GamepadButton::Start, Buttons::Start},
        {GamepadButton::LeftShoulder, Buttons::LeftShoulder},
        {GamepadButton::RightShoulder, Buttons::RightShoulder},
        {GamepadButton::LeftStick, Buttons::LeftStick},
        {GamepadButton::RightStick, Buttons::RightStick},
        {GamepadButton::DPadUp, Buttons::DPadUp},
        {GamepadButton::DPadDown, Buttons::DPadDown},
        {GamepadButton::DPadLeft, Buttons::DPadLeft},
        {GamepadButton::DPadRight, Buttons::DPadRight},
        {GamepadButton::BigButton, Buttons::BigButton},
        {GamepadButton::Misc1, Buttons::Misc1EXT},
        {GamepadButton::Paddle1, Buttons::Paddle1EXT},
        {GamepadButton::Paddle2, Buttons::Paddle2EXT},
        {GamepadButton::Paddle3, Buttons::Paddle3EXT},
        {GamepadButton::Paddle4, Buttons::Paddle4EXT},
        {GamepadButton::TouchPad, Buttons::TouchPadEXT},
    };
    for (const auto& button : buttons)
    {
        PlatformInputBridge::ProcessEvent(
            ControllerButtonEvent{10, button.platform, true});
        EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonDown(button.xna));
        PlatformInputBridge::ProcessEvent(
            ControllerButtonEvent{10, button.platform, false});
        EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).IsButtonDown(button.xna));
    }

    PlatformInputBridge::ProcessEvent(
        ControllerAxisEvent{10, GamepadAxis::LeftThumbstickX, -0.25f});
    PlatformInputBridge::ProcessEvent(
        ControllerAxisEvent{10, GamepadAxis::LeftThumbstickY, 0.50f});
    PlatformInputBridge::ProcessEvent(
        ControllerAxisEvent{10, GamepadAxis::RightThumbstickX, 0.75f});
    PlatformInputBridge::ProcessEvent(
        ControllerAxisEvent{10, GamepadAxis::RightThumbstickY, -1.0f});
    PlatformInputBridge::ProcessEvent(
        ControllerAxisEvent{10, GamepadAxis::LeftTrigger, 0.40f});
    PlatformInputBridge::ProcessEvent(
        ControllerAxisEvent{10, GamepadAxis::RightTrigger, 0.80f});
    const auto raw = InputManager::GetRawGamePadState(PlayerIndex::One);
    EXPECT_FLOAT_EQ(raw.leftX, -0.25f);
    EXPECT_FLOAT_EQ(raw.leftY, 0.50f);
    EXPECT_FLOAT_EQ(raw.rightX, 0.75f);
    EXPECT_FLOAT_EQ(raw.rightY, -1.0f);
    EXPECT_FLOAT_EQ(raw.leftTrigger, 0.40f);
    EXPECT_FLOAT_EQ(raw.rightTrigger, 0.80f);

    PlatformInputBridge::ProcessEvent(DeviceEvent{10, InputDeviceKind::Gamepad, false});
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_EQ(fake.closeCount, 1);
}

TEST_F(FakeGamepadTest, DuplicateAddDoesNotLeakOrAllocateSecondSlot)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    SdlInputBridge::ProcessEvent(addedEvent(10)); // duplicate

    EXPECT_EQ(fake.openCount, 1) << "duplicate add must not open a second handle";
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
}

TEST_F(FakeGamepadTest, UnknownRemoveIsIgnored)
{
    SdlInputBridge::ProcessEvent(removedEvent(999)); // never added
    EXPECT_EQ(fake.closeCount, 0);
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
}

TEST_F(FakeGamepadTest, RemoveClosesCorrectHandleAndDisconnectsPlayer)
{
    fake.Register(10, FullyFeaturedGamepad());
    fake.Register(20, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10)); // -> player One
    SdlInputBridge::ProcessEvent(addedEvent(20)); // -> player Two

    SdlInputBridge::ProcessEvent(removedEvent(10));

    EXPECT_EQ(fake.closeCount, 1);
    EXPECT_EQ(fake.lastClosedId, 10);
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
}

TEST_F(FakeGamepadTest, MoreThanFourPadsRefusedWhenNoFreeSlot)
{
    const SDL_JoystickID ids[] = {10, 20, 30, 40, 50};
    for (SDL_JoystickID id : ids)
        fake.Register(id, FullyFeaturedGamepad());
    for (SDL_JoystickID id : ids)
        SdlInputBridge::ProcessEvent(addedEvent(id));

    EXPECT_EQ(fake.openCount, 4) << "the 5th pad has no free slot and must be refused";
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::Three).getIsConnectedProperty());
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::Four).getIsConnectedProperty());
}

// --- FNA_GAMEPAD_NUM_GAMEPADS (913/914) ---

TEST(FakeGamepadEnvCount, ParsesFnaGamepadNumGamepadsValues)
{
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests("0"), 0u);
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests("1"), 1u);
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests("4"), 4u);
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests("8"), 4u);  // clamped to PlayerIndex max
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests("-1"), 4u); // negative -> default
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests("abc"), 4u);// non-numeric -> default
    EXPECT_EQ(SdlInputBridge::ParseGamepadCountForTests(nullptr), 4u);
}

TEST_F(FakeGamepadTest, GamepadCountOfOneLimitsToASingleSlot)
{
    SdlInputBridge::SetGamepadCountForTests(1);
    fake.Register(10, FullyFeaturedGamepad());
    fake.Register(20, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    SdlInputBridge::ProcessEvent(addedEvent(20));

    EXPECT_EQ(fake.openCount, 1);
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
}

TEST_F(FakeGamepadTest, GamepadCountOfZeroDisablesTracking)
{
    SdlInputBridge::SetGamepadCountForTests(0);
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(fake.openCount, 0);
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
}

// --- button mapping for every supported SDL button (920/921) ---

TEST_F(FakeGamepadTest, EverySdlButtonMapsToTheExpectedXnaButton)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    struct Case { SDL_GamepadButton sdl; Buttons xna; };
    const Case cases[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, Buttons::A},
        {SDL_GAMEPAD_BUTTON_EAST, Buttons::B},
        {SDL_GAMEPAD_BUTTON_WEST, Buttons::X},
        {SDL_GAMEPAD_BUTTON_NORTH, Buttons::Y},
        {SDL_GAMEPAD_BUTTON_BACK, Buttons::Back},
        {SDL_GAMEPAD_BUTTON_START, Buttons::Start},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, Buttons::LeftStick},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, Buttons::RightStick},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Buttons::LeftShoulder},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Buttons::RightShoulder},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, Buttons::DPadUp},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Buttons::DPadDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Buttons::DPadLeft},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Buttons::DPadRight},
        {SDL_GAMEPAD_BUTTON_GUIDE, Buttons::BigButton},
        {SDL_GAMEPAD_BUTTON_MISC1, Buttons::Misc1EXT},
        {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, Buttons::Paddle1EXT},
        {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, Buttons::Paddle2EXT},
        {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, Buttons::Paddle3EXT},
        {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, Buttons::Paddle4EXT},
        {SDL_GAMEPAD_BUTTON_TOUCHPAD, Buttons::TouchPadEXT},
    };

    for (const auto& c : cases)
    {
        SdlInputBridge::ProcessEvent(buttonEvent(true, 10, c.sdl));
        EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonDown(c.xna))
            << "SDL button " << static_cast<int>(c.sdl) << " should map down";
        SdlInputBridge::ProcessEvent(buttonEvent(false, 10, c.sdl));
        EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).IsButtonDown(c.xna))
            << "SDL button " << static_cast<int>(c.sdl) << " should map up";
    }
}

// --- axis mapping incl. Y inversion + trigger normalization (918/919) ---

TEST_F(FakeGamepadTest, AxisMappingHandlesYInversionAndTriggerNormalization)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    // Use the RAW state (pre-dead-zone) to assert the exact normalized values.
    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTX, 32767));   // full right
    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTY, -32768));  // SDL "up"
    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_RIGHTX, -32768)); // full left
    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767));

    {
        const auto raw = InputManager::GetRawGamePadState(PlayerIndex::One);
        EXPECT_NEAR(raw.leftX, 1.0f, 1e-3f);
        EXPECT_NEAR(raw.leftY, 1.0f, 1e-3f) << "SDL up (-32768) must invert to XNA +1.0";
        EXPECT_NEAR(raw.rightX, -1.0f, 1e-3f);
        EXPECT_NEAR(raw.leftTrigger, 1.0f, 1e-3f);
        EXPECT_NEAR(raw.rightTrigger, 0.0f, 1e-3f);
    }

    // SDL "down" on Y inverts to XNA -1.0.
    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTY, 32767));
    EXPECT_NEAR(InputManager::GetRawGamePadState(PlayerIndex::One).leftY, -1.0f, 1e-3f);
}

// L-015: stick-axis normalization must match FNA's `axis / 32767` over the WHOLE Sint16 range (both the
// positive and the negative half), NOT `/32768` for negatives. At a non-endpoint sample the divisors
// diverge: 16384/32767 = 0.500015 vs 16384/32768 = 0.5. Pin the FNA divisor so a regression to /32768 is
// caught (the endpoint test above tolerates 1e-3, which is coarser than the ~3e-5 divisor difference).
TEST_F(FakeGamepadTest, StickAxisNormalizationMatchesFnaDivisor)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTX, 16384));   // positive half
    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_RIGHTX, -16384)); // negative half

    const auto raw = InputManager::GetRawGamePadState(PlayerIndex::One);
    EXPECT_NEAR(raw.leftX,  16384.0f / 32767.0f, 1e-6f) << "positive half uses FNA /32767";
    EXPECT_NEAR(raw.rightX, -16384.0f / 32767.0f, 1e-6f) << "negative half must ALSO use /32767, not /32768";
    EXPECT_GT(raw.leftX, 0.5f);   // strictly > 0.5 (would be exactly 0.5 under /32768)
    EXPECT_LT(raw.rightX, -0.5f);
}

// --- capabilities (922/923) ---

TEST_F(FakeGamepadTest, CapabilitiesReflectConnectedDevice)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::One);
    EXPECT_TRUE(caps.getIsConnectedProperty());
    EXPECT_TRUE(caps.getHasAButtonProperty());
    EXPECT_TRUE(caps.getHasDPadUpButtonProperty());
    EXPECT_TRUE(caps.getHasLeftTriggerProperty());
    EXPECT_TRUE(caps.getHasTouchPadEXTProperty());
    EXPECT_TRUE(caps.getHasGyroEXTProperty());
    EXPECT_TRUE(caps.getHasAccelerometerEXTProperty());
}

// INPUT-GAMEPAD-031: on connect the bridge maps the SDL joystick type to the XNA GamePadType
// (sdl_joystick_type_to_gamepad_type) and stores it on the capabilities. Drive one pad per type into a
// separate slot through the real ProcessEvent path and assert GetCapabilities reports the mapped type.
TEST_F(FakeGamepadTest, SdlJoystickTypeMapsToXnaGamePadType)
{
    using Microsoft::Xna::Framework::Input::GamePadType;
    struct Case { SDL_JoystickID id; PlayerIndex player; SDL_JoystickType sdl; GamePadType expected; const char* name; };
    const Case cases[] = {
        {10, PlayerIndex::One,   SDL_JOYSTICK_TYPE_GAMEPAD,      GamePadType::GamePad,     "GamePad"},
        {20, PlayerIndex::Two,   SDL_JOYSTICK_TYPE_WHEEL,        GamePadType::Wheel,       "Wheel"},
        {30, PlayerIndex::Three, SDL_JOYSTICK_TYPE_ARCADE_STICK, GamePadType::ArcadeStick, "ArcadeStick"},
        {40, PlayerIndex::Four,  SDL_JOYSTICK_TYPE_FLIGHT_STICK, GamePadType::FlightStick, "FlightStick"},
    };
    for (const Case& c : cases)
    {
        FakeGamepadConfig cfg = FullyFeaturedGamepad();
        cfg.joystickType = c.sdl;
        fake.Register(c.id, cfg);
        SdlInputBridge::ProcessEvent(addedEvent(c.id));
    }
    for (const Case& c : cases)
        EXPECT_EQ(SdlInputBridge::GetCapabilities(c.player).getGamePadTypeProperty(), c.expected) << c.name;
}

TEST_F(FakeGamepadTest, CapabilitiesOfDisconnectedPlayerAreEmpty)
{
    const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::Two);
    EXPECT_FALSE(caps.getIsConnectedProperty());
    EXPECT_FALSE(caps.getHasAButtonProperty());
}

TEST_F(FakeGamepadTest, RumbleSupportReportedTrueWithoutStoppingActiveRumble)
{
    fake.Register(10, FullyFeaturedGamepad()); // rumble = true
    SdlInputBridge::ProcessEvent(addedEvent(10));

    // Start an active rumble, then read capabilities repeatedly.
    ASSERT_TRUE(SdlInputBridge::SetVibration(PlayerIndex::One, 1.0f, 1.0f));
    const int rumbleCallsAfterVibration = fake.rumbleCalls;

    const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::One);
    (void)SdlInputBridge::GetCapabilities(PlayerIndex::One);

    EXPECT_TRUE(caps.getHasLeftVibrationMotorProperty());
    EXPECT_TRUE(caps.getHasRightVibrationMotorProperty());
    EXPECT_EQ(fake.rumbleCalls, rumbleCallsAfterVibration)
        << "GetCapabilities must NOT call RumbleGamepad (that would cancel active vibration)";
}

TEST_F(FakeGamepadTest, RumbleSupportReportedFalseForNonRumblingDevice)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.rumble = false;
    cfg.triggerRumble = false;
    cfg.rgbLed = false;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::One);
    EXPECT_FALSE(caps.getHasLeftVibrationMotorProperty());
    EXPECT_FALSE(caps.getHasTriggerVibrationMotorsEXTProperty());
    EXPECT_FALSE(caps.getHasLightBarEXTProperty());
    EXPECT_EQ(fake.rumbleCalls, 0);
}

TEST_F(FakeGamepadTest, TriggerRumbleAndLightBarSupportReported)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.triggerRumble = true;
    cfg.rgbLed = true;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::One);
    EXPECT_TRUE(caps.getHasTriggerVibrationMotorsEXTProperty());
    EXPECT_TRUE(caps.getHasLightBarEXTProperty());
}

TEST_F(FakeGamepadTest, GyroAndAccelerometerSupportReportedAndAbsentWhenMissing)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    {
        const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::One);
        EXPECT_TRUE(caps.getHasGyroEXTProperty());
        EXPECT_TRUE(caps.getHasAccelerometerEXTProperty());
    }

    FakeGamepadConfig noSensors = FullyFeaturedGamepad();
    noSensors.sensors.clear();
    fake.Register(20, noSensors);
    SdlInputBridge::ProcessEvent(addedEvent(20));
    {
        const auto caps = SdlInputBridge::GetCapabilities(PlayerIndex::Two);
        EXPECT_FALSE(caps.getHasGyroEXTProperty());
        EXPECT_FALSE(caps.getHasAccelerometerEXTProperty());
    }
}

// --- GUID formatting (924) ---

TEST(FakeGamepadGuidFormat, FormatsXinputVendorProductAndNoDevice)
{
    // Pure formatter: no device needed.
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x0000, 0x0000), "xinput");
    // vendor 0x045e, product 0x028e -> little-endian bytes 5e 04 8e 02.
    EXPECT_EQ(SdlInputBridge::FormatGamePadGUIDEXT(0x045e, 0x028e), "5e048e02");
}

TEST_F(FakeGamepadTest, GetGuidUsesVendorProductAndValveOverrides)
{
    // Regular device: little-endian vendor/product.
    FakeGamepadConfig regular = FullyFeaturedGamepad();
    regular.vendor = 0x045e;
    regular.product = 0x028e;
    fake.Register(10, regular);
    SdlInputBridge::ProcessEvent(addedEvent(10));
    EXPECT_EQ(SdlInputBridge::GetGUID(PlayerIndex::One), "5e048e02");

    // Valve (0x28de) re-exposed PS4 -> fixed GUID override.
    FakeGamepadConfig valvePs4 = FullyFeaturedGamepad();
    valvePs4.vendor = 0x28de;
    valvePs4.gamepadType = SDL_GAMEPAD_TYPE_PS4;
    fake.Register(20, valvePs4);
    SdlInputBridge::ProcessEvent(addedEvent(20));
    EXPECT_EQ(SdlInputBridge::GetGUID(PlayerIndex::Two), "4c05c405");

    // Valve re-exposed Xbox One -> "xinput".
    FakeGamepadConfig valveXbox = FullyFeaturedGamepad();
    valveXbox.vendor = 0x28de;
    valveXbox.gamepadType = SDL_GAMEPAD_TYPE_XBOXONE;
    fake.Register(30, valveXbox);
    SdlInputBridge::ProcessEvent(addedEvent(30));
    EXPECT_EQ(SdlInputBridge::GetGUID(PlayerIndex::Three), "xinput");

    // Disconnected slot -> empty string.
    EXPECT_EQ(SdlInputBridge::GetGUID(PlayerIndex::Four), "");
}

// --- sensor read (923) ---

TEST_F(FakeGamepadTest, GyroAndAccelReadReturnData)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.gyroData = {{0.1f, 0.2f, 0.3f}};
    cfg.accelData = {{1.0f, 2.0f, 3.0f}};
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    Microsoft::Xna::Framework::Vector3 gyro;
    ASSERT_TRUE(SdlInputBridge::GetGyro(PlayerIndex::One, gyro));
    EXPECT_NEAR(gyro.X, 0.1f, 1e-5f);
    EXPECT_NEAR(gyro.Z, 0.3f, 1e-5f);

    Microsoft::Xna::Framework::Vector3 accel;
    ASSERT_TRUE(SdlInputBridge::GetAccelerometer(PlayerIndex::One, accel));
    EXPECT_NEAR(accel.Y, 2.0f, 1e-5f);
}

TEST_F(FakeGamepadTest, SensorReadFailsGracefullyWhenUnavailable)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.sensorReadFails = true;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    Microsoft::Xna::Framework::Vector3 gyro(9, 9, 9);
    EXPECT_FALSE(SdlInputBridge::GetGyro(PlayerIndex::One, gyro));
    EXPECT_NEAR(gyro.X, 0.0f, 1e-5f) << "failed read must zero the out param";
}

// --- vibration (P4-014) via the public GamePad::SetVibration surface ---

// P4-014(a): SetVibration maps [0,1] motor levels to SDL's 16-bit intensity as FNA does —
// (ushort)(Clamp(level,0,1) * 0xFFFF) — clamping out-of-range values first. 0.5 truncates to 32767.
TEST_F(FakeGamepadTest, SetVibrationClampsMotorLevelsToSdlIntensity)
{
    fake.Register(10, FullyFeaturedGamepad()); // rumble = true
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::One, 0.5f, 1.0f));
    EXPECT_EQ(fake.lastRumbleLow, 32767);   // 0.5 * 0xFFFF = 32767.5 -> truncated
    EXPECT_EQ(fake.lastRumbleHigh, 65535);  // 1.0 * 0xFFFF

    // Over-range and negative are clamped to [0,1] before scaling.
    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::One, 2.0f, -1.0f));
    EXPECT_EQ(fake.lastRumbleLow, 65535);
    EXPECT_EQ(fake.lastRumbleHigh, 0);

    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::One, 0.0f, 0.0f));
    EXPECT_EQ(fake.lastRumbleLow, 0);
    EXPECT_EQ(fake.lastRumbleHigh, 0);
}

// P4-014(d): NaN/Inf handling. std::clamp propagates NaN and casting NaN->integer is UB in C++, so
// SetVibration maps NaN to 0 (matching C#'s well-defined (ushort)NaN == 0). +Inf clamps to full, -Inf
// to zero. This proves the motor_level guard, not just the happy path.
TEST_F(FakeGamepadTest, SetVibrationHandlesNaNAndInfinity)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::One, nan, nan));
    EXPECT_EQ(fake.lastRumbleLow, 0) << "NaN motor level must resolve to 0, not UB";
    EXPECT_EQ(fake.lastRumbleHigh, 0);

    EXPECT_TRUE(GamePad::SetVibration(PlayerIndex::One, inf, -inf));
    EXPECT_EQ(fake.lastRumbleLow, 65535) << "+Inf clamps to full intensity";
    EXPECT_EQ(fake.lastRumbleHigh, 0) << "-Inf clamps to zero";
}

// P4-014(b): a disconnected player index never reaches the backend and reports failure.
TEST_F(FakeGamepadTest, SetVibrationReturnsFalseForDisconnectedPlayer)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10)); // -> player One only

    EXPECT_FALSE(GamePad::SetVibration(PlayerIndex::Two, 1.0f, 1.0f));
    EXPECT_EQ(fake.rumbleCalls, 0) << "no backend rumble call for a disconnected slot";
}

// P4-014(c): a connected device without rumble support still forwards the call (matching FNA, which
// unconditionally calls SDL_RumbleGamepad and returns its result) but reports false.
TEST_F(FakeGamepadTest, SetVibrationReturnsFalseWhenDeviceHasNoRumble)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.rumble = false;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_FALSE(GamePad::SetVibration(PlayerIndex::One, 1.0f, 1.0f));
    EXPECT_EQ(fake.rumbleCalls, 1) << "the call is still made; only the return reflects no support";
}

// --- trigger vibration extension (P4-015) via GamePad::SetTriggerVibrationEXT ---

// P4-015(a)+(d): the trigger-rumble extension shares the same motor_level clamping and drives the
// fake backend's RumbleGamepadTriggers seam. Duration-based rumble (P4-015 (b)) has no public API —
// the SDL duration argument is fixed at 0 — so it is intentionally not covered (documented in plan).
TEST_F(FakeGamepadTest, TriggerVibrationSucceedsAndClampsForCapableDevice)
{
    fake.Register(10, FullyFeaturedGamepad()); // triggerRumble = true
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_TRUE(GamePad::SetTriggerVibrationEXT(PlayerIndex::One, 0.5f, 2.0f));
    EXPECT_EQ(fake.triggerRumbleCalls, 1);
    EXPECT_EQ(fake.lastTriggerLow, 32767);
    EXPECT_EQ(fake.lastTriggerHigh, 65535) << "2.0 clamps to full intensity";
}

TEST_F(FakeGamepadTest, TriggerVibrationReturnsFalseWhenUnsupportedOrDisconnected)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.triggerRumble = false;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_FALSE(GamePad::SetTriggerVibrationEXT(PlayerIndex::One, 1.0f, 1.0f))
        << "unsupported device reports false";
    EXPECT_EQ(fake.triggerRumbleCalls, 1) << "call still forwarded";

    EXPECT_FALSE(GamePad::SetTriggerVibrationEXT(PlayerIndex::Two, 1.0f, 1.0f))
        << "disconnected slot reports false";
    EXPECT_EQ(fake.triggerRumbleCalls, 1) << "no forward for a disconnected slot";
}

// --- light bar extension (P4-016) via GamePad::SetLightBarEXT ---

// P4-016(a)+(c): the light-bar extension forwards the Color's R/G/B bytes to the backend LED seam.
// Color components are bytes, so there is no "invalid" color to reject (P4-016 (c) is N/A) — every
// value from black to white passes through unchanged.
TEST_F(FakeGamepadTest, LightBarForwardsColorRgbToBackend)
{
    fake.Register(10, FullyFeaturedGamepad()); // rgbLed = true
    SdlInputBridge::ProcessEvent(addedEvent(10));

    GamePad::SetLightBarEXT(PlayerIndex::One, Color(10, 20, 30));
    EXPECT_EQ(fake.ledCalls, 1);
    EXPECT_EQ(fake.lastLedR, 10);
    EXPECT_EQ(fake.lastLedG, 20);
    EXPECT_EQ(fake.lastLedB, 30);

    GamePad::SetLightBarEXT(PlayerIndex::One, Color(255, 255, 255));
    EXPECT_EQ(fake.lastLedR, 255);
    EXPECT_EQ(fake.lastLedG, 255);
    EXPECT_EQ(fake.lastLedB, 255);

    GamePad::SetLightBarEXT(PlayerIndex::One, Color(0, 0, 0));
    EXPECT_EQ(fake.ledCalls, 3);
    EXPECT_EQ(fake.lastLedR, 0);
}

// P4-016(b): the no-support path. A disconnected slot never touches the backend; a connected device
// without an RGB LED still forwards the call (matching FNA's unconditional SDL_SetGamepadLED).
TEST_F(FakeGamepadTest, LightBarNoOpsForDisconnectedButForwardsForConnectedNonLedDevice)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.rgbLed = false;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    GamePad::SetLightBarEXT(PlayerIndex::Two, Color(1, 2, 3)); // disconnected slot
    EXPECT_EQ(fake.ledCalls, 0) << "no backend call for a disconnected slot";

    GamePad::SetLightBarEXT(PlayerIndex::One, Color(1, 2, 3)); // connected but no LED
    EXPECT_EQ(fake.ledCalls, 1) << "call forwarded; hardware simply ignores it";
}

// --- player-index extension (input_noxna.md N-009) via GamePad::Get/SetPlayerIndexEXT ---

// N-009(a): GetPlayerIndexEXT reads the SDL device player index (the 0-based player-number LED)
// off the connected device; SetPlayerIndexEXT forwards the new value to the backend seam and a
// subsequent Get reads it back. This proves the round-trip through GamePad -> SdlInputBridge ->
// ISdlGamepadBackend without touching real hardware.
TEST_F(FakeGamepadTest, PlayerIndexRoundTripsThroughBackend)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.playerIndex = 2;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetPlayerIndexEXT(PlayerIndex::One), 2) << "reads the device's current LED index";

    EXPECT_TRUE(GamePad::SetPlayerIndexEXT(PlayerIndex::One, 0)) << "connected device accepts the change";
    EXPECT_EQ(fake.setPlayerIndexCalls, 1);
    EXPECT_EQ(fake.lastSetPlayerIndex, 0);
    EXPECT_EQ(GamePad::GetPlayerIndexEXT(PlayerIndex::One), 0) << "Get reads back the value just set";
}

// N-009(b): the no-device path. A disconnected slot must not touch the backend: Get returns -1 and
// Set returns false, leaving the backend's call counter untouched.
TEST_F(FakeGamepadTest, PlayerIndexIsSafeForDisconnectedSlot)
{
    EXPECT_EQ(GamePad::GetPlayerIndexEXT(PlayerIndex::Four), -1);
    EXPECT_FALSE(GamePad::SetPlayerIndexEXT(PlayerIndex::Four, 3));
    EXPECT_EQ(fake.setPlayerIndexCalls, 0) << "disconnected slot must not reach the backend";
}

// --- gamepad battery/power extension (input_noxna.md N-009b) via GamePad::GetPowerInfoEXT ---

// N-009b(a): GetPowerInfoEXT reads the device's battery state and charge percent through the seam,
// and the SDL_PowerState -> PowerStateEXT mapping is exhaustive.
TEST_F(FakeGamepadTest, PowerInfoReportsStateAndPercent)
{
    struct Case { SDL_PowerState sdl; CNA::Input::PowerStateEXT ext; int percent; };
    const Case cases[] = {
        {SDL_POWERSTATE_ON_BATTERY, CNA::Input::PowerStateEXT::OnBattery, 42},
        {SDL_POWERSTATE_NO_BATTERY, CNA::Input::PowerStateEXT::NoBattery, -1},
        {SDL_POWERSTATE_CHARGING,   CNA::Input::PowerStateEXT::Charging,  77},
        {SDL_POWERSTATE_CHARGED,    CNA::Input::PowerStateEXT::Charged,   100},
        {SDL_POWERSTATE_UNKNOWN,    CNA::Input::PowerStateEXT::Unknown,   -1},
        {SDL_POWERSTATE_ERROR,      CNA::Input::PowerStateEXT::Error,     -1},
    };
    for (const Case& c : cases)
    {
        FakeGamepadConfig cfg = FullyFeaturedGamepad();
        cfg.powerState = c.sdl;
        cfg.powerPercent = c.percent;
        fake.Register(10, cfg);
        SdlInputBridge::ProcessEvent(addedEvent(10));

        int percent = 999;
        EXPECT_EQ(GamePad::GetPowerInfoEXT(PlayerIndex::One, percent), c.ext);
        EXPECT_EQ(percent, c.percent);

        SdlInputBridge::ProcessEvent(removedEvent(10)); // reset the slot for the next case
    }
}

// N-009b(b): a disconnected slot never reaches the backend: Error state and percent forced to -1.
TEST_F(FakeGamepadTest, PowerInfoIsErrorForDisconnectedSlot)
{
    int percent = 999;
    EXPECT_EQ(GamePad::GetPowerInfoEXT(PlayerIndex::Four, percent), CNA::Input::PowerStateEXT::Error);
    EXPECT_EQ(percent, -1);
}

// --- gamepad button labels extension (input_noxna.md N-011) via GamePad::GetButtonLabelEXT ---

// N-011(a): an Xbox-style pad labels the face buttons A/B/X/Y. The XNA Buttons value is mapped to the
// SDL face button, the fake returns that button's configured label, and the SDL label maps to the EXT.
TEST_F(FakeGamepadTest, ButtonLabelReportsXboxGlyphs)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_SOUTH] = SDL_GAMEPAD_BUTTON_LABEL_A;
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_EAST]  = SDL_GAMEPAD_BUTTON_LABEL_B;
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_WEST]  = SDL_GAMEPAD_BUTTON_LABEL_X;
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_NORTH] = SDL_GAMEPAD_BUTTON_LABEL_Y;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::A), CNA::Input::GamePadButtonLabelEXT::A);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::B), CNA::Input::GamePadButtonLabelEXT::B);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::X), CNA::Input::GamePadButtonLabelEXT::X);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::Y), CNA::Input::GamePadButtonLabelEXT::Y);
}

// N-011(b): a PlayStation-style pad labels the same physical positions cross/circle/square/triangle,
// proving the full SDL_GamepadButtonLabel -> EXT mapping.
TEST_F(FakeGamepadTest, ButtonLabelReportsPlayStationGlyphs)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_SOUTH] = SDL_GAMEPAD_BUTTON_LABEL_CROSS;
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_EAST]  = SDL_GAMEPAD_BUTTON_LABEL_CIRCLE;
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_WEST]  = SDL_GAMEPAD_BUTTON_LABEL_SQUARE;
    cfg.buttonLabels[SDL_GAMEPAD_BUTTON_NORTH] = SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::A), CNA::Input::GamePadButtonLabelEXT::Cross);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::B), CNA::Input::GamePadButtonLabelEXT::Circle);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::X), CNA::Input::GamePadButtonLabelEXT::Square);
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::Y), CNA::Input::GamePadButtonLabelEXT::Triangle);
}

// N-011(c): a non-physical Buttons value (a thumbstick direction) has no SDL button, so the bridge
// short-circuits to Unknown without consulting the device; a connected physical button with no
// configured label also reports Unknown; and a disconnected slot reports Unknown.
TEST_F(FakeGamepadTest, ButtonLabelIsUnknownForNonPhysicalUnlabeledOrDisconnected)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad(); // no buttonLabels configured
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::LeftThumbstickUp),
              CNA::Input::GamePadButtonLabelEXT::Unknown) << "no SDL face button for a stick direction";
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::One, Buttons::A),
              CNA::Input::GamePadButtonLabelEXT::Unknown) << "physical button with no configured label";
    EXPECT_EQ(GamePad::GetButtonLabelEXT(PlayerIndex::Four, Buttons::A),
              CNA::Input::GamePadButtonLabelEXT::Unknown) << "disconnected slot";
}

// --- gamepad metadata extension (input_noxna.md N-010) via GamePad::Get{Name,Path,Serial,...}EXT ---

// N-010(a): each metadata getter forwards the device's canned value through the seam.
TEST_F(FakeGamepadTest, MetadataForwardsDeviceValues)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.name = "Xbox Wireless Controller";
    cfg.path = "/dev/input/event7";
    cfg.serial = "ABCDEF0123";
    cfg.firmwareVersion = 0x0105;
    cfg.steamHandle = 0xDEADBEEFULL;
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetNameEXT(PlayerIndex::One), "Xbox Wireless Controller");
    EXPECT_EQ(GamePad::GetPathEXT(PlayerIndex::One), "/dev/input/event7");
    EXPECT_EQ(GamePad::GetSerialEXT(PlayerIndex::One), "ABCDEF0123");
    EXPECT_EQ(GamePad::GetFirmwareVersionEXT(PlayerIndex::One), 0x0105);
    EXPECT_EQ(GamePad::GetSteamHandleEXT(PlayerIndex::One), 0xDEADBEEFULL);
}

// N-010(b): a disconnected slot returns empty strings and zero without reaching the backend.
TEST_F(FakeGamepadTest, MetadataIsEmptyForDisconnectedSlot)
{
    EXPECT_EQ(GamePad::GetNameEXT(PlayerIndex::Three), "");
    EXPECT_EQ(GamePad::GetPathEXT(PlayerIndex::Three), "");
    EXPECT_EQ(GamePad::GetSerialEXT(PlayerIndex::Three), "");
    EXPECT_EQ(GamePad::GetFirmwareVersionEXT(PlayerIndex::Three), 0);
    EXPECT_EQ(GamePad::GetSteamHandleEXT(PlayerIndex::Three), 0ULL);
}

// --- gamepad connection-state extension (input_noxna.md N-010b) via GamePad::GetConnectionStateEXT ---

// N-010b(a): the SDL wired/wireless/unknown/invalid states map onto the 3-value EXT enum.
TEST_F(FakeGamepadTest, ConnectionStateMapsWiredWirelessAndUnknown)
{
    struct Case { SDL_JoystickConnectionState sdl; CNA::Input::GamePadConnectionStateEXT ext; };
    const Case cases[] = {
        {SDL_JOYSTICK_CONNECTION_WIRED,    CNA::Input::GamePadConnectionStateEXT::Wired},
        {SDL_JOYSTICK_CONNECTION_WIRELESS, CNA::Input::GamePadConnectionStateEXT::Wireless},
        {SDL_JOYSTICK_CONNECTION_UNKNOWN,  CNA::Input::GamePadConnectionStateEXT::Unknown},
        {SDL_JOYSTICK_CONNECTION_INVALID,  CNA::Input::GamePadConnectionStateEXT::Unknown},
    };
    for (const Case& c : cases)
    {
        FakeGamepadConfig cfg = FullyFeaturedGamepad();
        cfg.connectionState = c.sdl;
        fake.Register(10, cfg);
        SdlInputBridge::ProcessEvent(addedEvent(10));

        EXPECT_EQ(GamePad::GetConnectionStateEXT(PlayerIndex::One), c.ext);

        SdlInputBridge::ProcessEvent(removedEvent(10)); // reset the slot for the next case
    }
}

// N-010b(b): a disconnected slot reports Unknown without reaching the backend.
TEST_F(FakeGamepadTest, ConnectionStateIsUnknownForDisconnectedSlot)
{
    EXPECT_EQ(GamePad::GetConnectionStateEXT(PlayerIndex::Two),
              CNA::Input::GamePadConnectionStateEXT::Unknown);
}

// --- gamepad touchpad fingers extension (input_noxna.md N-008) via GamePad::GetTouchpad*EXT ---

using CNA::Internal::Input::test_support::FakeTouchpadFinger;

// N-008(a): touchpad + finger counts and a finger read forward through the seam.
TEST_F(FakeGamepadTest, TouchpadFingerReportsCountsAndContact)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.numTouchpads = 1;
    cfg.touchpadFingers = {{
        FakeTouchpadFinger{true, 0.25f, 0.75f, 0.5f},   // finger 0 down
        FakeTouchpadFinger{false, 0.0f, 0.0f, 0.0f},    // finger 1 up
    }};
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetTouchpadCountEXT(PlayerIndex::One), 1);
    EXPECT_EQ(GamePad::GetTouchpadFingerCountEXT(PlayerIndex::One, 0), 2);

    bool down = false;
    float x = -1.0f, y = -1.0f, pressure = -1.0f;
    ASSERT_TRUE(GamePad::GetTouchpadFingerEXT(PlayerIndex::One, 0, 0, down, x, y, pressure));
    EXPECT_TRUE(down);
    EXPECT_FLOAT_EQ(x, 0.25f);
    EXPECT_FLOAT_EQ(y, 0.75f);
    EXPECT_FLOAT_EQ(pressure, 0.5f);

    ASSERT_TRUE(GamePad::GetTouchpadFingerEXT(PlayerIndex::One, 0, 1, down, x, y, pressure));
    EXPECT_FALSE(down);
}

// N-008(b): out-of-range touchpad/finger indices report no data (and reset the out-params).
TEST_F(FakeGamepadTest, TouchpadFingerOutOfRangeReturnsFalse)
{
    FakeGamepadConfig cfg = FullyFeaturedGamepad();
    cfg.numTouchpads = 1;
    cfg.touchpadFingers = {{FakeTouchpadFinger{true, 0.1f, 0.2f, 0.3f}}};
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(GamePad::GetTouchpadFingerCountEXT(PlayerIndex::One, 5), 0) << "no such touchpad";

    bool down = true;
    float x = 9.0f, y = 9.0f, pressure = 9.0f;
    EXPECT_FALSE(GamePad::GetTouchpadFingerEXT(PlayerIndex::One, 0, 3, down, x, y, pressure)) << "no such finger";
    EXPECT_FALSE(down);
    EXPECT_FLOAT_EQ(x, 0.0f);
    EXPECT_FLOAT_EQ(pressure, 0.0f);
}

// N-008(c): a disconnected slot reports zero counts and no finger data.
TEST_F(FakeGamepadTest, TouchpadFingerIsEmptyForDisconnectedSlot)
{
    EXPECT_EQ(GamePad::GetTouchpadCountEXT(PlayerIndex::Three), 0);
    EXPECT_EQ(GamePad::GetTouchpadFingerCountEXT(PlayerIndex::Three, 0), 0);

    bool down = true;
    float x = 1.0f, y = 1.0f, pressure = 1.0f;
    EXPECT_FALSE(GamePad::GetTouchpadFingerEXT(PlayerIndex::Three, 0, 0, down, x, y, pressure));
    EXPECT_FALSE(down);
}

// --- sensor enable/disable (P4-017) ---

// P4-017(c): reading a sensor lazily enables it exactly once. The first GetGyroEXT enables SDL_SENSOR_GYRO;
// a second read sees it already enabled and does not re-enable. Reading the accelerometer enables a
// second, distinct sensor. This proves the enable-once guard in read_gamepad_sensor.
TEST_F(FakeGamepadTest, ReadingSensorEnablesItOnceThenReadsWithoutReEnabling)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    Vector3 gyro;
    ASSERT_TRUE(GamePad::GetGyroEXT(PlayerIndex::One, gyro));
    EXPECT_EQ(fake.setSensorEnabledCalls, 1) << "first gyro read enables the gyro sensor";

    ASSERT_TRUE(GamePad::GetGyroEXT(PlayerIndex::One, gyro));
    EXPECT_EQ(fake.setSensorEnabledCalls, 1) << "already-enabled sensor must not be re-enabled";

    Vector3 accel;
    ASSERT_TRUE(GamePad::GetAccelerometerEXT(PlayerIndex::One, accel));
    EXPECT_EQ(fake.setSensorEnabledCalls, 2) << "accelerometer is a separate sensor, enabled once";
}

// --- slot lifecycle: reuse + stale-state clear (P4-009) ---

// P4-009(d): a slot freed by disconnect is reused by the next connect (lowest free slot first), rather
// than advancing to a higher player index.
TEST_F(FakeGamepadTest, FreedSlotIsReusedByNextConnect)
{
    fake.Register(10, FullyFeaturedGamepad());
    fake.Register(20, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10)); // -> One
    SdlInputBridge::ProcessEvent(removedEvent(10)); // One freed

    SdlInputBridge::ProcessEvent(addedEvent(20)); // must reuse One, not advance to Two
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::Two).getIsConnectedProperty());
    EXPECT_EQ(fake.openCount, 2);
    EXPECT_EQ(fake.closeCount, 1);
}

// P4-009(f): disconnect wipes the slot's accumulated button/axis state, so a later device in the same
// slot never inherits a stale "button held" from its predecessor.
TEST_F(FakeGamepadTest, StaleButtonStateIsClearedOnDisconnect)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    SdlInputBridge::ProcessEvent(buttonEvent(true, 10, SDL_GAMEPAD_BUTTON_SOUTH)); // A held
    ASSERT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonDown(Buttons::A));

    SdlInputBridge::ProcessEvent(removedEvent(10));
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());

    fake.Register(20, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(20)); // reuses One
    EXPECT_FALSE(GamePad::GetState(PlayerIndex::One).IsButtonDown(Buttons::A))
        << "the new device must not inherit the old device's held button";
}

// --- packet number: no bump on repeated identical events (P4-013) ---

// P4-013(c)+(d): the packet number advances only on a real state change. Repeated identical button
// events (already-down / already-up) leave it unchanged.
TEST_F(FakeGamepadTest, PacketNumberIsStableAcrossRepeatedIdenticalButtonEvents)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10)); // connect bumps packet once

    const int afterConnect = GamePad::GetState(PlayerIndex::One).getPacketNumberProperty();

    SdlInputBridge::ProcessEvent(buttonEvent(true, 10, SDL_GAMEPAD_BUTTON_SOUTH));
    const int afterDown = GamePad::GetState(PlayerIndex::One).getPacketNumberProperty();
    EXPECT_GT(afterDown, afterConnect) << "a real press advances the packet";

    SdlInputBridge::ProcessEvent(buttonEvent(true, 10, SDL_GAMEPAD_BUTTON_SOUTH)); // already down
    SdlInputBridge::ProcessEvent(buttonEvent(true, 10, SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_EQ(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), afterDown)
        << "redundant press events must not advance the packet";

    SdlInputBridge::ProcessEvent(buttonEvent(false, 10, SDL_GAMEPAD_BUTTON_SOUTH));
    const int afterUp = GamePad::GetState(PlayerIndex::One).getPacketNumberProperty();
    EXPECT_GT(afterUp, afterDown);
    SdlInputBridge::ProcessEvent(buttonEvent(false, 10, SDL_GAMEPAD_BUTTON_SOUTH)); // already up
    EXPECT_EQ(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), afterUp)
        << "redundant release events must not advance the packet";
}

// P4-013(d): axis jitter — re-sending the identical raw axis value does not advance the packet, but a
// genuinely different value does.
TEST_F(FakeGamepadTest, PacketNumberIsStableAcrossRepeatedIdenticalAxisEvents)
{
    fake.Register(10, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTX, 16000));
    const int afterFirst = GamePad::GetState(PlayerIndex::One).getPacketNumberProperty();

    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTX, 16000)); // identical
    EXPECT_EQ(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), afterFirst)
        << "an unchanged axis value must not advance the packet";

    SdlInputBridge::ProcessEvent(axisEvent(10, SDL_GAMEPAD_AXIS_LEFTX, 20000)); // moved
    EXPECT_GT(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), afterFirst)
        << "a changed axis value advances the packet";
}

// --- extended GamePadType mapping (P4-019) ---

// P4-019(b): the SDL joystick-type -> XNA GamePadType map beyond the four already covered, plus the
// safe fallback: unknown/unmapped SDL types resolve to GamePadType::Unknown (FNA does the same, and
// SDL3-only THROTTLE has no XNA equivalent). Uses a single slot, disconnecting between each case.
TEST_F(FakeGamepadTest, ExtendedSdlJoystickTypesMapToXnaGamePadType)
{
    struct Case { SDL_JoystickType sdl; GamePadType expected; const char* name; };
    const Case cases[] = {
        {SDL_JOYSTICK_TYPE_DANCE_PAD,    GamePadType::DancePad,     "DancePad"},
        {SDL_JOYSTICK_TYPE_GUITAR,       GamePadType::Guitar,       "Guitar"},
        {SDL_JOYSTICK_TYPE_DRUM_KIT,     GamePadType::DrumKit,      "DrumKit"},
        {SDL_JOYSTICK_TYPE_ARCADE_PAD,   GamePadType::BigButtonPad, "ArcadePad->BigButtonPad"},
        {SDL_JOYSTICK_TYPE_THROTTLE,     GamePadType::Unknown,      "Throttle->Unknown"},
        {SDL_JOYSTICK_TYPE_UNKNOWN,      GamePadType::Unknown,      "Unknown"},
    };

    SDL_JoystickID id = 10;
    for (const Case& c : cases)
    {
        FakeGamepadConfig cfg = FullyFeaturedGamepad();
        cfg.joystickType = c.sdl;
        fake.Register(id, cfg);
        SdlInputBridge::ProcessEvent(addedEvent(id));
        EXPECT_EQ(SdlInputBridge::GetCapabilities(PlayerIndex::One).getGamePadTypeProperty(), c.expected) << c.name;
        SdlInputBridge::ProcessEvent(removedEvent(id));
        ++id;
    }
}

// --- gamepad reset (P4-020) ---

// P4-020: ResetAllForTests clears every gamepad slot and its packet number, and restores the real
// backend (so the fake's slot maps do not leak into the next test). Reinstalling the fake and adding a
// fresh pad shows the packet counter starts clean (no cross-test leak).
TEST_F(FakeGamepadTest, ResetClearsAllGamepadSlotsAndPacketNumbers)
{
    fake.Register(10, FullyFeaturedGamepad());
    fake.Register(20, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(10)); // One
    SdlInputBridge::ProcessEvent(addedEvent(20)); // Two
    SdlInputBridge::ProcessEvent(buttonEvent(true, 10, SDL_GAMEPAD_BUTTON_SOUTH));
    SdlInputBridge::ProcessEvent(axisEvent(20, SDL_GAMEPAD_AXIS_LEFTX, 20000));
    ASSERT_TRUE(GamePad::GetState(PlayerIndex::One).getIsConnectedProperty());
    ASSERT_GT(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), 0);
    ASSERT_GT(GamePad::GetState(PlayerIndex::Two).getPacketNumberProperty(), 0);

    InputManager::ResetAllForTests(); // uninstalls the fake + clears all slots

    for (PlayerIndex p : {PlayerIndex::One, PlayerIndex::Two, PlayerIndex::Three, PlayerIndex::Four})
    {
        const auto s = GamePad::GetState(p);
        EXPECT_FALSE(s.getIsConnectedProperty());
        EXPECT_EQ(s.getPacketNumberProperty(), 0) << "no packet leak after reset";
    }

    // Reinstall the fake and connect a fresh pad: the packet counter restarts, proving no leak.
    SetSdlGamepadBackendForTests(&fake);
    fake.Register(30, FullyFeaturedGamepad());
    SdlInputBridge::ProcessEvent(addedEvent(30));
    EXPECT_EQ(GamePad::GetState(PlayerIndex::One).getPacketNumberProperty(), 1)
        << "a fresh connection after reset starts its packet count at 1";
}

// --- startup gamepad-subsystem init (the invariant Game::DoInitialize establishes) ---

TEST(SdlGamepadSubsystemInit, EnsureIsIdempotentAndInitializesSubsystem)
{
    // Both the explicit startup call (Game::DoInitialize, before the first event pump/Update) and
    // the defensive lazy fallback (SdlInputBridge::ProcessEvent) use this. After it runs, the SDL
    // gamepad subsystem must be initialized (so SDL enumerates already-connected pads), and calling
    // it again must be safe (idempotent — SDL ref-counts subsystem init).
    SdlInputBridge::EnsureGamepadSubsystemInitialized();
    EXPECT_TRUE((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0u);

    SdlInputBridge::EnsureGamepadSubsystemInitialized();
    EXPECT_TRUE((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0u);
}

// P8-002: the shutdown counterpart to EnsureGamepadSubsystemInitialized (Game::Dispose calls this,
// mirroring FNA's ProgramExit which quits SDL_INIT_VIDEO | SDL_INIT_GAMEPAD together). Must actually
// quit the subsystem, and must be safe to call when the subsystem was never initialized or has
// already been shut down (SDL_QuitSubSystem is a documented no-op in both cases).
TEST(SdlGamepadSubsystemInit, ShutdownQuitsSubsystemAndIsSafeToCallRepeatedly)
{
    SdlInputBridge::EnsureGamepadSubsystemInitialized();
    ASSERT_TRUE((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0u);

    SdlInputBridge::ShutdownGamepadSubsystem();
    EXPECT_FALSE((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0u);

    // Safe to call again with the subsystem already shut down.
    EXPECT_NO_THROW(SdlInputBridge::ShutdownGamepadSubsystem());

    // Re-initialization still works afterward (round-trips cleanly).
    SdlInputBridge::EnsureGamepadSubsystemInitialized();
    EXPECT_TRUE((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0u);
}
