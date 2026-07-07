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
         * Task READINGS-002: get-only, matching the real WP7 API exactly —
         * confirmed via the archived MSDN page (`ff707568`, `public double X
         * { get; }`, no setter of any visibility). Only ever set via the
         * constructor.
         *
         * @return X-axis acceleration.
         */
        [[nodiscard]] double getXProperty() const;

        /**
         * @brief Gets the Y-axis acceleration.
         *
         * Task READINGS-002: get-only, matching the real WP7 API exactly —
         * confirmed via the archived MSDN page (`ff707712`, `public double Y
         * { get; }`, no setter of any visibility). Only ever set via the
         * constructor.
         *
         * @return Y-axis acceleration.
         */
        [[nodiscard]] double getYProperty() const;

        /**
         * @brief Gets the Z-axis acceleration.
         *
         * Task READINGS-002: get-only, matching the real WP7 API exactly —
         * confirmed via the archived MSDN page (`ff708055`, `public double Z
         * { get; }`, no setter of any visibility). Only ever set via the
         * constructor.
         *
         * @return Z-axis acceleration.
         */
        [[nodiscard]] double getZProperty() const;

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * Task READINGS-002: no public setter, matching the real WP7 API —
         * confirmed via the archived MSDN page (`ff707430`,
         * `public DateTimeOffset Timestamp { get; private set; }`). Only
         * ever set via the constructor; a C++ `private set` equivalent
         * would add a method nothing in this class currently calls (the
         * constructor already assigns the private field directly), so none
         * is declared.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const;

        /**
         * @brief Returns true if both instances have equal X, Y, Z, and Timestamp.
         *
         * Task DEV-API-002: a CNA extension, not real XNA/WP7 API — the real
         * `AccelerometerReadingEventArgs` class's own archived MSDN page
         * (`ff707998(v=vs.105)`) lists `Equals`/`GetHashCode`/`ToString` all
         * inherited unmodified from `System.Object` (reference identity),
         * and no equality operator at all. Same CNA-extension rationale as
         * the reading structs' identical members (see
         * `AccelerometerReading::operator==()`'s doc comment, Task
         * `DEV-API-004`) — useful value-equality for a C++ type that has no
         * automatic equivalent, not an attempt to mimic undocumented real
         * behavior.
         *
         * @param other The instance to compare against.
         * @return true if equal; otherwise false.
         */
        NOXNA bool operator==(const AccelerometerReadingEventArgs& other) const;

        /**
         * @brief Returns true if the instances differ in X, Y, Z, or Timestamp.
         *
         * See operator==()'s doc comment — same CNA-extension rationale.
         *
         * @param other The instance to compare against.
         * @return true if not equal; otherwise false.
         */
        NOXNA bool operator!=(const AccelerometerReadingEventArgs& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * Task DEV-API-002: a CNA extension, not real XNA/WP7 API — the real
         * class's `ToString()` is inherited unmodified from
         * `System.Object.ToString()` (returns just the fully qualified type
         * name, not field values).
         *
         * @return String in the format "{X:0 Y:0 Z:0}".
         */
        NOXNA [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this instance.
         *
         * Task DEV-API-002: a CNA extension, not real XNA/WP7 API — the real
         * class's `GetHashCode()` is inherited unmodified from
         * `System.Object.GetHashCode()`.
         *
         * @return Hash derived from X, Y, Z, and Timestamp.
         */
        NOXNA [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         *
         * @return "Microsoft.Devices.Sensors.AccelerometerReadingEventArgs"
         */
        NOXNA [[nodiscard]] std::string GetTypeName() const;
    };
} // namespace Microsoft::Devices::Sensors
