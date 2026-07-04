// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"

namespace Microsoft::Devices::Sensors
{
    using Xna::Framework::Vector3;

    /** @brief Represents one fused device-motion sensor reading. */
    class MotionReading : public ISensorReading
    {
        friend class Motion;

    private:
        DEF_MEMBER(AttitudeReading, Attitude)
        DEF_MEMBER(Vector3, DeviceAcceleration)
        DEF_MEMBER(Vector3, DeviceRotationRate)
        DEF_MEMBER(Vector3, Gravity)
        DEF_MEMBER(System::DateTimeOffset, Timestamp)

    public:
        /**
         * @brief Initializes a new instance with default attitude, zero vectors, and a default timestamp.
         */
        MotionReading();

        /**
         * @brief Initializes a new instance with the specified motion values.
         *
         * @param attitude Fused device orientation.
         * @param deviceAcceleration Linear acceleration excluding gravity, in g, for each axis.
         * @param deviceRotationRate Angular velocity, in radians per second, for each axis.
         * @param gravity Gravity vector, in g, for each axis.
         * @param timestamp Reading timestamp.
         */
        MotionReading(
            const AttitudeReading& attitude,
            const Vector3& deviceAcceleration,
            const Vector3& deviceRotationRate,
            const Vector3& gravity,
            const System::DateTimeOffset& timestamp);

        /**
         * @brief Gets the fused device orientation.
         *
         * @return Attitude reading.
         */
        [[nodiscard]] const AttitudeReading& getAttitudeProperty() const;

        /**
         * @brief Gets the linear acceleration excluding gravity, in g, for each axis.
         *
         * @return Device acceleration vector.
         */
        [[nodiscard]] const Vector3& getDeviceAccelerationProperty() const;

        /**
         * @brief Gets the angular velocity, in radians per second, for each axis.
         *
         * @return Device rotation rate vector.
         */
        [[nodiscard]] const Vector3& getDeviceRotationRateProperty() const;

        /**
         * @brief Gets the gravity vector, in g, for each axis.
         *
         * @return Gravity vector.
         */
        [[nodiscard]] const Vector3& getGravityProperty() const;

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const override;

        /**
         * @brief Returns true if both readings have equal attitude, motion vectors, and timestamp.
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        bool operator==(const MotionReading& other) const;

        /**
         * @brief Returns true if the readings differ in attitude, any motion vector, or timestamp.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        bool operator!=(const MotionReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * @return String in the format "DeviceAcceleration:{X:0 Y:0 Z:0} Gravity:{X:0 Y:0 Z:0}".
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this reading.
         *
         * @return Hash derived from all reading values.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.MotionReading"
         */
        NOXNA [[nodiscard]] std::string GetTypeName() const;

    private:
        /**
         * @brief Sets the fused device orientation.
         *
         * Restricted to Motion, the class that produces readings of this
         * type, matching the real WP7 API's `internal set` (settable only
         * from within Microsoft.Devices.Sensors.dll).
         *
         * @param value New attitude reading.
         */
        void setAttitudeProperty(const AttitudeReading& value);

        /**
         * @brief Sets the linear acceleration excluding gravity, in g, for each axis.
         *
         * Restricted to Motion; see setAttitudeProperty() for why.
         *
         * @param value New device acceleration vector.
         */
        void setDeviceAccelerationProperty(const Vector3& value);

        /**
         * @brief Sets the angular velocity, in radians per second, for each axis.
         *
         * Restricted to Motion; see setAttitudeProperty() for why.
         *
         * @param value New device rotation rate vector.
         */
        void setDeviceRotationRateProperty(const Vector3& value);

        /**
         * @brief Sets the gravity vector, in g, for each axis.
         *
         * Restricted to Motion; see setAttitudeProperty() for why.
         *
         * @param value New gravity vector.
         */
        void setGravityProperty(const Vector3& value);

        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * Restricted to Motion; see setAttitudeProperty() for why.
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);
    };
} // namespace Microsoft::Devices::Sensors
