// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/PowerInfo.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_power.h>

namespace
{
    CNA::Devices::PowerState ConvertSdlPowerState(SDL_PowerState state)
    {
        switch (state)
        {
        case SDL_POWERSTATE_ON_BATTERY: return CNA::Devices::PowerState::OnBattery;
        case SDL_POWERSTATE_NO_BATTERY: return CNA::Devices::PowerState::NoBattery;
        case SDL_POWERSTATE_CHARGING:   return CNA::Devices::PowerState::Charging;
        case SDL_POWERSTATE_CHARGED:    return CNA::Devices::PowerState::Charged;
        case SDL_POWERSTATE_UNKNOWN:    return CNA::Devices::PowerState::Unknown;
        case SDL_POWERSTATE_ERROR:
        default:
            return CNA::Devices::PowerState::Error;
        }
    }
} // namespace

namespace CNA::Devices
{
    PowerState PowerInfo::getStateProperty()
    {
        return ConvertSdlPowerState(SDL_GetPowerInfo(nullptr, nullptr));
    }

    int PowerInfo::getBatteryPercentProperty()
    {
        int percent = -1;
        SDL_GetPowerInfo(nullptr, &percent);
        return percent;
    }

    int PowerInfo::getSecondsRemainingProperty()
    {
        int seconds = -1;
        SDL_GetPowerInfo(&seconds, nullptr);
        return seconds;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
