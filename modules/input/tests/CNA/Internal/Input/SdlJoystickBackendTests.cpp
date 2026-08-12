// SPDX-License-Identifier: MS-PL
//
// input_noxna.md N-007: raw-joystick tests using the injectable fake backend (no real hardware).
// Covers hot-plug (open/close, duplicate-add, unknown-remove), enumeration, capabilities, and raw
// axis/button/hat/trackball state — driving the real SdlInputBridge event path with a fake
// ISdlJoystickBackend swapped in.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Input/JoystickCapabilities.hpp"
#include "CNA/Input/JoystickState.hpp"
#include "CNA/Input/Joysticks.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/SdlJoystickBackend.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"

#include "FakeSdlJoystickBackend.hpp"

using CNA::Input::JoystickCapabilitiesEXT;
using CNA::Input::JoystickHatPositionEXT;
using CNA::Input::JoystickInfoEXT;
using CNA::Input::JoystickStateEXT;
using CNA::Input::JoystickTypeEXT;
using CNA::Input::Joysticks;
using CNA::Input::PowerStateEXT;
using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Internal::Input::SdlInputBridge;
using CNA::Internal::Input::SetSdlJoystickBackendForTests;
using CNA::Internal::Input::test_support::FakeJoystickConfig;
using CNA::Internal::Input::test_support::FakeSdlJoystickBackend;
using Microsoft::Xna::Framework::Point;

namespace
{
    SDL_Event addedEvent(SDL_JoystickID which)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_JOYSTICK_ADDED;
        e.jdevice.which = which;
        return e;
    }
    SDL_Event removedEvent(SDL_JoystickID which)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_JOYSTICK_REMOVED;
        e.jdevice.which = which;
        return e;
    }

    struct FakeJoystickTest : ::testing::Test
    {
        FakeSdlJoystickBackend fake;

        void SetUp() override
        {
            InputManager::ResetAllForTests();     // restores the real backend + clears the opened map
            Joysticks::ResetForTests();
            SetSdlJoystickBackendForTests(&fake);  // then install the fake for this test
        }
        void TearDown() override
        {
            SetSdlJoystickBackendForTests(nullptr); // restore real BEFORE fake is destroyed
            InputManager::ResetAllForTests();
            Joysticks::ResetForTests();
        }
    };

    FakeJoystickConfig FlightStickConfig()
    {
        FakeJoystickConfig cfg;
        cfg.name = "Test Flight Stick";
        cfg.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
        cfg.guid = "0123456789abcdef0123456789abcdef";
        cfg.axes = {100, -200, 32767, -32768};
        cfg.buttons = {true, false, true};
        cfg.hats = {SDL_HAT_CENTERED, SDL_HAT_LEFTUP};
        cfg.balls = {{3, -4}};
        cfg.powerState = SDL_POWERSTATE_ON_BATTERY;
        cfg.powerPercent = 42;
        return cfg;
    }
}

// --- hot-plug ---

TEST_F(FakeJoystickTest, AddedOpensHandleAndAppearsInEnumeration)
{
    fake.Register(10, FlightStickConfig());
    EXPECT_TRUE(Joysticks::GetJoysticksEXT().empty());

    PlatformInputBridge::ProcessEvent(CNA::Platform::DeviceEvent{
        10, CNA::Platform::InputDeviceKind::Joystick, true});

    EXPECT_EQ(fake.openCount, 1);
    const auto list = Joysticks::GetJoysticksEXT();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0], (JoystickInfoEXT{10, "Test Flight Stick", JoystickTypeEXT::FlightStick}));
}

TEST_F(FakeJoystickTest, DuplicateAddDoesNotOpenSecondHandle)
{
    fake.Register(10, FlightStickConfig());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    SdlInputBridge::ProcessEvent(addedEvent(10)); // duplicate

    EXPECT_EQ(fake.openCount, 1) << "duplicate add must not open a second handle";
    EXPECT_EQ(Joysticks::GetJoysticksEXT().size(), 1u);
}

TEST_F(FakeJoystickTest, UnknownRemoveIsIgnored)
{
    SdlInputBridge::ProcessEvent(removedEvent(999)); // never added
    EXPECT_EQ(fake.closeCount, 0);
    EXPECT_TRUE(Joysticks::GetJoysticksEXT().empty());
}

TEST_F(FakeJoystickTest, RemoveClosesHandleAndDropsFromEnumeration)
{
    fake.Register(10, FlightStickConfig());
    fake.Register(20, FlightStickConfig());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    SdlInputBridge::ProcessEvent(addedEvent(20));

    SdlInputBridge::ProcessEvent(removedEvent(10));

    EXPECT_EQ(fake.closeCount, 1);
    EXPECT_EQ(fake.lastClosedId, 10u);
    const auto list = Joysticks::GetJoysticksEXT();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, 20u);
}

TEST_F(FakeJoystickTest, AddEventWhoseOpenFailsIsIgnored)
{
    FakeJoystickConfig cfg = FlightStickConfig();
    cfg.openFails = true;
    fake.Register(10, cfg);

    SdlInputBridge::ProcessEvent(addedEvent(10));

    EXPECT_EQ(fake.openCount, 0);
    EXPECT_TRUE(Joysticks::GetJoysticksEXT().empty());
}

// --- hot-plug events ---

TEST_F(FakeJoystickTest, ConnectedAndDisconnectedEventsFireWithDeviceId)
{
    std::vector<std::uint32_t> connected;
    std::vector<std::uint32_t> disconnected;
    Joysticks::ConnectedEXT += [&](std::uint32_t id) { connected.push_back(id); };
    Joysticks::DisconnectedEXT += [&](std::uint32_t id) { disconnected.push_back(id); };

    fake.Register(10, FlightStickConfig());
    SdlInputBridge::ProcessEvent(addedEvent(10));
    SdlInputBridge::ProcessEvent(removedEvent(10));

    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0], 10u);
    ASSERT_EQ(disconnected.size(), 1u);
    EXPECT_EQ(disconnected[0], 10u);
}

// --- capabilities ---

TEST_F(FakeJoystickTest, CapabilitiesReportsCountsTypeNameGuidAndPower)
{
    fake.Register(10, FlightStickConfig());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const JoystickCapabilitiesEXT caps = Joysticks::GetCapabilitiesEXT(10);

    EXPECT_TRUE(caps.isConnected);
    EXPECT_EQ(caps.axisCount, 4);
    EXPECT_EQ(caps.buttonCount, 3);
    EXPECT_EQ(caps.hatCount, 2);
    EXPECT_EQ(caps.ballCount, 1);
    EXPECT_EQ(caps.type, JoystickTypeEXT::FlightStick);
    EXPECT_EQ(caps.name, "Test Flight Stick");
    EXPECT_EQ(caps.guid, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(caps.powerState, PowerStateEXT::OnBattery);
    EXPECT_EQ(caps.powerPercent, 42);
}

TEST_F(FakeJoystickTest, CapabilitiesIsDefaultWhenDisconnected)
{
    const JoystickCapabilitiesEXT caps = Joysticks::GetCapabilitiesEXT(12345);
    EXPECT_EQ(caps, JoystickCapabilitiesEXT{});
    EXPECT_FALSE(caps.isConnected);
}

// --- state ---

TEST_F(FakeJoystickTest, StateReportsAxesButtonsHatsAndBalls)
{
    fake.Register(10, FlightStickConfig());
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const JoystickStateEXT state = Joysticks::GetStateEXT(10);

    ASSERT_EQ(state.axes.size(), 4u);
    EXPECT_EQ(state.axes[0], 100);
    EXPECT_EQ(state.axes[1], -200);
    EXPECT_EQ(state.axes[2], 32767);
    EXPECT_EQ(state.axes[3], -32768);

    ASSERT_EQ(state.buttons.size(), 3u);
    EXPECT_TRUE(state.buttons[0]);
    EXPECT_FALSE(state.buttons[1]);
    EXPECT_TRUE(state.buttons[2]);

    ASSERT_EQ(state.hats.size(), 2u);
    EXPECT_EQ(state.hats[0], JoystickHatPositionEXT::Centered);
    EXPECT_EQ(state.hats[1], JoystickHatPositionEXT::LeftUp);

    ASSERT_EQ(state.balls.size(), 1u);
    EXPECT_EQ(state.balls[0], Point(3, -4));
}

TEST_F(FakeJoystickTest, StateIsAllEmptyWhenDisconnected)
{
    const JoystickStateEXT state = Joysticks::GetStateEXT(12345);
    EXPECT_EQ(state, JoystickStateEXT{});
    EXPECT_TRUE(state.axes.empty());
    EXPECT_TRUE(state.buttons.empty());
    EXPECT_TRUE(state.hats.empty());
    EXPECT_TRUE(state.balls.empty());
}

// --- hat mapping exhaustiveness ---

TEST_F(FakeJoystickTest, AllNineHatPositionsMapCorrectly)
{
    FakeJoystickConfig cfg;
    cfg.hats = {
        SDL_HAT_CENTERED, SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT,
        SDL_HAT_RIGHTUP, SDL_HAT_RIGHTDOWN, SDL_HAT_LEFTUP, SDL_HAT_LEFTDOWN,
    };
    fake.Register(10, cfg);
    SdlInputBridge::ProcessEvent(addedEvent(10));

    const JoystickStateEXT state = Joysticks::GetStateEXT(10);

    ASSERT_EQ(state.hats.size(), 9u);
    EXPECT_EQ(state.hats[0], JoystickHatPositionEXT::Centered);
    EXPECT_EQ(state.hats[1], JoystickHatPositionEXT::Up);
    EXPECT_EQ(state.hats[2], JoystickHatPositionEXT::Right);
    EXPECT_EQ(state.hats[3], JoystickHatPositionEXT::Down);
    EXPECT_EQ(state.hats[4], JoystickHatPositionEXT::Left);
    EXPECT_EQ(state.hats[5], JoystickHatPositionEXT::RightUp);
    EXPECT_EQ(state.hats[6], JoystickHatPositionEXT::RightDown);
    EXPECT_EQ(state.hats[7], JoystickHatPositionEXT::LeftUp);
    EXPECT_EQ(state.hats[8], JoystickHatPositionEXT::LeftDown);
}

// --- joystick type mapping exhaustiveness ---

TEST_F(FakeJoystickTest, AllJoystickTypesMapCorrectly)
{
    struct Case { SDL_JoystickID id; SDL_JoystickType sdl; JoystickTypeEXT ext; };
    const Case cases[] = {
        {1, SDL_JOYSTICK_TYPE_UNKNOWN, JoystickTypeEXT::Unknown},
        {2, SDL_JOYSTICK_TYPE_GAMEPAD, JoystickTypeEXT::Gamepad},
        {3, SDL_JOYSTICK_TYPE_WHEEL, JoystickTypeEXT::Wheel},
        {4, SDL_JOYSTICK_TYPE_ARCADE_STICK, JoystickTypeEXT::ArcadeStick},
        {5, SDL_JOYSTICK_TYPE_FLIGHT_STICK, JoystickTypeEXT::FlightStick},
        {6, SDL_JOYSTICK_TYPE_DANCE_PAD, JoystickTypeEXT::DancePad},
        {7, SDL_JOYSTICK_TYPE_GUITAR, JoystickTypeEXT::Guitar},
        {8, SDL_JOYSTICK_TYPE_DRUM_KIT, JoystickTypeEXT::DrumKit},
        {9, SDL_JOYSTICK_TYPE_ARCADE_PAD, JoystickTypeEXT::ArcadePad},
        {10, SDL_JOYSTICK_TYPE_THROTTLE, JoystickTypeEXT::Throttle},
    };

    for (const auto& c : cases)
    {
        FakeJoystickConfig cfg;
        cfg.type = c.sdl;
        fake.Register(c.id, cfg);
        SdlInputBridge::ProcessEvent(addedEvent(c.id));
        EXPECT_EQ(Joysticks::GetCapabilitiesEXT(c.id).type, c.ext) << "sdl type " << c.sdl;
    }
}

// --- descriptor equality (no bridge/fake needed) ---

TEST(JoystickInfoEXTTest, EqualityComparesIdNameAndType)
{
    EXPECT_EQ((JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}),
              (JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}));
    EXPECT_NE((JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}),
              (JoystickInfoEXT{1, "a", JoystickTypeEXT::Throttle}));
    EXPECT_NE((JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}),
              (JoystickInfoEXT{2, "a", JoystickTypeEXT::Wheel}));
}

TEST(JoystickCapabilitiesEXTTest, EqualityComparesEveryField)
{
    JoystickCapabilitiesEXT a;
    a.isConnected = true;
    a.axisCount = 4;
    a.buttonCount = 2;
    a.hatCount = 1;
    a.ballCount = 1;
    a.type = JoystickTypeEXT::Wheel;
    a.name = "Wheel";
    a.guid = "abc";
    a.powerState = PowerStateEXT::Charging;
    a.powerPercent = 50;

    JoystickCapabilitiesEXT b = a;
    EXPECT_EQ(a, b);

    b.powerPercent = 51;
    EXPECT_NE(a, b);
}

TEST(JoystickStateEXTTest, EqualityComparesEveryField)
{
    JoystickStateEXT a;
    a.axes = {1, -2};
    a.buttons = {true, false};
    a.hats = {JoystickHatPositionEXT::Up};
    a.balls = {Point(1, 2)};

    JoystickStateEXT b = a;
    EXPECT_EQ(a, b);

    b.axes[0] = 5;
    EXPECT_NE(a, b);
}
