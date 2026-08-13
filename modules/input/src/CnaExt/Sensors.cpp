// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Sensors.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <limits>

namespace CNA::Input
{
    namespace
    {
        class ScopedSensorSubsystem
        {
        public:
            explicit ScopedSensorSubsystem(CNA::Platform::IPlatform& platform)
                : platform_(&platform)
            {
                try
                {
                    platform_->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                    acquired_ = true;
                }
                catch (const CNA::Platform::PlatformException&)
                {
                    // The public extension has always expressed an unavailable sensor as an
                    // empty list/false, including subsystem initialisation failures.
                }
            }

            ~ScopedSensorSubsystem()
            {
                if (acquired_)
                {
                    platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                }
            }

            [[nodiscard]] bool IsAcquired() const { return acquired_; }

        private:
            CNA::Platform::IPlatform* platform_;
            bool acquired_ = false;
        };

        class ScopedStartedSensor
        {
        public:
            ScopedStartedSensor(CNA::Platform::IPlatformSensors& sensors,
                                const CNA::Platform::SensorKind kind)
                : sensors_(&sensors), kind_(kind)
            {
                sensors_->Start(kind_);
                started_ = true;
            }

            ~ScopedStartedSensor()
            {
                if (started_)
                {
                    // Stop is specified as a no-op for an inactive sensor. It is a teardown call,
                    // so never let a non-conforming implementation throw through this destructor.
                    try
                    {
                        sensors_->Stop(kind_);
                    }
                    catch (...)
                    {
                    }
                }
            }

            ScopedStartedSensor(const ScopedStartedSensor&) = delete;
            ScopedStartedSensor& operator=(const ScopedStartedSensor&) = delete;

        private:
            CNA::Platform::IPlatformSensors* sensors_;
            CNA::Platform::SensorKind kind_;
            bool started_ = false;
        };

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

        /// Reads one sensor with one balanced subsystem/open lifetime.
        ///
        /// The previous SDL-backed path opened, read and closed on every call, so this accessor
        /// has always looked stateless to its caller and must keep doing so. The stop happens
        /// before the subsystem release; reversing that order would close an SDL handle after
        /// its owning subsystem was already gone.
        bool read_sensor(const CNA::Platform::SensorKind kind,
                         Microsoft::Xna::Framework::Vector3& out)
        {
            CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
            CNA::Platform::IPlatformSensors* sensors = platform.GetSensors();
            if (sensors == nullptr)
            {
                return false;
            }

            ScopedSensorSubsystem subsystem(platform);
            if (!subsystem.IsAcquired())
            {
                return false;
            }
            if (!sensors->IsAvailable(kind))
            {
                return false;
            }

            try
            {
                // Availability can change between the query and Start (device unplug). The old
                // backend returned false for both absence and open failure, so keep that shape.
                ScopedStartedSensor started(*sensors, kind);
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
            catch (const CNA::Platform::PlatformException&)
            {
                return false;
            }
        }
    }

    std::vector<SensorInfoEXT> Sensors::GetSensorsEXT()
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        CNA::Platform::IPlatformSensors* sensors = platform.GetSensors();
        if (sensors == nullptr)
        {
            return {};
        }
        ScopedSensorSubsystem subsystem(platform);
        if (!subsystem.IsAcquired())
        {
            return {};
        }

        std::vector<SensorInfoEXT> result;
        for (const CNA::Platform::SensorInfo& sensor : sensors->GetSensors())
        {
            if (sensor.id <= std::numeric_limits<std::uint32_t>::max())
            {
                result.push_back(SensorInfoEXT{static_cast<std::uint32_t>(sensor.id), sensor.name,
                                               to_sensor_type_ext(sensor.kind)});
            }
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
