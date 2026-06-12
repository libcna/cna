// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <type_traits>
#include <utility>

#include "System/EventArgs.hpp"
#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Provides data for a sensor reading event.
     *
     * This is a C++ counterpart of the .NET
     * Microsoft.Devices.Sensors.SensorReadingEventArgs<T> class.
     *
     * @tparam T Type of sensor reading payload.
     *
     * @note Status: Partial.
     */
    template <typename T>
    class SensorReadingEventArgs : public System::EventArgs
    {
        static_assert(std::is_base_of_v<ISensorReading, T>,
                      "T must derive from ISensorReading");

    private:
        DEF_MEMBER(T, SensorReading)

    public:
        /**
         * @brief Initializes a new instance with a default-constructed sensor reading.
         */
        SensorReadingEventArgs()
            : SensorReading_{}
        {
        }

        /**
         * @brief Initializes a new instance with the specified sensor reading.
         *
         * @param sensorReading Sensor reading value.
         */
        explicit SensorReadingEventArgs(const T& sensorReading)
            : SensorReading_(sensorReading)
        {
        }

        /**
         * @brief Initializes a new instance with the specified sensor reading.
         *
         * @param sensorReading Sensor reading value to move.
         */
        explicit SensorReadingEventArgs(T&& sensorReading)
            : SensorReading_(std::move(sensorReading))
        {
        }

        /**
         * @brief Gets the sensor reading associated with this event.
         *
         * @return Sensor reading value.
         */
        [[nodiscard]] const T& getSensorReadingProperty() const
        {
            return SensorReading_;
        }

        /**
         * @brief Sets the sensor reading associated with this event.
         *
         * @param v New sensor reading value.
         */
        void setSensorReadingProperty(const T& v)
        {
            SensorReading_ = v;
        }

        /**
         * @brief Sets the sensor reading associated with this event.
         *
         * @param v New sensor reading value to move.
         */
        void setSensorReadingProperty(T&& v)
        {
            SensorReading_ = std::move(v);
        }
    };
} // namespace Microsoft::Devices::Sensors
