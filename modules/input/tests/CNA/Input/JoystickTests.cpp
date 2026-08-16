// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Input/JoystickCapabilities.hpp"
#include "CNA/Input/JoystickState.hpp"
#include "CNA/Input/Joysticks.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Platform/CannedJoystick.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"

using CNA::Input::JoystickCapabilitiesEXT;
using CNA::Input::JoystickHatPositionEXT;
using CNA::Input::JoystickInfoEXT;
using CNA::Input::JoystickStateEXT;
using CNA::Input::JoystickTypeEXT;
using CNA::Input::Joysticks;
using CNA::Input::PowerStateEXT;
using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Platform::DeviceEvent;
using CNA::Platform::InputDeviceKind;
using CNA::Platform::JoystickBallDelta;
using CNA::Platform::JoystickCapabilities;
using CNA::Platform::JoystickHat;
using CNA::Platform::JoystickInfo;
using CNA::Platform::JoystickKind;
using CNA::Platform::JoystickPowerState;
using CNA::Platform::JoystickSnapshot;
using CNA::Platform::Testing::CannedJoystickPlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;
using Microsoft::Xna::Framework::Point;

namespace
{
    JoystickCapabilities FlightStickCapabilities()
    {
        JoystickCapabilities capabilities;
        capabilities.axisCount = 4;
        capabilities.buttonCount = 3;
        capabilities.hatCount = 2;
        capabilities.ballCount = 1;
        capabilities.guid = "0123456789abcdef0123456789abcdef";
        capabilities.powerState = JoystickPowerState::OnBattery;
        capabilities.powerPercent = 42;
        return capabilities;
    }

    JoystickSnapshot FlightStickState()
    {
        JoystickSnapshot state;
        state.axes = {100, -200, 32767, -32768};
        state.buttons = {true, false, true};
        state.hats = {JoystickHat::Centered, JoystickHat::LeftUp};
        state.balls = {{3, -4}};
        return state;
    }

    struct CannedJoystickTest : ::testing::Test
    {
        CannedJoystickPlatform platform;
        ScopedCurrentPlatform installed{platform};

        void SetUp() override
        {
            InputManager::ResetAllForTests();
            Joysticks::ResetForTests();
        }

        void TearDown() override
        {
            InputManager::ResetAllForTests();
            Joysticks::ResetForTests();
        }

        void ConnectFlightStick(const std::uint32_t id = 10)
        {
            platform.joystick.Connect(
                JoystickInfo{id, "Test Flight Stick", JoystickKind::FlightStick},
                FlightStickCapabilities(), FlightStickState());
        }

        static void Added(const std::uint32_t id)
        {
            PlatformInputBridge::ProcessEvent(DeviceEvent{id, InputDeviceKind::Joystick, true});
        }

        static void Removed(const std::uint32_t id)
        {
            PlatformInputBridge::ProcessEvent(DeviceEvent{id, InputDeviceKind::Joystick, false});
        }
    };
}

TEST_F(CannedJoystickTest, ConnectedDeviceAppearsInDeterministicEnumeration)
{
    ConnectFlightStick();
    Added(10);

    const auto list = Joysticks::GetJoysticksEXT();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0], (JoystickInfoEXT{10, "Test Flight Stick", JoystickTypeEXT::FlightStick}));
}

TEST_F(CannedJoystickTest, DuplicateAddAndUnknownRemoveDoNotDuplicatePublicEvents)
{
    std::vector<std::uint32_t> connected;
    std::vector<std::uint32_t> disconnected;
    Joysticks::ConnectedEXT += [&](const std::uint32_t id) { connected.push_back(id); };
    Joysticks::DisconnectedEXT += [&](const std::uint32_t id) { disconnected.push_back(id); };

    ConnectFlightStick();
    Added(10);
    Added(10);
    Removed(999);

    EXPECT_EQ(connected, std::vector<std::uint32_t>{10});
    EXPECT_TRUE(disconnected.empty());
}

TEST_F(CannedJoystickTest, FailedConnectionIsNotAnnounced)
{
    int connected = 0;
    Joysticks::ConnectedEXT += [&](std::uint32_t) { ++connected; };

    Added(10); // the platform service refused/opened no such device

    EXPECT_EQ(connected, 0);
    EXPECT_TRUE(Joysticks::GetJoysticksEXT().empty());
}

TEST_F(CannedJoystickTest, RemovalDropsTheDeviceAndFiresOnce)
{
    std::vector<std::uint32_t> disconnected;
    Joysticks::DisconnectedEXT += [&](const std::uint32_t id) { disconnected.push_back(id); };
    ConnectFlightStick();
    Added(10);

    platform.joystick.Disconnect(10);
    Removed(10);
    Removed(10);

    EXPECT_TRUE(Joysticks::GetJoysticksEXT().empty());
    EXPECT_EQ(disconnected, std::vector<std::uint32_t>{10});
}

TEST_F(CannedJoystickTest, CapabilitiesMapEveryField)
{
    ConnectFlightStick();

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

TEST_F(CannedJoystickTest, UnknownDeviceReturnsDisconnectedDefaults)
{
    EXPECT_EQ(Joysticks::GetCapabilitiesEXT(12345), JoystickCapabilitiesEXT{});
    EXPECT_EQ(Joysticks::GetStateEXT(12345), JoystickStateEXT{});
}

TEST_F(CannedJoystickTest, StateMapsAxesButtonsHatsAndTrackballs)
{
    ConnectFlightStick();
    const JoystickStateEXT state = Joysticks::GetStateEXT(10);

    EXPECT_EQ(state.axes, (std::vector<std::int16_t>{100, -200, 32767, -32768}));
    EXPECT_EQ(state.buttons, (std::vector<bool>{true, false, true}));
    EXPECT_EQ(state.hats, (std::vector<JoystickHatPositionEXT>{
                              JoystickHatPositionEXT::Centered,
                              JoystickHatPositionEXT::LeftUp}));
    EXPECT_EQ(state.balls, (std::vector<Point>{Point(3, -4)}));
}

TEST_F(CannedJoystickTest, PendingStateAdvancesOnlyWhenTheServiceUpdates)
{
    ConnectFlightStick();
    JoystickSnapshot next = FlightStickState();
    next.axes[0] = 777;
    next.balls[0] = JoystickBallDelta{-8, 9};
    platform.joystick.SetPending(10, next);

    EXPECT_EQ(Joysticks::GetStateEXT(10).axes[0], 100);
    platform.joystick.Update();
    EXPECT_EQ(Joysticks::GetStateEXT(10).axes[0], 777);
    EXPECT_EQ(Joysticks::GetStateEXT(10).balls[0], Point(-8, 9));
}

TEST_F(CannedJoystickTest, AllNineHatPositionsMapExactly)
{
    JoystickCapabilities caps;
    caps.hatCount = 9;
    JoystickSnapshot state;
    state.hats = {JoystickHat::Centered, JoystickHat::Up, JoystickHat::Right,
                  JoystickHat::Down, JoystickHat::Left, JoystickHat::RightUp,
                  JoystickHat::RightDown, JoystickHat::LeftUp, JoystickHat::LeftDown};
    platform.joystick.Connect({10, "Hat", JoystickKind::Unknown}, caps, state);

    EXPECT_EQ(Joysticks::GetStateEXT(10).hats,
              (std::vector<JoystickHatPositionEXT>{
                  JoystickHatPositionEXT::Centered, JoystickHatPositionEXT::Up,
                  JoystickHatPositionEXT::Right, JoystickHatPositionEXT::Down,
                  JoystickHatPositionEXT::Left, JoystickHatPositionEXT::RightUp,
                  JoystickHatPositionEXT::RightDown, JoystickHatPositionEXT::LeftUp,
                  JoystickHatPositionEXT::LeftDown}));
}

TEST_F(CannedJoystickTest, AllJoystickKindsMapExactly)
{
    const std::pair<JoystickKind, JoystickTypeEXT> cases[] = {
        {JoystickKind::Unknown, JoystickTypeEXT::Unknown},
        {JoystickKind::Gamepad, JoystickTypeEXT::Gamepad},
        {JoystickKind::Wheel, JoystickTypeEXT::Wheel},
        {JoystickKind::ArcadeStick, JoystickTypeEXT::ArcadeStick},
        {JoystickKind::FlightStick, JoystickTypeEXT::FlightStick},
        {JoystickKind::DancePad, JoystickTypeEXT::DancePad},
        {JoystickKind::Guitar, JoystickTypeEXT::Guitar},
        {JoystickKind::DrumKit, JoystickTypeEXT::DrumKit},
        {JoystickKind::ArcadePad, JoystickTypeEXT::ArcadePad},
        {JoystickKind::Throttle, JoystickTypeEXT::Throttle},
    };

    std::uint32_t id = 1;
    for (const auto& [kind, expected] : cases)
    {
        platform.joystick.Connect({id, "Device", kind}, {}, {});
        EXPECT_EQ(Joysticks::GetCapabilitiesEXT(id).type, expected);
        ++id;
    }
}

TEST(JoystickInfoEXTTest, EqualityComparesIdNameAndType)
{
    EXPECT_EQ((JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}),
              (JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}));
    EXPECT_NE((JoystickInfoEXT{1, "a", JoystickTypeEXT::Wheel}),
              (JoystickInfoEXT{1, "a", JoystickTypeEXT::Throttle}));
}

TEST(JoystickCapabilitiesEXTTest, EqualityComparesEveryField)
{
    JoystickCapabilitiesEXT left;
    left.isConnected = true;
    left.axisCount = 4;
    left.type = JoystickTypeEXT::Wheel;
    left.name = "Wheel";
    left.guid = "abc";
    left.powerState = PowerStateEXT::Charging;
    left.powerPercent = 50;
    JoystickCapabilitiesEXT right = left;
    EXPECT_EQ(left, right);
    right.powerPercent = 51;
    EXPECT_NE(left, right);
}

TEST(JoystickStateEXTTest, EqualityComparesEveryField)
{
    JoystickStateEXT left;
    left.axes = {1, -2};
    left.buttons = {true, false};
    left.hats = {JoystickHatPositionEXT::Up};
    left.balls = {Point(1, 2)};
    JoystickStateEXT right = left;
    EXPECT_EQ(left, right);
    right.axes[0] = 5;
    EXPECT_NE(left, right);
}
