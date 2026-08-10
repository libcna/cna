// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Power.hpp"

#include "CNA/Internal/Input/SystemPowerBackend.hpp"

namespace CNA::Input
{
    namespace
    {
        PowerStateEXT to_power_state_ext(SDL_PowerState state)
        {
            switch (state)
            {
            case SDL_POWERSTATE_ON_BATTERY: return PowerStateEXT::OnBattery;
            case SDL_POWERSTATE_NO_BATTERY: return PowerStateEXT::NoBattery;
            case SDL_POWERSTATE_CHARGING:   return PowerStateEXT::Charging;
            case SDL_POWERSTATE_CHARGED:    return PowerStateEXT::Charged;
            case SDL_POWERSTATE_UNKNOWN:    return PowerStateEXT::Unknown;
            case SDL_POWERSTATE_ERROR:
            default:                        return PowerStateEXT::Error;
            }
        }
    }

    PowerStateEXT Power::GetInfoEXT(int& secondsLeft, int& percent)
    {
        secondsLeft = -1;
        percent = -1;
        const SDL_PowerState state =
            CNA::Internal::Input::system_power_backend().GetPowerInfo(&secondsLeft, &percent);
        return to_power_state_ext(state);
    }
}
