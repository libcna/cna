// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"

namespace Microsoft::Devices::Sensors
{
    using Xna::Framework::Vector3;

    /** @brief Represents one accelerometer sensor reading with a timestamp and acceleration vector. */
    class AccelerometerReading : public ISensorReading
    {
    private:
        DEF_MEMBER(System::DateTimeOffset, Timestamp)
        DEF_MEMBER(Vector3, Acceleration)

    public:
        /**
         * @brief Initializes a new instance with default timestamp and acceleration.
         */
        AccelerometerReading();

        /**
         * @brief Initializes a new instance with the specified timestamp and acceleration.
         *
         * @param timestamp Reading timestamp.
         * @param acceleration Acceleration vector.
         */
        AccelerometerReading(const System::DateTimeOffset& timestamp, const Vector3& acceleration);

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const override;

        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);

        /**
         * @brief Gets the acceleration vector.
         *
         * @return Acceleration vector.
         */
        [[nodiscard]] const Vector3& getAccelerationProperty() const;

        /**
         * @brief Sets the acceleration vector.
         *
         * @param value New acceleration vector.
         */
        void setAccelerationProperty(const Vector3& value);
    };
} // namespace Microsoft::Devices::Sensors
