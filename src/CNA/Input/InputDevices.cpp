// SPDX-License-Identifier: MS-PL
#include "CNA/Input/InputDevices.hpp"

#include "CNA/Internal/Input/SystemDeviceBackend.hpp"

namespace CNA::Input
{
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
