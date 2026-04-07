//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include "System/DateTimeOffset.hpp"

namespace Microsoft::Devices::Sensors {

    /**
     * @brief Defines the common interface for sensor reading types.
     *
     * This is a C++ counterpart of the .NET
     * Microsoft.Devices.Sensors.ISensorReading interface.
     *
     * @note Status: Partial.
     */
    class ISensorReading {
    public:
        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the sensor reading.
         */
        [[nodiscard]] virtual const System::DateTimeOffset& getTimestampProperty() const = 0;

        /**
         * @brief Virtual destructor for safe polymorphic destruction.
         */
        virtual ~ISensorReading() = default;
    };

} // namespace Microsoft::Devices::Sensors