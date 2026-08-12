// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/InputDevices.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"

#include <cstdint>
#include <vector>

using CNA::Input::InputDevices;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Platform::DeviceEvent;
using CNA::Platform::InputDeviceKind;

namespace
{
    DeviceEvent mouseDeviceEvent(const bool connected, const std::uint32_t which)
    {
        return DeviceEvent{which, InputDeviceKind::Mouse, connected};
    }
    DeviceEvent keyboardDeviceEvent(const bool connected, const std::uint32_t which)
    {
        return DeviceEvent{which, InputDeviceKind::Keyboard, connected};
    }

    class CnaInputDevicesHotplugTest : public ::testing::Test
    {
    protected:
        void SetUp() override { InputDevices::ResetForTests(); }
        void TearDown() override { InputDevices::ResetForTests(); }
    };
}

TEST_F(CnaInputDevicesHotplugTest, MouseAddedAndRemovedFireWithDeviceId)
{
    std::vector<std::uint32_t> connected;
    std::vector<std::uint32_t> disconnected;
    InputDevices::MouseConnectedEXT += [&](std::uint32_t id) { connected.push_back(id); };
    InputDevices::MouseDisconnectedEXT += [&](std::uint32_t id) { disconnected.push_back(id); };

    PlatformInputBridge::ProcessEvent(mouseDeviceEvent(true, 42));
    PlatformInputBridge::ProcessEvent(mouseDeviceEvent(false, 42));

    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0], 42u);
    ASSERT_EQ(disconnected.size(), 1u);
    EXPECT_EQ(disconnected[0], 42u);
}

TEST_F(CnaInputDevicesHotplugTest, KeyboardAddedAndRemovedFireWithDeviceId)
{
    std::vector<std::uint32_t> connected;
    std::vector<std::uint32_t> disconnected;
    InputDevices::KeyboardConnectedEXT += [&](std::uint32_t id) { connected.push_back(id); };
    InputDevices::KeyboardDisconnectedEXT += [&](std::uint32_t id) { disconnected.push_back(id); };

    PlatformInputBridge::ProcessEvent(keyboardDeviceEvent(true, 7));
    PlatformInputBridge::ProcessEvent(keyboardDeviceEvent(false, 7));

    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0], 7u);
    ASSERT_EQ(disconnected.size(), 1u);
    EXPECT_EQ(disconnected[0], 7u);
}

TEST_F(CnaInputDevicesHotplugTest, MouseAndKeyboardEventsDoNotCrossFire)
{
    int mouseCalls = 0;
    int keyboardCalls = 0;
    InputDevices::MouseConnectedEXT += [&](std::uint32_t) { ++mouseCalls; };
    InputDevices::KeyboardConnectedEXT += [&](std::uint32_t) { ++keyboardCalls; };

    PlatformInputBridge::ProcessEvent(mouseDeviceEvent(true, 1));
    EXPECT_EQ(mouseCalls, 1);
    EXPECT_EQ(keyboardCalls, 0);

    PlatformInputBridge::ProcessEvent(keyboardDeviceEvent(true, 2));
    EXPECT_EQ(mouseCalls, 1);
    EXPECT_EQ(keyboardCalls, 1);
}
