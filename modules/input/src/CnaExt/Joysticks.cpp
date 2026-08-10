// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Joysticks.hpp"

#include "CNA/Internal/Input/SdlInputBridge.hpp"

namespace CNA::Input
{
    System::MulticastAction<std::uint32_t> Joysticks::ConnectedEXT;
    System::MulticastAction<std::uint32_t> Joysticks::DisconnectedEXT;

    void Joysticks::ResetForTests()
    {
        ConnectedEXT = nullptr;
        DisconnectedEXT = nullptr;
    }

    std::vector<JoystickInfoEXT> Joysticks::GetJoysticksEXT()
    {
        return CNA::Internal::Input::SdlInputBridge::GetJoysticks();
    }

    JoystickCapabilitiesEXT Joysticks::GetCapabilitiesEXT(const std::uint32_t id)
    {
        return CNA::Internal::Input::SdlInputBridge::GetJoystickCapabilities(id);
    }

    JoystickStateEXT Joysticks::GetStateEXT(const std::uint32_t id)
    {
        return CNA::Internal::Input::SdlInputBridge::GetJoystickState(id);
    }
}
