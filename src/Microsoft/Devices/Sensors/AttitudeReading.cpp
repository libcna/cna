// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"

#include <functional>
#include <sstream>

namespace Microsoft::Devices::Sensors
{
    AttitudeReading::AttitudeReading()
        : Pitch_(0.0f),
          Roll_(0.0f),
          Yaw_(0.0f),
          Quaternion_(Quaternion::Identity),
          RotationMatrix_(Matrix::getIdentityProperty()),
          Timestamp_(System::DateTimeOffset())
    {
    }

    AttitudeReading::AttitudeReading(
        float pitch,
        float roll,
        float yaw,
        const Quaternion& quaternion,
        const Matrix& rotationMatrix,
        const System::DateTimeOffset& timestamp)
        : Pitch_(pitch),
          Roll_(roll),
          Yaw_(yaw),
          Quaternion_(quaternion),
          RotationMatrix_(rotationMatrix),
          Timestamp_(timestamp)
    {
    }

    float AttitudeReading::getPitchProperty() const
    {
        return Pitch_;
    }

    void AttitudeReading::setPitchProperty(float value)
    {
        Pitch_ = value;
    }

    float AttitudeReading::getRollProperty() const
    {
        return Roll_;
    }

    void AttitudeReading::setRollProperty(float value)
    {
        Roll_ = value;
    }

    float AttitudeReading::getYawProperty() const
    {
        return Yaw_;
    }

    void AttitudeReading::setYawProperty(float value)
    {
        Yaw_ = value;
    }

    const Quaternion& AttitudeReading::getQuaternionProperty() const
    {
        return Quaternion_;
    }

    void AttitudeReading::setQuaternionProperty(const Quaternion& value)
    {
        Quaternion_ = value;
    }

    const Matrix& AttitudeReading::getRotationMatrixProperty() const
    {
        return RotationMatrix_;
    }

    void AttitudeReading::setRotationMatrixProperty(const Matrix& value)
    {
        RotationMatrix_ = value;
    }

    const System::DateTimeOffset& AttitudeReading::getTimestampProperty() const
    {
        return Timestamp_;
    }

    void AttitudeReading::setTimestampProperty(const System::DateTimeOffset& value)
    {
        Timestamp_ = value;
    }

    bool AttitudeReading::operator==(const AttitudeReading& other) const
    {
        return Pitch_ == other.Pitch_
            && Roll_ == other.Roll_
            && Yaw_ == other.Yaw_
            && Quaternion_ == other.Quaternion_
            && RotationMatrix_ == other.RotationMatrix_
            && Timestamp_ == other.Timestamp_;
    }

    bool AttitudeReading::operator!=(const AttitudeReading& other) const
    {
        return !(*this == other);
    }

    std::string AttitudeReading::ToString() const
    {
        std::ostringstream s;
        s << "Pitch:" << Pitch_ << " Roll:" << Roll_ << " Yaw:" << Yaw_;
        return s.str();
    }

    std::size_t AttitudeReading::GetHashCode() const
    {
        std::size_t h = std::hash<float>{}(Pitch_);
        h ^= std::hash<float>{}(Roll_) << 1u;
        h ^= std::hash<float>{}(Yaw_) << 1u;
        h ^= static_cast<std::size_t>(Quaternion_.GetHashCode()) << 1u;
        h ^= static_cast<std::size_t>(RotationMatrix_.GetHashCode()) << 1u;
        h ^= std::hash<std::int64_t>{}(Timestamp_.getUtcTicksProperty()) << 1u;
        return h;
    }

    std::string AttitudeReading::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.AttitudeReading";
    }
} // namespace Microsoft::Devices::Sensors
