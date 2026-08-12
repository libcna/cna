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
        // FNA is poll-driven: SDL3_FNAPlatform.GetGamePadState() re-queries the SDL device
        // (SDL_GetGamepadButton/SDL_GetGamepadAxis, etc.) fresh on every call. CNA is
        // event-driven instead: PlatformInputBridge::ProcessEvent accumulates the raw per-device
        // state in InputManager as SDL events arrive, and GetRawGamePadState here just reads
        // that accumulated snapshot. This is only "current" because Game::Tick() calls
        // PollEvents() (IPlatform::PollEvents -> PlatformInputBridge::ProcessEvent) exactly once per
        // frame, unconditionally, before Update()/Draw() run — see Game::Tick() (and the
        // Emscripten main-loop callback, which does the same). If a caller ever bypasses
        // Game::Tick() (e.g. drives InputManager directly, as the unit tests do), this
        // state simply reflects whatever was last pushed in, with no implicit polling.
        const auto raw = CNA::Internal::Input::InputManager::GetRawGamePadState(playerIndex);
        if (!raw.isConnected)
            return GamePadState();

        const GamePadThumbSticks thumbSticks(
            Microsoft::Xna::Framework::Vector2(raw.leftX, raw.leftY),
            Microsoft::Xna::Framework::Vector2(raw.rightX, raw.rightY),
            deadZoneMode);

        const GamePadTriggers triggers(raw.leftTrigger, raw.rightTrigger, deadZoneMode);
        const GamePadButtons  buttons(raw.buttons);
        const GamePadDPad     dpad = GamePadDPad::FromButtonArray({raw.buttons});

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

    int GamePad::GetPlayerIndexEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetPlayerIndex(playerIndex);
    }

    bool GamePad::SetPlayerIndexEXT(PlayerIndex playerIndex, int index)
    {
        return CNA::Internal::Input::SdlInputBridge::SetPlayerIndex(playerIndex, index);
    }

    CNA::Input::PowerStateEXT GamePad::GetPowerInfoEXT(PlayerIndex playerIndex, int& percent)
    {
        return CNA::Internal::Input::SdlInputBridge::GetPowerInfo(playerIndex, percent);
    }

    CNA::Input::GamePadButtonLabelEXT GamePad::GetButtonLabelEXT(PlayerIndex playerIndex, Buttons button)
    {
        return CNA::Internal::Input::SdlInputBridge::GetButtonLabel(playerIndex, button);
    }

    std::string GamePad::GetNameEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetName(playerIndex);
    }

    std::string GamePad::GetPathEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetPath(playerIndex);
    }

    std::string GamePad::GetSerialEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetSerial(playerIndex);
    }

    std::uint16_t GamePad::GetFirmwareVersionEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetFirmwareVersion(playerIndex);
    }

    std::uint64_t GamePad::GetSteamHandleEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetSteamHandle(playerIndex);
    }

    CNA::Input::GamePadConnectionStateEXT GamePad::GetConnectionStateEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetConnectionState(playerIndex);
    }

    int GamePad::GetTouchpadCountEXT(PlayerIndex playerIndex)
    {
        return CNA::Internal::Input::SdlInputBridge::GetTouchpadCount(playerIndex);
    }

    int GamePad::GetTouchpadFingerCountEXT(PlayerIndex playerIndex, int touchpad)
    {
        return CNA::Internal::Input::SdlInputBridge::GetTouchpadFingerCount(playerIndex, touchpad);
    }

    bool GamePad::GetTouchpadFingerEXT(PlayerIndex playerIndex, int touchpad, int finger,
                                       bool& down, float& x, float& y, float& pressure)
    {
        return CNA::Internal::Input::SdlInputBridge::GetTouchpadFinger(
            playerIndex, touchpad, finger, down, x, y, pressure);
    }
}
