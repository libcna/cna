// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/Prop.hpp"
#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "System/DateTimeOffset.hpp"

namespace Microsoft::Devices::Sensors
{
    using Xna::Framework::Matrix;
    using Xna::Framework::Quaternion;

    /** @brief Represents one device attitude (orientation) sensor reading. */
    class AttitudeReading : public ISensorReading
    {
        friend class Motion;

    private:
        DEF_MEMBER(float, Pitch)
        DEF_MEMBER(float, Roll)
        DEF_MEMBER(float, Yaw)
        DEF_MEMBER(Quaternion, Quaternion)
        DEF_MEMBER(Matrix, RotationMatrix)
        DEF_MEMBER(System::DateTimeOffset, Timestamp)

    public:
        /**
         * @brief Initializes a new instance with zero pitch/roll/yaw, identity
         * quaternion, identity rotation matrix, and a default timestamp.
         */
        AttitudeReading();

        /**
         * @brief Initializes a new instance with the specified attitude values.
         *
         * @param pitch Rotation around the X-axis, in radians.
         * @param roll Rotation around the Z-axis, in radians.
         * @param yaw Rotation around the Y-axis, in radians.
         * @param quaternion Orientation expressed as a quaternion.
         * @param rotationMatrix Orientation expressed as a rotation matrix.
         * @param timestamp Reading timestamp.
         */
        AttitudeReading(
            float pitch,
            float roll,
            float yaw,
            const Quaternion& quaternion,
            const Matrix& rotationMatrix,
            const System::DateTimeOffset& timestamp);

        /**
         * @brief Gets the rotation around the X-axis, in radians.
         *
         * @return Pitch, in radians.
         */
        [[nodiscard]] float getPitchProperty() const;

        /**
         * @brief Gets the rotation around the Z-axis, in radians.
         *
         * @return Roll, in radians.
         */
        [[nodiscard]] float getRollProperty() const;

        /**
         * @brief Gets the rotation around the Y-axis, in radians.
         *
         * @return Yaw, in radians.
         */
        [[nodiscard]] float getYawProperty() const;

        /**
         * @brief Gets the device orientation expressed as a quaternion.
         *
         * @return Orientation quaternion.
         */
        [[nodiscard]] const Quaternion& getQuaternionProperty() const;

        /**
         * @brief Gets the device orientation expressed as a rotation matrix.
         *
         * @return Orientation rotation matrix.
         */
        [[nodiscard]] const Matrix& getRotationMatrixProperty() const;

        /**
         * @brief Gets the timestamp of the sensor reading.
         *
         * @return Timestamp of the reading.
         */
        [[nodiscard]] const System::DateTimeOffset& getTimestampProperty() const override;

        /**
         * @brief Returns true if both readings have equal attitude and timestamp values.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — see
         * AccelerometerReading::operator==()'s doc comment for the full
         * rationale (verified against `AttitudeReading`'s own archived MSDN
         * page, `hh220667(v=vs.105)`, same `ValueType`-inherited pattern).
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        CNAEXT bool operator==(const AttitudeReading& other) const;

        /**
         * @brief Returns true if the readings differ in any attitude or timestamp value.
         *
         * See operator==()'s doc comment — same CNA-extension rationale.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        CNAEXT bool operator!=(const AttitudeReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * Task DEV-API-004: a CNA extension, not real XNA/WP7 API — see
         * AccelerometerReading::ToString()'s doc comment for the full
         * rationale.
         *
         * @return String in the format "Pitch:0 Roll:0 Yaw:0".
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
         * @return "Microsoft.Devices.Sensors.AttitudeReading"
         */
        CNAEXT [[nodiscard]] std::string GetTypeName() const;

    private:
        /**
         * @brief Sets the rotation around the X-axis, in radians.
         *
         * Restricted to Motion, the class that produces readings of this
         * type (as MotionReading.Attitude), matching the real WP7 API's
         * `internal set` (settable only from within
         * Microsoft.Devices.Sensors.dll).
         *
         * @param value New pitch, in radians.
         */
        void setPitchProperty(float value);

        /**
         * @brief Sets the rotation around the Z-axis, in radians.
         *
         * Restricted to Motion; see setPitchProperty() for why.
         *
         * @param value New roll, in radians.
         */
        void setRollProperty(float value);

        /**
         * @brief Sets the rotation around the Y-axis, in radians.
         *
         * Restricted to Motion; see setPitchProperty() for why.
         *
         * @param value New yaw, in radians.
         */
        void setYawProperty(float value);

        /**
         * @brief Sets the device orientation expressed as a quaternion.
         *
         * Restricted to Motion; see setPitchProperty() for why.
         *
         * @param value New orientation quaternion.
         */
        void setQuaternionProperty(const Quaternion& value);

        /**
         * @brief Sets the device orientation expressed as a rotation matrix.
         *
         * Restricted to Motion; see setPitchProperty() for why.
         *
         * @param value New orientation rotation matrix.
         */
        void setRotationMatrixProperty(const Matrix& value);

        /**
         * @brief Sets the timestamp of the sensor reading.
         *
         * Restricted to Motion; see setPitchProperty() for why.
         *
         * @param value New timestamp.
         */
        void setTimestampProperty(const System::DateTimeOffset& value);
    };
} // namespace Microsoft::Devices::Sensors
