// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
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
        friend class Accelerometer;

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
         * @brief Gets the acceleration vector.
         *
         * @return Acceleration vector.
         */
        [[nodiscard]] const Vector3& getAccelerationProperty() const;

        /**
         * @brief Returns true if both readings have equal Acceleration and Timestamp.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — the real
         * `AccelerometerReading` structure's own archived MSDN reference page
         * (`ff403534(v=vs.105)`) lists no equality operator at all;
         * `Equals(Object)` is inherited unmodified from `System.ValueType`
         * (field-reflection based). C++ `struct`/`class` types have no such
         * automatic equality, so this operator is a useful, deliberate CNA
         * convenience, not an attempt to mimic undocumented real behavior.
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        CNAEXT bool operator==(const AccelerometerReading& other) const;

        /**
         * @brief Returns true if the readings differ in Acceleration or Timestamp.
         *
         * See operator==()'s doc comment — same CNA-extension rationale.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        CNAEXT bool operator!=(const AccelerometerReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — the real
         * structure's `ToString()` is inherited unmodified from
         * `System.ValueType.ToString()`, which returns only the fully
         * qualified type name (`"Microsoft.Devices.Sensors.AccelerometerReading"`),
         * not field values. This format is a more useful CNA convenience.
         *
         * @return String in the format "Acceleration:{X:0 Y:0 Z:0}".
         */
        CNAEXT [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this reading.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — the real
         * structure's `GetHashCode()` is inherited unmodified from
         * `System.ValueType.GetHashCode()`. See operator==()'s doc comment
         * for the same CNA-extension rationale.
         *
         * @return Hash derived from Acceleration and Timestamp.
         */
        CNAEXT [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.AccelerometerReading"
         */
        CNAEXT [[nodiscard]] std::string GetTypeName() const;

    private:
        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * Restricted to Accelerometer, the class that produces readings of
         * this type, matching the real WP7 API's `internal set` (settable
         * only from within Microsoft.Devices.Sensors.dll).
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);

        /**
         * @brief Sets the acceleration vector.
         *
         * Restricted to Accelerometer; see setTimestampProperty() for why.
         *
         * @param value New acceleration vector.
         */
        void setAccelerationProperty(const Vector3& value);
    };
} // namespace Microsoft::Devices::Sensors
