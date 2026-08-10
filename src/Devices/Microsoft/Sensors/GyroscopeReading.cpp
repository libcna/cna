// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"

#include <functional>
#include <sstream>

namespace Microsoft::Devices::Sensors
{
    GyroscopeReading::GyroscopeReading()
        : RotationRate_(Vector3()),
          Timestamp_(System::DateTimeOffset())
    {
    }

    GyroscopeReading::GyroscopeReading(
        const Vector3& rotationRate,
        const System::DateTimeOffset& timestamp)
        : RotationRate_(rotationRate),
          Timestamp_(timestamp)
    {
    }

    const Vector3& GyroscopeReading::getRotationRateProperty() const
    {
        return RotationRate_;
    }

    void GyroscopeReading::setRotationRateProperty(const Vector3& value)
    {
        RotationRate_ = value;
    }

    const System::DateTimeOffset& GyroscopeReading::getTimestampProperty() const
    {
        return Timestamp_;
    }

    void GyroscopeReading::setTimestampProperty(const System::DateTimeOffset& value)
    {
        Timestamp_ = value;
    }

    bool GyroscopeReading::operator==(const GyroscopeReading& other) const
    {
        return RotationRate_ == other.RotationRate_ && Timestamp_ == other.Timestamp_;
    }

    bool GyroscopeReading::operator!=(const GyroscopeReading& other) const
    {
        return !(*this == other);
    }

    std::string GyroscopeReading::ToString() const
    {
        std::ostringstream s;
        s << "RotationRate:" << RotationRate_.ToString();
        return s.str();
    }

    std::size_t GyroscopeReading::GetHashCode() const
    {
        const std::size_t h1 = static_cast<std::size_t>(RotationRate_.GetHashCode());
        const std::size_t h2 = std::hash<std::int64_t>{}(Timestamp_.getUtcTicksProperty());
        return h1 ^ (h2 << 1u);
    }

    std::string GyroscopeReading::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.GyroscopeReading";
    }
} // namespace Microsoft::Devices::Sensors
