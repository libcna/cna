// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Haptics.hpp"

#include "CNA/Internal/Input/SdlHapticBackend.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"

#include <utility>

namespace CNA::Input
{
    std::vector<HapticInfoEXT> Haptics::GetHapticsEXT()
    {
        std::vector<HapticInfoEXT> result;
        for (const SDL_HapticID id : CNA::Internal::Input::sdl_haptic_backend().GetHaptics())
        {
            HapticInfoEXT info;
            info.id = static_cast<std::uint32_t>(id);
            info.name = CNA::Internal::Input::sdl_haptic_backend().GetHapticNameForID(id);
            result.push_back(std::move(info));
        }
        return result;
    }

    HapticDevice Haptics::OpenEXT(const std::uint32_t id)
    {
        return HapticDevice(
            CNA::Internal::Input::sdl_haptic_backend().OpenHaptic(static_cast<SDL_HapticID>(id)));
    }

    HapticDevice Haptics::OpenFromJoystickEXT(const std::uint32_t joystickId)
    {
        CNA::Platform::IPlatformJoystick* joysticks =
            CNA::Platform::GetCurrentPlatform().GetJoystick();
        if (joysticks == nullptr || !joysticks->IsConnected(joystickId))
            return HapticDevice(nullptr);
        return HapticDevice(
            CNA::Internal::Input::sdl_haptic_backend().OpenHapticFromJoystick(joystickId));
    }

    HapticDevice Haptics::OpenFromMouseEXT()
    {
        return HapticDevice(CNA::Internal::Input::sdl_haptic_backend().OpenHapticFromMouse());
    }

    bool Haptics::IsJoystickHapticEXT(const std::uint32_t joystickId)
    {
        CNA::Platform::IPlatformJoystick* joysticks =
            CNA::Platform::GetCurrentPlatform().GetJoystick();
        return joysticks != nullptr && joysticks->IsConnected(joystickId)
            && CNA::Internal::Input::sdl_haptic_backend().IsJoystickHaptic(joystickId);
    }

    bool Haptics::IsMouseHapticEXT()
    {
        return CNA::Internal::Input::sdl_haptic_backend().IsMouseHaptic();
    }
}
