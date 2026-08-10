// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SdlJoystickBackend.hpp"

namespace CNA::Internal::Input
{
    namespace
    {
        // Real SDL-backed implementation: every method forwards 1:1 to the matching SDL3 function.
        class RealSdlJoystickBackend final : public ISdlJoystickBackend
        {
        public:
            SDL_Joystick* OpenJoystick(SDL_JoystickID instanceId) override
            {
                return SDL_OpenJoystick(instanceId);
            }
            void CloseJoystick(SDL_Joystick* joystick) override
            {
                SDL_CloseJoystick(joystick);
            }
            std::string GetJoystickName(SDL_Joystick* joystick) override
            {
                const char* s = SDL_GetJoystickName(joystick);
                return s ? s : "";
            }
            SDL_JoystickType GetJoystickType(SDL_Joystick* joystick) override
            {
                return SDL_GetJoystickType(joystick);
            }
            std::string GetJoystickGUID(SDL_Joystick* joystick) override
            {
                char buffer[33] = {};
                SDL_GUIDToString(SDL_GetJoystickGUID(joystick), buffer, sizeof(buffer));
                return buffer;
            }
            int GetNumJoystickAxes(SDL_Joystick* joystick) override
            {
                return SDL_GetNumJoystickAxes(joystick);
            }
            int GetNumJoystickButtons(SDL_Joystick* joystick) override
            {
                return SDL_GetNumJoystickButtons(joystick);
            }
            int GetNumJoystickHats(SDL_Joystick* joystick) override
            {
                return SDL_GetNumJoystickHats(joystick);
            }
            int GetNumJoystickBalls(SDL_Joystick* joystick) override
            {
                return SDL_GetNumJoystickBalls(joystick);
            }
            Sint16 GetJoystickAxis(SDL_Joystick* joystick, int axis) override
            {
                return SDL_GetJoystickAxis(joystick, axis);
            }
            bool GetJoystickButton(SDL_Joystick* joystick, int button) override
            {
                return SDL_GetJoystickButton(joystick, button);
            }
            Uint8 GetJoystickHat(SDL_Joystick* joystick, int hat) override
            {
                return SDL_GetJoystickHat(joystick, hat);
            }
            bool GetJoystickBall(SDL_Joystick* joystick, int ball, int* dx, int* dy) override
            {
                return SDL_GetJoystickBall(joystick, ball, dx, dy);
            }
            SDL_PowerState GetJoystickPowerInfo(SDL_Joystick* joystick, int* percent) override
            {
                return SDL_GetJoystickPowerInfo(joystick, percent);
            }
        };

        RealSdlJoystickBackend g_realBackend;
        ISdlJoystickBackend*   g_currentBackend = &g_realBackend;
    }

    ISdlJoystickBackend& sdl_joystick_backend()
    {
        return *g_currentBackend;
    }

    void SetSdlJoystickBackendForTests(ISdlJoystickBackend* backend)
    {
        g_currentBackend = (backend != nullptr) ? backend : &g_realBackend;
    }
}
