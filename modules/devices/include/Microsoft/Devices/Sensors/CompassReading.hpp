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
        friend class Compass;

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
         * @brief Gets the heading, in degrees, measured relative to magnetic north.
         *
         * @return Magnetic heading, in degrees.
         */
        [[nodiscard]] double getMagneticHeadingProperty() const;

        /**
         * @brief Gets the raw magnetometer reading, in micro-teslas (uT), for each 3D axis.
         *
         * @return Magnetometer reading vector.
         */
        [[nodiscard]] const Vector3& getMagnetometerReadingProperty() const;

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const override;

        /**
         * @brief Gets the heading, in degrees, measured relative to true (geographic) north.
         *
         * Task COMP2-008 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
         * every backend in this codebase currently reports the same value as
         * `MagneticHeadingProperty` — computing a genuine true-north
         * correction requires magnetic declination, which depends on
         * geographic location (`System.Device.Location`, a separate WP7
         * assembly `Microsoft::Devices::Sensors` does not implement — see
         * `docs/location-future-plans/plan.md`). This never fabricates an assumed
         * declination: absent real location data, magnetic heading is the
         * most accurate honest answer available, not geographic heading
         * silently presented as if it were.
         *
         * @return True heading, in degrees — currently identical to `MagneticHeadingProperty` on every backend, pending real declination data.
         */
        [[nodiscard]] double getTrueHeadingProperty() const;

        /**
         * @brief Returns true if both readings have equal heading, magnetometer, and timestamp values.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — see
         * AccelerometerReading::operator==()'s doc comment for the full
         * rationale (verified against `CompassReading`'s own archived MSDN
         * page, `hh203072(v=vs.105)`, same `ValueType`-inherited pattern).
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        CNAEXT bool operator==(const CompassReading& other) const;

        /**
         * @brief Returns true if the readings differ in any heading, magnetometer, or timestamp value.
         *
         * See operator==()'s doc comment — same CNA-extension rationale.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        CNAEXT bool operator!=(const CompassReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — see
         * AccelerometerReading::ToString()'s doc comment for the full
         * rationale.
         *
         * @return String in the format "MagneticHeading:0 TrueHeading:0 HeadingAccuracy:0 MagnetometerReading:{X:0 Y:0 Z:0}".
         */
        CNAEXT [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this reading.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — see
         * AccelerometerReading::GetHashCode()'s doc comment for the full
         * rationale.
         *
         * @return Hash derived from all reading values.
         */
        CNAEXT [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.CompassReading"
         */
        CNAEXT [[nodiscard]] std::string GetTypeName() const;

    private:
        /**
         * @brief Sets the accuracy of the heading reading, in degrees.
         *
         * Restricted to Compass, the class that produces readings of this
         * type, matching the real WP7 API's `internal set` (settable only
         * from within Microsoft.Devices.Sensors.dll).
         *
         * @param value New heading accuracy, in degrees.
         */
        void setHeadingAccuracyProperty(double value);

        /**
         * @brief Sets the heading, in degrees, measured relative to magnetic north.
         *
         * Restricted to Compass; see setHeadingAccuracyProperty() for why.
         *
         * @param value New magnetic heading, in degrees.
         */
        void setMagneticHeadingProperty(double value);

        /**
         * @brief Sets the raw magnetometer reading, in micro-teslas (uT), for each 3D axis.
         *
         * Restricted to Compass; see setHeadingAccuracyProperty() for why.
         *
         * @param value New magnetometer reading vector.
         */
        void setMagnetometerReadingProperty(const Vector3& value);

        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * Restricted to Compass; see setHeadingAccuracyProperty() for why.
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);

        /**
         * @brief Sets the heading, in degrees, measured relative to true north.
         *
         * Restricted to Compass; see setHeadingAccuracyProperty() for why.
         *
         * @param value New true heading, in degrees.
         */
        void setTrueHeadingProperty(double value);
    };
} // namespace Microsoft::Devices::Sensors
