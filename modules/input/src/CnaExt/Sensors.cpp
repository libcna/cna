// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Sensors.hpp"

#include "CNA/Internal/Input/SystemSensorBackend.hpp"

namespace CNA::Input
{
    std::vector<SensorInfoEXT> Sensors::GetSensorsEXT()
    {
        return CNA::Internal::Input::system_sensor_backend().GetSensors();
    }

    bool Sensors::GetAccelerometerEXT(Microsoft::Xna::Framework::Vector3& acceleration)
    {
        return CNA::Internal::Input::system_sensor_backend().ReadSensor(
            SensorTypeEXT::Accelerometer, acceleration);
    }

    bool Sensors::GetGyroscopeEXT(Microsoft::Xna::Framework::Vector3& angularVelocity)
    {
        return CNA::Internal::Input::system_sensor_backend().ReadSensor(
            SensorTypeEXT::Gyroscope, angularVelocity);
    }
}
