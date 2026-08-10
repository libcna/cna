// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Event data for the Compass.Calibrate and Motion.Calibrate events.
     *
     * Carries no additional data; the XNA class is intentionally empty.
     */
    class CalibrationEventArgs : public System::EventArgs
    {
    public:
        /** @brief Initializes a new instance of the CalibrationEventArgs class. */
        CalibrationEventArgs();

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.CalibrationEventArgs"
         */
        CNAEXT [[nodiscard]] std::string GetTypeName() const;
    };
} // namespace Microsoft::Devices::Sensors
