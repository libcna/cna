// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SystemDeviceBackend.hpp"

#include <SDL3/SDL.h>

namespace CNA::Internal::Input
{
    namespace
    {
        struct RealSystemDeviceBackend final : ISystemDeviceBackend
        {
            std::vector<CNA::Input::InputDeviceInfoEXT> GetMice() override
            {
                std::vector<CNA::Input::InputDeviceInfoEXT> devices;
                int count = 0;
                SDL_MouseID* ids = SDL_GetMice(&count);
                if (ids != nullptr)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        const char* name = SDL_GetMouseNameForID(ids[i]);
                        devices.push_back({ids[i], name ? name : ""});
                    }
                    SDL_free(ids);
                }
                return devices;
            }

            std::vector<CNA::Input::InputDeviceInfoEXT> GetKeyboards() override
            {
                std::vector<CNA::Input::InputDeviceInfoEXT> devices;
                int count = 0;
                SDL_KeyboardID* ids = SDL_GetKeyboards(&count);
                if (ids != nullptr)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        const char* name = SDL_GetKeyboardNameForID(ids[i]);
                        devices.push_back({ids[i], name ? name : ""});
                    }
                    SDL_free(ids);
                }
                return devices;
            }

            std::vector<CNA::Input::InputDeviceInfoEXT> GetTouchDevices() override
            {
                std::vector<CNA::Input::InputDeviceInfoEXT> devices;
                int count = 0;
                SDL_TouchID* ids = SDL_GetTouchDevices(&count);
                if (ids != nullptr)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        const char* name = SDL_GetTouchDeviceName(ids[i]);
                        devices.push_back({ids[i], name ? name : ""});
                    }
                    SDL_free(ids);
                }
                return devices;
            }
        };

        RealSystemDeviceBackend g_realBackend;
        ISystemDeviceBackend*   g_currentBackend = &g_realBackend;
    }

    ISystemDeviceBackend& system_device_backend()
    {
        return *g_currentBackend;
    }

    void SetSystemDeviceBackendForTests(ISystemDeviceBackend* backend)
    {
        g_currentBackend = backend ? backend : &g_realBackend;
    }
}
