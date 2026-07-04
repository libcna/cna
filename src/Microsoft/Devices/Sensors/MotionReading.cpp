// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/MotionReading.hpp"

#include <functional>
#include <sstream>

namespace Microsoft::Devices::Sensors
{
    MotionReading::MotionReading()
        : Attitude_(AttitudeReading()),
          DeviceAcceleration_(Vector3()),
          DeviceRotationRate_(Vector3()),
          Gravity_(Vector3()),
          Timestamp_(System::DateTimeOffset())
    {
    }

    MotionReading::MotionReading(
        const AttitudeReading& attitude,
        const Vector3& deviceAcceleration,
        const Vector3& deviceRotationRate,
        const Vector3& gravity,
        const System::DateTimeOffset& timestamp)
        : Attitude_(attitude),
          DeviceAcceleration_(deviceAcceleration),
          DeviceRotationRate_(deviceRotationRate),
          Gravity_(gravity),
          Timestamp_(timestamp)
    {
    }

    const AttitudeReading& MotionReading::getAttitudeProperty() const
    {
        return Attitude_;
    }

    void MotionReading::setAttitudeProperty(const AttitudeReading& value)
    {
        Attitude_ = value;
    }

    const Vector3& MotionReading::getDeviceAccelerationProperty() const
    {
        return DeviceAcceleration_;
    }

    void MotionReading::setDeviceAccelerationProperty(const Vector3& value)
    {
        DeviceAcceleration_ = value;
    }

    const Vector3& MotionReading::getDeviceRotationRateProperty() const
    {
        return DeviceRotationRate_;
    }

    void MotionReading::setDeviceRotationRateProperty(const Vector3& value)
    {
        DeviceRotationRate_ = value;
    }

    const Vector3& MotionReading::getGravityProperty() const
    {
        return Gravity_;
    }

    void MotionReading::setGravityProperty(const Vector3& value)
    {
        Gravity_ = value;
    }

    const System::DateTimeOffset& MotionReading::getTimestampProperty() const
    {
        return Timestamp_;
    }

    void MotionReading::setTimestampProperty(const System::DateTimeOffset& value)
    {
        Timestamp_ = value;
    }

    bool MotionReading::operator==(const MotionReading& other) const
    {
        return Attitude_ == other.Attitude_
            && DeviceAcceleration_ == other.DeviceAcceleration_
            && DeviceRotationRate_ == other.DeviceRotationRate_
            && Gravity_ == other.Gravity_
            && Timestamp_ == other.Timestamp_;
    }

    bool MotionReading::operator!=(const MotionReading& other) const
    {
        return !(*this == other);
    }

    std::string MotionReading::ToString() const
    {
        std::ostringstream s;
        s << "DeviceAcceleration:" << DeviceAcceleration_.ToString()
          << " Gravity:" << Gravity_.ToString();
        return s.str();
    }

    std::size_t MotionReading::GetHashCode() const
    {
        std::size_t h = Attitude_.GetHashCode();
        h ^= static_cast<std::size_t>(DeviceAcceleration_.GetHashCode()) << 1u;
        h ^= static_cast<std::size_t>(DeviceRotationRate_.GetHashCode()) << 1u;
        h ^= static_cast<std::size_t>(Gravity_.GetHashCode()) << 1u;
        h ^= std::hash<std::int64_t>{}(Timestamp_.getUtcTicksProperty()) << 1u;
        return h;
    }

    std::string MotionReading::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.MotionReading";
    }
} // namespace Microsoft::Devices::Sensors
