// SPDX-License-Identifier: MS-PL
//
// Phase I15: device-level SDL gamepad tests using the injectable fake backend (no real hardware).
// Covers hot-plug/slot assignment, FNA_GAMEPAD_NUM_GAMEPADS, button/axis mapping, capabilities,
// rumble/LED/sensor support, and GUID formatting — driving the real SdlInputBridge event path with
// a fake ISdlGamepadBackend swapped in.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlGamepadBackend.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"

#include "FakeSdlGamepadBackend.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using CNA::Internal::Input::SetSdlGamepadBackendForTests;
using CNA::Internal::Input::test_support::FakeGamepadConfig;
using CNA::Internal::Input::test_support::FakeSdlGamepadBackend;
using CNA::Internal::Input::test_support::FullyFeaturedGamepad;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Input::Buttons;
using Microsoft::Xna::Framework::Input::GamePad;

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
