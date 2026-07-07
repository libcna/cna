// SPDX-License-Identifier: MS-PL
#include "CNA/Input/InputDevices.hpp"

#include "CNA/Internal/Input/SystemDeviceBackend.hpp"

namespace CNA::Input
{
    System::MulticastAction<std::uint32_t> InputDevices::MouseConnectedEXT;
    System::MulticastAction<std::uint32_t> InputDevices::MouseDisconnectedEXT;
    System::MulticastAction<std::uint32_t> InputDevices::KeyboardConnectedEXT;
    System::MulticastAction<std::uint32_t> InputDevices::KeyboardDisconnectedEXT;

    void InputDevices::ResetForTests()
    {
        MouseConnectedEXT = nullptr;
        MouseDisconnectedEXT = nullptr;
        KeyboardConnectedEXT = nullptr;
        KeyboardDisconnectedEXT = nullptr;
    }

    std::vector<InputDeviceInfoEXT> InputDevices::GetMiceEXT()
    {
        return CNA::Internal::Input::system_device_backend().GetMice();
    }

    std::vector<InputDeviceInfoEXT> InputDevices::GetKeyboardsEXT()
    {
        return CNA::Internal::Input::system_device_backend().GetKeyboards();
    }

    std::vector<InputDeviceInfoEXT> InputDevices::GetTouchDevicesEXT()
    {
        return CNA::Internal::Input::system_device_backend().GetTouchDevices();
    }
}
