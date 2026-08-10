// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/InputDevices.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

using CNA::Input::InputDevices;
using CNA::Internal::Input::SdlInputBridge;

namespace
{
    SDL_Event mouseDeviceEvent(Uint32 type, SDL_MouseID which)
    {
        SDL_Event e{};
        e.type = type;
        e.mdevice.which = which;
        return e;
    }
    SDL_Event keyboardDeviceEvent(Uint32 type, SDL_KeyboardID which)
    {
        SDL_Event e{};
        e.type = type;
        e.kdevice.which = which;
        return e;
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

    SdlInputBridge::ProcessEvent(mouseDeviceEvent(SDL_EVENT_MOUSE_ADDED, 42));
    SdlInputBridge::ProcessEvent(mouseDeviceEvent(SDL_EVENT_MOUSE_REMOVED, 42));

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

    SdlInputBridge::ProcessEvent(keyboardDeviceEvent(SDL_EVENT_KEYBOARD_ADDED, 7));
    SdlInputBridge::ProcessEvent(keyboardDeviceEvent(SDL_EVENT_KEYBOARD_REMOVED, 7));

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

    SdlInputBridge::ProcessEvent(mouseDeviceEvent(SDL_EVENT_MOUSE_ADDED, 1));
    EXPECT_EQ(mouseCalls, 1);
    EXPECT_EQ(keyboardCalls, 0);

    SdlInputBridge::ProcessEvent(keyboardDeviceEvent(SDL_EVENT_KEYBOARD_ADDED, 2));
    EXPECT_EQ(mouseCalls, 1);
    EXPECT_EQ(keyboardCalls, 1);
}
