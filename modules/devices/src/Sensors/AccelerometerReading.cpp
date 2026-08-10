// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"

#include <functional>
#include <sstream>

namespace Microsoft::Devices::Sensors
{
    AccelerometerReading::AccelerometerReading()
        : Timestamp_(System::DateTimeOffset()),
          Acceleration_(Vector3())
    {
    }

    AccelerometerReading::AccelerometerReading(
        const System::DateTimeOffset& timestamp,
        const Vector3& acceleration)
        : Timestamp_(timestamp),
          Acceleration_(acceleration)
    {
    }

    const System::DateTimeOffset& AccelerometerReading::getTimestampProperty() const
    {
        return Timestamp_;
    }

    void AccelerometerReading::setTimestampProperty(const System::DateTimeOffset& value)
    {
        Timestamp_ = value;
    }

    const Vector3& AccelerometerReading::getAccelerationProperty() const
    {
        return Acceleration_;
    }

    void AccelerometerReading::setAccelerationProperty(const Vector3& value)
    {
        Acceleration_ = value;
    }

    bool AccelerometerReading::operator==(const AccelerometerReading& other) const
    {
        return Acceleration_ == other.Acceleration_ && Timestamp_ == other.Timestamp_;
    }

    bool AccelerometerReading::operator!=(const AccelerometerReading& other) const
    {
        return !(*this == other);
    }

    std::string AccelerometerReading::ToString() const
    {
        std::ostringstream s;
        s << "Acceleration:" << Acceleration_.ToString();
        return s.str();
    }

    std::size_t AccelerometerReading::GetHashCode() const
    {
        const std::size_t h1 = static_cast<std::size_t>(Acceleration_.GetHashCode());
        const std::size_t h2 = std::hash<std::int64_t>{}(Timestamp_.getUtcTicksProperty());
        return h1 ^ (h2 << 1u);
    }

    std::string AccelerometerReading::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.AccelerometerReading";
    }
} // namespace Microsoft::Devices::Sensors
