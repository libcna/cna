// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "CNA/Input/GamePadButtonLabel.hpp"
#include "CNA/Input/GamePadConnectionState.hpp"
#include "CNA/Input/PowerState.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/Input/IPlatformGamepad.hpp"

#include <cmath>
#include <cstdio>
#include <optional>

namespace Microsoft::Xna::Framework::Input
{
    namespace
    {
        int ToSlot(const PlayerIndex playerIndex)
        {
            const int slot = static_cast<int>(playerIndex);
            return slot >= 0 && slot < CNA::Platform::GamepadSlotCount ? slot : -1;
        }

        CNA::Platform::IPlatformGamepad* GetService()
        {
            return CNA::Platform::GetCurrentPlatform().GetGamepad();
        }

        CNA::Platform::IPlatformGamepad* GetService(const PlayerIndex playerIndex, int& slot)
        {
            slot = ToSlot(playerIndex);
            return slot >= 0 ? GetService() : nullptr;
        }

        std::optional<CNA::Platform::GamepadButton> ToPlatformButton(const Buttons button)
        {
            using CNA::Platform::GamepadButton;
            switch (button)
            {
                case Buttons::A:             return GamepadButton::A;
                case Buttons::B:             return GamepadButton::B;
                case Buttons::X:             return GamepadButton::X;
                case Buttons::Y:             return GamepadButton::Y;
                case Buttons::Back:          return GamepadButton::Back;
                case Buttons::Start:         return GamepadButton::Start;
                case Buttons::LeftShoulder:  return GamepadButton::LeftShoulder;
                case Buttons::RightShoulder: return GamepadButton::RightShoulder;
                case Buttons::LeftStick:     return GamepadButton::LeftStick;
                case Buttons::RightStick:    return GamepadButton::RightStick;
                case Buttons::DPadUp:        return GamepadButton::DPadUp;
                case Buttons::DPadDown:      return GamepadButton::DPadDown;
                case Buttons::DPadLeft:      return GamepadButton::DPadLeft;
                case Buttons::DPadRight:     return GamepadButton::DPadRight;
                case Buttons::BigButton:     return GamepadButton::BigButton;
                case Buttons::Misc1EXT:      return GamepadButton::Misc1;
                case Buttons::Paddle1EXT:    return GamepadButton::Paddle1;
                case Buttons::Paddle2EXT:    return GamepadButton::Paddle2;
                case Buttons::Paddle3EXT:    return GamepadButton::Paddle3;
                case Buttons::Paddle4EXT:    return GamepadButton::Paddle4;
                case Buttons::TouchPadEXT:   return GamepadButton::TouchPad;
                default:                     return std::nullopt;
            }
        }

        GamePadType ToGamePadType(const CNA::Platform::GamepadKind kind)
        {
            using CNA::Platform::GamepadKind;
            switch (kind)
            {
                case GamepadKind::Gamepad:      return GamePadType::GamePad;
                case GamepadKind::Wheel:        return GamePadType::Wheel;
                case GamepadKind::ArcadeStick:  return GamePadType::ArcadeStick;
                case GamepadKind::FlightStick:  return GamePadType::FlightStick;
                case GamepadKind::DancePad:     return GamePadType::DancePad;
                case GamepadKind::Guitar:       return GamePadType::Guitar;
                case GamepadKind::DrumKit:      return GamePadType::DrumKit;
                case GamepadKind::BigButtonPad: return GamePadType::BigButtonPad;
                case GamepadKind::Unknown:      return GamePadType::Unknown;
            }
            return GamePadType::Unknown;
        }

        std::string FormatGuid(const CNA::Platform::GamepadInfo& info)
        {
            if (info.vendor == 0 && info.product == 0)
            {
                return "xinput";
            }

            if (info.vendor == 0x28de)
            {
                switch (info.model)
                {
                    case CNA::Platform::GamepadModel::Xbox360:
                    case CNA::Platform::GamepadModel::XboxOne:      return "xinput";
                    case CNA::Platform::GamepadModel::PlayStation4: return "4c05c405";
                    case CNA::Platform::GamepadModel::PlayStation5: return "4c05e60c";
                    default:                                        break;
                }
            }

            char buffer[9];
            std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x%02x",
                          info.vendor & 0xFF, (info.vendor >> 8) & 0xFF,
                          info.product & 0xFF, (info.product >> 8) & 0xFF);
            return buffer;
        }
    }

    float GamePad::ExcludeAxisDeadZone(float value, float deadZone)
    {
        if (value < -deadZone)
            value += deadZone;
        else if (value > deadZone)
            value -= deadZone;
        else
            return 0.0f;
        return value / (1.0f - deadZone);
    }

    GamePadCapabilities GamePad::GetCapabilities(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        if (service == nullptr)
        {
            return {};
        }

        const CNA::Platform::GamepadCapabilities& source = service->GetCapabilities(slot);
        if (!source.connected)
        {
            return {};
        }

        const auto hasButton = [&source](const CNA::Platform::GamepadButton button) {
            return (source.buttons & static_cast<std::uint32_t>(button)) != 0;
        };
        const auto hasAxis = [&source](const CNA::Platform::GamepadAxis axis) {
            return (source.axes & CNA::Platform::GamepadAxisBit(axis)) != 0;
        };

        GamePadCapabilities result;
        result.setIsConnectedProperty(true);
        result.setGamePadTypeProperty(ToGamePadType(source.kind));
        result.setHasAButtonProperty(hasButton(CNA::Platform::GamepadButton::A));
        result.setHasBButtonProperty(hasButton(CNA::Platform::GamepadButton::B));
        result.setHasXButtonProperty(hasButton(CNA::Platform::GamepadButton::X));
        result.setHasYButtonProperty(hasButton(CNA::Platform::GamepadButton::Y));
        result.setHasBackButtonProperty(hasButton(CNA::Platform::GamepadButton::Back));
        result.setHasStartButtonProperty(hasButton(CNA::Platform::GamepadButton::Start));
        result.setHasBigButtonProperty(hasButton(CNA::Platform::GamepadButton::BigButton));
        result.setHasLeftShoulderButtonProperty(hasButton(CNA::Platform::GamepadButton::LeftShoulder));
        result.setHasRightShoulderButtonProperty(hasButton(CNA::Platform::GamepadButton::RightShoulder));
        result.setHasLeftStickButtonProperty(hasButton(CNA::Platform::GamepadButton::LeftStick));
        result.setHasRightStickButtonProperty(hasButton(CNA::Platform::GamepadButton::RightStick));
        result.setHasDPadUpButtonProperty(hasButton(CNA::Platform::GamepadButton::DPadUp));
        result.setHasDPadDownButtonProperty(hasButton(CNA::Platform::GamepadButton::DPadDown));
        result.setHasDPadLeftButtonProperty(hasButton(CNA::Platform::GamepadButton::DPadLeft));
        result.setHasDPadRightButtonProperty(hasButton(CNA::Platform::GamepadButton::DPadRight));
        result.setHasMisc1EXTProperty(hasButton(CNA::Platform::GamepadButton::Misc1));
        result.setHasPaddle1EXTProperty(hasButton(CNA::Platform::GamepadButton::Paddle1));
        result.setHasPaddle2EXTProperty(hasButton(CNA::Platform::GamepadButton::Paddle2));
        result.setHasPaddle3EXTProperty(hasButton(CNA::Platform::GamepadButton::Paddle3));
        result.setHasPaddle4EXTProperty(hasButton(CNA::Platform::GamepadButton::Paddle4));
        result.setHasTouchPadEXTProperty(source.touchpad);
        result.setHasLeftXThumbStickProperty(hasAxis(CNA::Platform::GamepadAxis::LeftThumbstickX));
        result.setHasLeftYThumbStickProperty(hasAxis(CNA::Platform::GamepadAxis::LeftThumbstickY));
        result.setHasRightXThumbStickProperty(hasAxis(CNA::Platform::GamepadAxis::RightThumbstickX));
        result.setHasRightYThumbStickProperty(hasAxis(CNA::Platform::GamepadAxis::RightThumbstickY));
        result.setHasLeftTriggerProperty(hasAxis(CNA::Platform::GamepadAxis::LeftTrigger));
        result.setHasRightTriggerProperty(hasAxis(CNA::Platform::GamepadAxis::RightTrigger));
        result.setHasLeftVibrationMotorProperty(source.rumble);
        result.setHasRightVibrationMotorProperty(source.rumble);
        result.setHasTriggerVibrationMotorsEXTProperty(source.triggerRumble);
        result.setHasLightBarEXTProperty(source.lightBar);
        result.setHasGyroEXTProperty(source.gyroscope);
        result.setHasAccelerometerEXTProperty(source.accelerometer);
        return result;
    }

    GamePadState GamePad::GetState(PlayerIndex playerIndex)
    {
        return GetState(playerIndex, GamePadDeadZone::IndependentAxes);
    }

    GamePadState GamePad::GetState(PlayerIndex playerIndex, GamePadDeadZone deadZoneMode)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        if (service == nullptr)
        {
            return {};
        }
        const CNA::Platform::GamepadSnapshot& raw = service->GetSnapshot(slot);
        if (!raw.connected)
        {
            return {};
        }

        const auto axis = [&raw](const CNA::Platform::GamepadAxis value) {
            return raw.axes[static_cast<std::size_t>(value)];
        };

        const GamePadThumbSticks thumbSticks(
            Microsoft::Xna::Framework::Vector2(axis(CNA::Platform::GamepadAxis::LeftThumbstickX),
                                               axis(CNA::Platform::GamepadAxis::LeftThumbstickY)),
            Microsoft::Xna::Framework::Vector2(axis(CNA::Platform::GamepadAxis::RightThumbstickX),
                                               axis(CNA::Platform::GamepadAxis::RightThumbstickY)),
            deadZoneMode);

        const GamePadTriggers triggers(axis(CNA::Platform::GamepadAxis::LeftTrigger),
                                       axis(CNA::Platform::GamepadAxis::RightTrigger), deadZoneMode);
        const Buttons flags = static_cast<Buttons>(raw.buttons);
        const GamePadButtons buttons(flags);
        const GamePadDPad dpad = GamePadDPad::FromButtonArray({flags});

        GamePadState state(thumbSticks, triggers, buttons, dpad);
        state.setPacketNumberProperty(static_cast<int>(raw.packetNumber));
        return state;
    }

    bool GamePad::SetVibration(PlayerIndex playerIndex, float leftMotor, float rightMotor)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr && service->SetRumble(slot, leftMotor, rightMotor, 0);
    }

    std::string GamePad::GetGUIDEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        if (service == nullptr || !service->GetSnapshot(slot).connected)
        {
            return {};
        }
        return FormatGuid(service->GetInfo(slot));
    }

    void GamePad::SetLightBarEXT(PlayerIndex playerIndex, const Microsoft::Xna::Framework::Color& color)
    {
        int slot = -1;
        if (CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot))
        {
            (void)service->SetLightBar(slot, color.getRProperty(), color.getGProperty(),
                                      color.getBProperty());
        }
    }

    bool GamePad::SetTriggerVibrationEXT(PlayerIndex playerIndex, float leftTrigger, float rightTrigger)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr && service->SetTriggerRumble(slot, leftTrigger, rightTrigger, 0);
    }

    bool GamePad::GetGyroEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& gyro)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        CNA::Platform::GamepadSensorReading reading;
        const bool result = service != nullptr
            && service->TryGetSensor(slot, CNA::Platform::GamepadSensor::Gyroscope, reading);
        gyro = result ? Microsoft::Xna::Framework::Vector3(reading.x, reading.y, reading.z)
                      : Microsoft::Xna::Framework::Vector3::Zero;
        return result;
    }

    bool GamePad::GetAccelerometerEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& accel)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        CNA::Platform::GamepadSensorReading reading;
        const bool result = service != nullptr
            && service->TryGetSensor(slot, CNA::Platform::GamepadSensor::Accelerometer, reading);
        accel = result ? Microsoft::Xna::Framework::Vector3(reading.x, reading.y, reading.z)
                       : Microsoft::Xna::Framework::Vector3::Zero;
        return result;
    }

    int GamePad::GetPlayerIndexEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetPlayerIndex(slot) : -1;
    }

    bool GamePad::SetPlayerIndexEXT(PlayerIndex playerIndex, int index)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr && service->SetPlayerIndex(slot, index);
    }

    CNA::Input::PowerStateEXT GamePad::GetPowerInfoEXT(PlayerIndex playerIndex, int& percent)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        if (service == nullptr)
        {
            percent = -1;
            return CNA::Input::PowerStateEXT::Error;
        }
        const CNA::Platform::GamepadPowerInfo info = service->GetPowerInfo(slot);
        percent = info.percent;
        switch (info.state)
        {
            case CNA::Platform::GamepadPowerState::Unknown:   return CNA::Input::PowerStateEXT::Unknown;
            case CNA::Platform::GamepadPowerState::Error:     return CNA::Input::PowerStateEXT::Error;
            case CNA::Platform::GamepadPowerState::OnBattery: return CNA::Input::PowerStateEXT::OnBattery;
            case CNA::Platform::GamepadPowerState::Charging:  return CNA::Input::PowerStateEXT::Charging;
            case CNA::Platform::GamepadPowerState::Charged:   return CNA::Input::PowerStateEXT::Charged;
            case CNA::Platform::GamepadPowerState::NoBattery: return CNA::Input::PowerStateEXT::NoBattery;
        }
        return CNA::Input::PowerStateEXT::Error;
    }

    CNA::Input::GamePadButtonLabelEXT GamePad::GetButtonLabelEXT(PlayerIndex playerIndex, Buttons button)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        const auto platformButton = ToPlatformButton(button);
        const CNA::Platform::GamepadButtonLabel label = service != nullptr && platformButton.has_value()
            ? service->GetButtonLabel(slot, *platformButton)
            : CNA::Platform::GamepadButtonLabel::Unknown;
        switch (label)
        {
            case CNA::Platform::GamepadButtonLabel::A:        return CNA::Input::GamePadButtonLabelEXT::A;
            case CNA::Platform::GamepadButtonLabel::B:        return CNA::Input::GamePadButtonLabelEXT::B;
            case CNA::Platform::GamepadButtonLabel::X:        return CNA::Input::GamePadButtonLabelEXT::X;
            case CNA::Platform::GamepadButtonLabel::Y:        return CNA::Input::GamePadButtonLabelEXT::Y;
            case CNA::Platform::GamepadButtonLabel::Cross:    return CNA::Input::GamePadButtonLabelEXT::Cross;
            case CNA::Platform::GamepadButtonLabel::Circle:   return CNA::Input::GamePadButtonLabelEXT::Circle;
            case CNA::Platform::GamepadButtonLabel::Square:   return CNA::Input::GamePadButtonLabelEXT::Square;
            case CNA::Platform::GamepadButtonLabel::Triangle: return CNA::Input::GamePadButtonLabelEXT::Triangle;
            case CNA::Platform::GamepadButtonLabel::Unknown:  return CNA::Input::GamePadButtonLabelEXT::Unknown;
        }
        return CNA::Input::GamePadButtonLabelEXT::Unknown;
    }

    std::string GamePad::GetNameEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetInfo(slot).name : std::string();
    }

    std::string GamePad::GetPathEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetInfo(slot).path : std::string();
    }

    std::string GamePad::GetSerialEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetInfo(slot).serial : std::string();
    }

    std::uint16_t GamePad::GetFirmwareVersionEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetInfo(slot).firmwareVersion : 0;
    }

    std::uint64_t GamePad::GetSteamHandleEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetInfo(slot).steamHandle : 0;
    }

    CNA::Input::GamePadConnectionStateEXT GamePad::GetConnectionStateEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        const auto state = service != nullptr ? service->GetInfo(slot).connectionState
                                              : CNA::Platform::GamepadConnectionState::Unknown;
        switch (state)
        {
            case CNA::Platform::GamepadConnectionState::Wired:
                return CNA::Input::GamePadConnectionStateEXT::Wired;
            case CNA::Platform::GamepadConnectionState::Wireless:
                return CNA::Input::GamePadConnectionStateEXT::Wireless;
            case CNA::Platform::GamepadConnectionState::Unknown:
                return CNA::Input::GamePadConnectionStateEXT::Unknown;
        }
        return CNA::Input::GamePadConnectionStateEXT::Unknown;
    }

    int GamePad::GetTouchpadCountEXT(PlayerIndex playerIndex)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetTouchpadCount(slot) : 0;
    }

    int GamePad::GetTouchpadFingerCountEXT(PlayerIndex playerIndex, int touchpad)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        return service != nullptr ? service->GetTouchpadFingerCount(slot, touchpad) : 0;
    }

    bool GamePad::GetTouchpadFingerEXT(PlayerIndex playerIndex, int touchpad, int finger,
                                       bool& down, float& x, float& y, float& pressure)
    {
        int slot = -1;
        CNA::Platform::IPlatformGamepad* service = GetService(playerIndex, slot);
        CNA::Platform::GamepadTouchpadFinger value;
        const bool result = service != nullptr
            && service->TryGetTouchpadFinger(slot, touchpad, finger, value);
        down = value.down;
        x = value.x;
        y = value.y;
        pressure = value.pressure;
        return result;
    }
}
