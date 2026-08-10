// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"

namespace Microsoft::Devices::Sensors
{
    CalibrationEventArgs::CalibrationEventArgs() = default;

    std::string CalibrationEventArgs::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.CalibrationEventArgs";
    }
} // namespace Microsoft::Devices::Sensors
