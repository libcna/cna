// SPDX-License-Identifier: MS-PL
#include "CNA/Input/InputDevices.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

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

    namespace
    {
        /// An empty list when the platform cannot enumerate: this is metadata, and a game that
        /// asks which mice are attached copes with "none reported" far better than with a throw
        /// it has no reason to expect from a query.
        std::vector<InputDeviceInfoEXT> enumerate(const CNA::Platform::InputDeviceKind kind)
        {
            CNA::Platform::IPlatformInputDevices* service =
                CNA::Platform::GetCurrentPlatform().GetInputDevices();
            if (service == nullptr)
            {
                return {};
            }

            std::vector<InputDeviceInfoEXT> devices;
            for (const CNA::Platform::InputDeviceInfo& device : service->GetDevices(kind))
            {
                devices.push_back(InputDeviceInfoEXT{device.id, device.name});
            }
            return devices;
        }
    }

    std::vector<InputDeviceInfoEXT> InputDevices::GetMiceEXT()
    {
        return enumerate(CNA::Platform::InputDeviceKind::Mouse);
    }

    std::vector<InputDeviceInfoEXT> InputDevices::GetKeyboardsEXT()
    {
        return enumerate(CNA::Platform::InputDeviceKind::Keyboard);
    }

    std::vector<InputDeviceInfoEXT> InputDevices::GetTouchDevicesEXT()
    {
        return enumerate(CNA::Platform::InputDeviceKind::Touch);
    }
}
