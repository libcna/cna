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
         * @brief Sets the rotation around the X-axis, in radians.
         *
         * @param value New pitch, in radians.
         */
        void setPitchProperty(float value);

        /**
         * @brief Gets the rotation around the Z-axis, in radians.
         *
         * @return Roll, in radians.
         */
        [[nodiscard]] float getRollProperty() const;

        /**
         * @brief Sets the rotation around the Z-axis, in radians.
         *
         * @param value New roll, in radians.
         */
        void setRollProperty(float value);

        /**
         * @brief Gets the rotation around the Y-axis, in radians.
         *
         * @return Yaw, in radians.
         */
        [[nodiscard]] float getYawProperty() const;

        /**
         * @brief Sets the rotation around the Y-axis, in radians.
         *
         * @param value New yaw, in radians.
         */
        void setYawProperty(float value);

        /**
         * @brief Gets the device orientation expressed as a quaternion.
         *
         * @return Orientation quaternion.
         */
        [[nodiscard]] const Quaternion& getQuaternionProperty() const;

        /**
         * @brief Sets the device orientation expressed as a quaternion.
         *
         * @param value New orientation quaternion.
         */
        void setQuaternionProperty(const Quaternion& value);

        /**
         * @brief Gets the device orientation expressed as a rotation matrix.
         *
         * @return Orientation rotation matrix.
         */
        [[nodiscard]] const Matrix& getRotationMatrixProperty() const;

        /**
         * @brief Sets the device orientation expressed as a rotation matrix.
         *
         * @param value New orientation rotation matrix.
         */
        void setRotationMatrixProperty(const Matrix& value);

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
         * @brief Returns true if both readings have equal attitude and timestamp values.
         *
         * @param other The reading to compare against.
         * @return true if equal; otherwise false.
         */
        bool operator==(const AttitudeReading& other) const;

        /**
         * @brief Returns true if the readings differ in any attitude or timestamp value.
         *
         * @param other The reading to compare against.
         * @return true if not equal; otherwise false.
         */
        bool operator!=(const AttitudeReading& other) const;

        /**
         * @brief Returns a string representation of the reading.
         *
         * @return String in the format "Pitch:0 Roll:0 Yaw:0".
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
         * @return "Microsoft.Devices.Sensors.AttitudeReading"
         */
        NOXNA [[nodiscard]] std::string GetTypeName() const;
    };
} // namespace Microsoft::Devices::Sensors
