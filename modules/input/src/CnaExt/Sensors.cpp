// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Sensors.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

namespace CNA::Input
{
    namespace
    {
        SensorTypeEXT to_sensor_type_ext(const CNA::Platform::SensorKind kind)
        {
            // Exhaustive with no default arm: a kind added to the contract without a mapping here
            // becomes a compiler diagnostic rather than a silent Unknown.
            switch (kind)
            {
            case CNA::Platform::SensorKind::Accelerometer:      return SensorTypeEXT::Accelerometer;
            case CNA::Platform::SensorKind::Gyroscope:          return SensorTypeEXT::Gyroscope;
            case CNA::Platform::SensorKind::AccelerometerLeft:  return SensorTypeEXT::AccelerometerLeft;
            case CNA::Platform::SensorKind::GyroscopeLeft:      return SensorTypeEXT::GyroscopeLeft;
            case CNA::Platform::SensorKind::AccelerometerRight: return SensorTypeEXT::AccelerometerRight;
            case CNA::Platform::SensorKind::GyroscopeRight:     return SensorTypeEXT::GyroscopeRight;
            case CNA::Platform::SensorKind::Unknown:            return SensorTypeEXT::Unknown;
            }
            return SensorTypeEXT::Unknown;
        }

        /// Reads one sensor, starting it first.
        ///
        /// The previous SDL-backed path opened, read and closed on every call, so this accessor
        /// has always looked stateless to its caller and must keep doing so.
        ///
        /// It starts on every read rather than remembering. Caching "already started" needs an
        /// identity for the service, and the only one available is its address -- which a
        /// destroyed platform hands back to the next one, so the cache would claim a sensor was
        /// started on a service that has never seen it. A per-frame subsystem with a real
        /// lifetime can hold that state; a static accessor cannot. Starting again is cheap on
        /// every implementation CNA has, and strictly cheaper than the open-read-close this
        /// replaced.
        bool read_sensor(const CNA::Platform::SensorKind kind,
                         Microsoft::Xna::Framework::Vector3& out)
        {
            CNA::Platform::IPlatformSensors* sensors =
                CNA::Platform::GetCurrentPlatform().GetSensors();
            if (sensors == nullptr || !sensors->IsAvailable(kind))
            {
                return false;
            }

            sensors->Start(kind);

            CNA::Platform::SensorReading reading;
            if (!sensors->TryGetReading(kind, reading))
            {
                return false;
            }

            out.X = reading.x;
            out.Y = reading.y;
            out.Z = reading.z;
            return true;
        }
    }

    std::vector<SensorInfoEXT> Sensors::GetSensorsEXT()
    {
        CNA::Platform::IPlatformSensors* sensors =
            CNA::Platform::GetCurrentPlatform().GetSensors();
        if (sensors == nullptr)
        {
            return {};
        }

        std::vector<SensorInfoEXT> result;
        for (const CNA::Platform::SensorInfo& sensor : sensors->GetSensors())
        {
            result.push_back(SensorInfoEXT{static_cast<std::uint32_t>(sensor.id), sensor.name,
                                           to_sensor_type_ext(sensor.kind)});
        }
        return result;
    }

    bool Sensors::GetAccelerometerEXT(Microsoft::Xna::Framework::Vector3& acceleration)
    {
        return read_sensor(CNA::Platform::SensorKind::Accelerometer, acceleration);
    }

    bool Sensors::GetGyroscopeEXT(Microsoft::Xna::Framework::Vector3& angularVelocity)
    {
        return read_sensor(CNA::Platform::SensorKind::Gyroscope, angularVelocity);
    }
}
