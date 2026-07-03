// SPDX-License-Identifier: MS-PL

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

    /** @brief Represents one gyroscope sensor reading with a timestamp and angular velocity vector. */
    class GyroscopeReading : public ISensorReading
    {
        friend class Gyroscope;

    private:
        DEF_MEMBER(Vector3, RotationRate)
        DEF_MEMBER(System::DateTimeOffset, Timestamp)

    public:
        /**
         * @brief Initializes a new instance with default rotation rate and timestamp.
         */
        GyroscopeReading();

        /**
         * @brief Initializes a new instance with the specified rotation rate and timestamp.
         *
         * @param rotationRate Angular velocity, in radians per second, for each axis.
         * @param timestamp Reading timestamp.
         */
        GyroscopeReading(const Vector3& rotationRate, const System::DateTimeOffset& timestamp);

        /**
         * @brief Gets the angular velocity, in radians per second, for each axis.
         *
         * @return Rotation rate vector.
         */
        [[nodiscard]] const Vector3& getRotationRateProperty() const;

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const override;

        /**
         * @brief Returns true if both readings have equal RotationRate and Timestamp.
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        bool operator==(const GyroscopeReading& other) const;

        /**
         * @brief Returns true if the readings differ in RotationRate or Timestamp.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        bool operator!=(const GyroscopeReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * @return String in the format "RotationRate:{X:0 Y:0 Z:0}".
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this reading.
         *
         * @return Hash derived from RotationRate and Timestamp.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.GyroscopeReading"
         */
        NOXNA [[nodiscard]] std::string GetTypeName() const;

    private:
        /**
         * @brief Sets the angular velocity, in radians per second, for each axis.
         *
         * Restricted to Gyroscope, the class that produces readings of this
         * type, matching the real WP7 API's `internal set` (settable only
         * from within Microsoft.Devices.Sensors.dll).
         *
         * @param value New rotation rate vector.
         */
        void setRotationRateProperty(const Vector3& value);

        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * Restricted to Gyroscope; see setRotationRateProperty() for why.
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);
    };
} // namespace Microsoft::Devices::Sensors
