// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/PowerInfo.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"

namespace
{
    CNA::Devices::PowerState ConvertPlatformPowerState(const CNA::Platform::PowerState state)
    {
        switch (state)
        {
        case CNA::Platform::PowerState::OnBattery: return CNA::Devices::PowerState::OnBattery;
        case CNA::Platform::PowerState::NoBattery: return CNA::Devices::PowerState::NoBattery;
        case CNA::Platform::PowerState::Charging:  return CNA::Devices::PowerState::Charging;
        case CNA::Platform::PowerState::Charged:   return CNA::Devices::PowerState::Charged;
        case CNA::Platform::PowerState::Unknown:   return CNA::Devices::PowerState::Unknown;
        case CNA::Platform::PowerState::Error:     return CNA::Devices::PowerState::Error;
        }
        return CNA::Devices::PowerState::Error;
    }
} // namespace

namespace CNA::Devices
{
    PowerState PowerInfo::getStateProperty()
    {
        return ConvertPlatformPowerState(
            Platform::GetCurrentPlatform().GetSystemInfo()->GetPowerInfo().state);
    }

    int PowerInfo::getBatteryPercentProperty()
    {
        return Platform::GetCurrentPlatform().GetSystemInfo()->GetPowerInfo().percent;
    }

    int PowerInfo::getSecondsRemainingProperty()
    {
        return Platform::GetCurrentPlatform().GetSystemInfo()->GetPowerInfo().secondsRemaining;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
