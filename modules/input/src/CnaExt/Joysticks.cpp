// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Joysticks.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"

#include <limits>

namespace CNA::Input
{
    namespace
    {
        JoystickTypeEXT ToPublicType(const CNA::Platform::JoystickKind kind)
        {
            using CNA::Platform::JoystickKind;
            switch (kind)
            {
                case JoystickKind::Gamepad:      return JoystickTypeEXT::Gamepad;
                case JoystickKind::Wheel:        return JoystickTypeEXT::Wheel;
                case JoystickKind::ArcadeStick:  return JoystickTypeEXT::ArcadeStick;
                case JoystickKind::FlightStick:  return JoystickTypeEXT::FlightStick;
                case JoystickKind::DancePad:     return JoystickTypeEXT::DancePad;
                case JoystickKind::Guitar:       return JoystickTypeEXT::Guitar;
                case JoystickKind::DrumKit:      return JoystickTypeEXT::DrumKit;
                case JoystickKind::ArcadePad:    return JoystickTypeEXT::ArcadePad;
                case JoystickKind::Throttle:     return JoystickTypeEXT::Throttle;
                case JoystickKind::Unknown:      return JoystickTypeEXT::Unknown;
            }
            return JoystickTypeEXT::Unknown;
        }

        JoystickHatPositionEXT ToPublicHat(const CNA::Platform::JoystickHat hat)
        {
            using CNA::Platform::JoystickHat;
            switch (hat)
            {
                case JoystickHat::Up:        return JoystickHatPositionEXT::Up;
                case JoystickHat::Right:     return JoystickHatPositionEXT::Right;
                case JoystickHat::Down:      return JoystickHatPositionEXT::Down;
                case JoystickHat::Left:      return JoystickHatPositionEXT::Left;
                case JoystickHat::RightUp:   return JoystickHatPositionEXT::RightUp;
                case JoystickHat::RightDown: return JoystickHatPositionEXT::RightDown;
                case JoystickHat::LeftUp:    return JoystickHatPositionEXT::LeftUp;
                case JoystickHat::LeftDown:  return JoystickHatPositionEXT::LeftDown;
                case JoystickHat::Centered:  return JoystickHatPositionEXT::Centered;
            }
            return JoystickHatPositionEXT::Centered;
        }

        PowerStateEXT ToPublicPower(const CNA::Platform::JoystickPowerState state)
        {
            using CNA::Platform::JoystickPowerState;
            switch (state)
            {
                case JoystickPowerState::Error:      return PowerStateEXT::Error;
                case JoystickPowerState::Unknown:    return PowerStateEXT::Unknown;
                case JoystickPowerState::OnBattery:  return PowerStateEXT::OnBattery;
                case JoystickPowerState::NoBattery:  return PowerStateEXT::NoBattery;
                case JoystickPowerState::Charging:   return PowerStateEXT::Charging;
                case JoystickPowerState::Charged:    return PowerStateEXT::Charged;
            }
            return PowerStateEXT::Error;
        }

        CNA::Platform::IPlatformJoystick* GetService()
        {
            return CNA::Platform::GetCurrentPlatform().GetJoystick();
        }
    }

    System::MulticastAction<std::uint32_t> Joysticks::ConnectedEXT;
    System::MulticastAction<std::uint32_t> Joysticks::DisconnectedEXT;

    void Joysticks::ResetForTests()
    {
        ConnectedEXT = nullptr;
        DisconnectedEXT = nullptr;
    }

    std::vector<JoystickInfoEXT> Joysticks::GetJoysticksEXT()
    {
        std::vector<JoystickInfoEXT> result;
        CNA::Platform::IPlatformJoystick* service = GetService();
        if (service == nullptr)
        {
            return result;
        }
        for (const CNA::Platform::JoystickInfo& source : service->GetJoysticks())
        {
            if (source.id > std::numeric_limits<std::uint32_t>::max())
            {
                continue;
            }
            result.push_back({static_cast<std::uint32_t>(source.id), source.name,
                              ToPublicType(source.kind)});
        }
        return result;
    }

    JoystickCapabilitiesEXT Joysticks::GetCapabilitiesEXT(const std::uint32_t id)
    {
        CNA::Platform::IPlatformJoystick* service = GetService();
        if (service == nullptr)
        {
            return {};
        }
        const CNA::Platform::JoystickCapabilities source = service->GetCapabilities(id);
        JoystickCapabilitiesEXT result;
        result.isConnected = source.connected;
        result.axisCount = source.axisCount;
        result.buttonCount = source.buttonCount;
        result.hatCount = source.hatCount;
        result.ballCount = source.ballCount;
        result.type = ToPublicType(source.kind);
        result.name = source.name;
        result.guid = source.guid;
        result.powerState = ToPublicPower(source.powerState);
        result.powerPercent = source.powerPercent;
        return result;
    }

    JoystickStateEXT Joysticks::GetStateEXT(const std::uint32_t id)
    {
        CNA::Platform::IPlatformJoystick* service = GetService();
        if (service == nullptr)
        {
            return {};
        }
        const CNA::Platform::JoystickSnapshot source = service->GetSnapshot(id);
        JoystickStateEXT result;
        result.axes = source.axes;
        result.buttons = source.buttons;
        result.hats.reserve(source.hats.size());
        for (const CNA::Platform::JoystickHat hat : source.hats)
        {
            result.hats.push_back(ToPublicHat(hat));
        }
        result.balls.reserve(source.balls.size());
        for (const CNA::Platform::JoystickBallDelta& ball : source.balls)
        {
            result.balls.emplace_back(ball.x, ball.y);
        }
        return result;
    }
}
