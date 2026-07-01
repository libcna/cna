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

    /** @brief Represents one compass sensor reading with heading and magnetometer data. */
    class CompassReading : public ISensorReading
    {
    private:
        DEF_MEMBER(double, HeadingAccuracy)
        DEF_MEMBER(double, MagneticHeading)
        DEF_MEMBER(Vector3, MagnetometerReading)
        DEF_MEMBER(System::DateTimeOffset, Timestamp)
        DEF_MEMBER(double, TrueHeading)

    public:
        /**
         * @brief Initializes a new instance with default heading, magnetometer, and timestamp values.
         */
        CompassReading();

        /**
         * @brief Initializes a new instance with the specified heading, magnetometer, and timestamp values.
         *
         * @param headingAccuracy Accuracy of the heading reading, in degrees.
         * @param magneticHeading Heading, in degrees, measured relative to magnetic north.
         * @param magnetometerReading Raw magnetometer reading, in micro-teslas (uT), for each 3D axis.
         * @param timestamp Reading timestamp.
         * @param trueHeading Heading, in degrees, measured relative to true north.
         */
        CompassReading(
            double headingAccuracy,
            double magneticHeading,
            const Vector3& magnetometerReading,
            const System::DateTimeOffset& timestamp,
            double trueHeading);

        /**
         * @brief Gets the accuracy of the heading reading, in degrees.
         *
         * @return Heading accuracy, in degrees.
         */
        [[nodiscard]] double getHeadingAccuracyProperty() const;

        /**
         * @brief Sets the accuracy of the heading reading, in degrees.
         *
         * @param value New heading accuracy, in degrees.
         */
        void setHeadingAccuracyProperty(double value);

        /**
         * @brief Gets the heading, in degrees, measured relative to magnetic north.
         *
         * @return Magnetic heading, in degrees.
         */
        [[nodiscard]] double getMagneticHeadingProperty() const;

        /**
         * @brief Sets the heading, in degrees, measured relative to magnetic north.
         *
         * @param value New magnetic heading, in degrees.
         */
        void setMagneticHeadingProperty(double value);

        /**
         * @brief Gets the raw magnetometer reading, in micro-teslas (uT), for each 3D axis.
         *
         * @return Magnetometer reading vector.
         */
        [[nodiscard]] const Vector3& getMagnetometerReadingProperty() const;

        /**
         * @brief Sets the raw magnetometer reading, in micro-teslas (uT), for each 3D axis.
         *
         * @param value New magnetometer reading vector.
         */
        void setMagnetometerReadingProperty(const Vector3& value);

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
         * @brief Gets the heading, in degrees, measured relative to true north.
         *
         * @return True heading, in degrees.
         */
        [[nodiscard]] double getTrueHeadingProperty() const;

        /**
         * @brief Sets the heading, in degrees, measured relative to true north.
         *
         * @param value New true heading, in degrees.
         */
        void setTrueHeadingProperty(double value);

        /**
         * @brief Returns true if both readings have equal heading, magnetometer, and timestamp values.
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        bool operator==(const CompassReading& other) const;

        /**
         * @brief Returns true if the readings differ in any heading, magnetometer, or timestamp value.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        bool operator!=(const CompassReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * @return String in the format "MagneticHeading:0 TrueHeading:0 HeadingAccuracy:0 MagnetometerReading:{X:0 Y:0 Z:0}".
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
         * @return "Microsoft.Devices.Sensors.CompassReading"
         */
        NOXNA [[nodiscard]] std::string GetTypeName() const;
    };
} // namespace Microsoft::Devices::Sensors
