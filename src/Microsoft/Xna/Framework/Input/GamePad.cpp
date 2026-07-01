// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"

#include <cmath>

namespace Microsoft::Xna::Framework::Input
{
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
        return CNA::Internal::Input::SdlInputBridge::GetCapabilities(playerIndex);
    }

    GamePadState GamePad::GetState(PlayerIndex playerIndex)
    {
        return GetState(playerIndex, GamePadDeadZone::IndependentAxes);
    }

    GamePadState GamePad::GetState(PlayerIndex playerIndex, GamePadDeadZone deadZoneMode)
    {
        const auto raw = CNA::Internal::Input::InputManager::GetRawGamePadState(playerIndex);
        if (!raw.isConnected)
            return GamePadState();

        const GamePadThumbSticks thumbSticks(
            Microsoft::Xna::Framework::Vector2(raw.leftX, raw.leftY),
            Microsoft::Xna::Framework::Vector2(raw.rightX, raw.rightY),
            deadZoneMode);

        const GamePadTriggers triggers(raw.leftTrigger, raw.rightTrigger, deadZoneMode);
        const GamePadButtons  buttons(raw.buttons);
        const GamePadDPad     dpad = GamePadDPad::FromButtons(raw.buttons);

        GamePadState state(thumbSticks, triggers, buttons, dpad);
        state.setPacketNumberProperty(raw.packetNumber);
        return state;
    }

    bool GamePad::SetVibration(PlayerIndex playerIndex, float leftMotor, float rightMotor)
    {
        return CNA::Internal::Input::SdlInputBridge::SetVibration(playerIndex, leftMotor, rightMotor);
    }

    std::string GamePad::GetGUIDEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetGUID(playerIndex);
    }

    void GamePad::SetLightBarEXT(PlayerIndex playerIndex, const Microsoft::Xna::Framework::Color& color)
    {
        CNA::Internal::Input::SdlInputBridge::SetLightBar(playerIndex, color);
    }

    bool GamePad::SetTriggerVibrationEXT(PlayerIndex playerIndex, float leftTrigger, float rightTrigger)
    {
        return CNA::Internal::Input::SdlInputBridge::SetTriggerVibration(playerIndex, leftTrigger, rightTrigger);
    }

    bool GamePad::GetGyroEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& gyro)
    {
        return CNA::Internal::Input::SdlInputBridge::GetGyro(playerIndex, gyro);
    }

    bool GamePad::GetAccelerometerEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& accel)
    {
        return CNA::Internal::Input::SdlInputBridge::GetAccelerometer(playerIndex, accel);
    }
}
