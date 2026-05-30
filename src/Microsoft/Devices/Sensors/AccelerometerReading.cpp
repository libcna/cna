//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"

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
} // namespace Microsoft::Devices::Sensors
