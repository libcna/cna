// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/Prop.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Legacy WP7 7.0 event data for the Accelerometer.ReadingChanged event.
     *
     * Superseded by the WP7 7.1 SensorBase pattern
     * (CurrentValueChanged / SensorReadingEventArgs<AccelerometerReading>) used by
     * the current CNA Accelerometer implementation. This class exists for API
     * completeness and is not raised by that implementation.
     */
    class AccelerometerReadingEventArgs : public System::EventArgs
    {
    private:
        DEF_MEMBER(double, X)
        DEF_MEMBER(double, Y)
        DEF_MEMBER(double, Z)
        DEF_MEMBER(System::DateTimeOffset, Timestamp)

    public:
        /**
         * @brief Initializes a new instance with zero acceleration and a default timestamp.
         */
        AccelerometerReadingEventArgs();

        /**
         * @brief Initializes a new instance with the specified acceleration and timestamp.
         *
         * @param x X-axis acceleration.
         * @param y Y-axis acceleration.
         * @param z Z-axis acceleration.
         * @param timestamp Reading timestamp.
         */
        AccelerometerReadingEventArgs(double x, double y, double z, const System::DateTimeOffset& timestamp);

        /**
         * @brief Gets the X-axis acceleration.
         *
         * @return X-axis acceleration.
         */
        [[nodiscard]] double getXProperty() const;

        /**
         * @brief Sets the X-axis acceleration.
         *
         * @param value New X-axis acceleration.
         */
        void setXProperty(double value);

        /**
         * @brief Gets the Y-axis acceleration.
         *
         * @return Y-axis acceleration.
         */
        [[nodiscard]] double getYProperty() const;

        /**
         * @brief Sets the Y-axis acceleration.
         *
         * @param value New Y-axis acceleration.
         */
        void setYProperty(double value);

        /**
         * @brief Gets the Z-axis acceleration.
         *
         * @return Z-axis acceleration.
         */
        [[nodiscard]] double getZProperty() const;

        /**
         * @brief Sets the Z-axis acceleration.
         *
         * @param value New Z-axis acceleration.
         */
        void setZProperty(double value);

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const;

        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);

        /**
         * @brief Returns true if both instances have equal X, Y, Z, and Timestamp.
         *
         * @param other The instance to compare against.
         * @return true if equal; otherwise false.
         */
        bool operator==(const AccelerometerReadingEventArgs& other) const;

        /**
         * @brief Returns true if the instances differ in X, Y, Z, or Timestamp.
         *
         * @param other The instance to compare against.
         * @return true if not equal; otherwise false.
         */
        bool operator!=(const AccelerometerReadingEventArgs& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * @return String in the format "{X:0 Y:0 Z:0}".
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this instance.
         *
         * @return Hash derived from X, Y, Z, and Timestamp.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.AccelerometerReadingEventArgs"
         */
        NOXNA [[nodiscard]] std::string GetTypeName() const;
    };
} // namespace Microsoft::Devices::Sensors
