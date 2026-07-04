// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SdlGamepadBackend.hpp"

namespace CNA::Internal::Input
{
    namespace
    {
        // Real SDL-backed implementation: every method forwards 1:1 to the matching SDL3 function.
        class RealSdlGamepadBackend final : public ISdlGamepadBackend
        {
        public:
            bool IsGamepad(SDL_JoystickID instanceId) override
            {
                return SDL_IsGamepad(instanceId);
            }
            SDL_Gamepad* OpenGamepad(SDL_JoystickID instanceId) override
            {
                return SDL_OpenGamepad(instanceId);
            }
            void CloseGamepad(SDL_Gamepad* gamepad) override
            {
                SDL_CloseGamepad(gamepad);
            }
            SDL_Joystick* GetGamepadJoystick(SDL_Gamepad* gamepad) override
            {
                return SDL_GetGamepadJoystick(gamepad);
            }
            SDL_JoystickType GetJoystickType(SDL_Joystick* joystick) override
            {
                return SDL_GetJoystickType(joystick);
            }
            bool GamepadHasButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) override
            {
                return SDL_GamepadHasButton(gamepad, button);
            }
            bool GamepadHasAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) override
            {
                return SDL_GamepadHasAxis(gamepad, axis);
            }
            int GetNumGamepadTouchpads(SDL_Gamepad* gamepad) override
            {
                return SDL_GetNumGamepadTouchpads(gamepad);
            }
            bool GamepadHasSensor(SDL_Gamepad* gamepad, SDL_SensorType type) override
            {
                return SDL_GamepadHasSensor(gamepad, type);
            }
            SDL_PropertiesID GetGamepadProperties(SDL_Gamepad* gamepad) override
            {
                return SDL_GetGamepadProperties(gamepad);
            }
            bool RumbleGamepad(SDL_Gamepad* gamepad, Uint16 lowFreq, Uint16 highFreq, Uint32 durationMs) override
            {
                return SDL_RumbleGamepad(gamepad, lowFreq, highFreq, durationMs);
            }
            bool RumbleGamepadTriggers(SDL_Gamepad* gamepad, Uint16 left, Uint16 right, Uint32 durationMs) override
            {
                return SDL_RumbleGamepadTriggers(gamepad, left, right, durationMs);
            }
            bool SetGamepadLED(SDL_Gamepad* gamepad, Uint8 red, Uint8 green, Uint8 blue) override
            {
                return SDL_SetGamepadLED(gamepad, red, green, blue);
            }
            bool GamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type) override
            {
                return SDL_GamepadSensorEnabled(gamepad, type);
            }
            bool SetGamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type, bool enabled) override
            {
                return SDL_SetGamepadSensorEnabled(gamepad, type, enabled);
            }
            bool GetGamepadSensorData(SDL_Gamepad* gamepad, SDL_SensorType type, float* data, int count) override
            {
                return SDL_GetGamepadSensorData(gamepad, type, data, count);
            }
            Uint16 GetJoystickVendor(SDL_Joystick* joystick) override
            {
                return SDL_GetJoystickVendor(joystick);
            }
            Uint16 GetJoystickProduct(SDL_Joystick* joystick) override
            {
                return SDL_GetJoystickProduct(joystick);
            }
            SDL_GamepadType GetGamepadType(SDL_Gamepad* gamepad) override
            {
                return SDL_GetGamepadType(gamepad);
            }
        };

        RealSdlGamepadBackend g_realBackend;
        ISdlGamepadBackend*   g_currentBackend = &g_realBackend;
    }

    ISdlGamepadBackend& sdl_gamepad_backend()
    {
        return *g_currentBackend;
    }

    void SetSdlGamepadBackendForTests(ISdlGamepadBackend* backend)
    {
        g_currentBackend = (backend != nullptr) ? backend : &g_realBackend;
    }
}
