// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Power.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

namespace CNA::Input
{
    namespace
    {
        PowerStateEXT to_power_state_ext(const CNA::Platform::PowerState state)
        {
            // Exhaustive with no default arm: a state added to the platform contract without a
            // mapping here becomes a compiler diagnostic rather than a silent Error.
            switch (state)
            {
            case CNA::Platform::PowerState::OnBattery: return PowerStateEXT::OnBattery;
            case CNA::Platform::PowerState::NoBattery: return PowerStateEXT::NoBattery;
            case CNA::Platform::PowerState::Charging:  return PowerStateEXT::Charging;
            case CNA::Platform::PowerState::Charged:   return PowerStateEXT::Charged;
            case CNA::Platform::PowerState::Unknown:   return PowerStateEXT::Unknown;
            case CNA::Platform::PowerState::Error:     return PowerStateEXT::Error;
            }
            return PowerStateEXT::Error;
        }
    }

    PowerStateEXT Power::GetInfoEXT(int& secondsLeft, int& percent)
    {
        // Read once into a local: PowerInfo carries all three answers together, and querying
        // three times could report a state, a percentage and a runtime from three different
        // instants -- visibly wrong the moment a charger is plugged in mid-call.
        const CNA::Platform::PowerInfo info =
            CNA::Platform::GetCurrentPlatform().GetSystemInfo()->GetPowerInfo();

        secondsLeft = info.secondsRemaining;
        percent = info.percent;
        return to_power_state_ext(info.state);
    }
}
