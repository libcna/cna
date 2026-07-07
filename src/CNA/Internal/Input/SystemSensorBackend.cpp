// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SystemSensorBackend.hpp"

#include <SDL3/SDL.h>

namespace CNA::Internal::Input
{
    namespace
    {
        CNA::Input::SensorTypeEXT sdl_sensor_type_to_ext(SDL_SensorType type)
        {
            using CNA::Input::SensorTypeEXT;
            switch (type)
            {
            case SDL_SENSOR_ACCEL:   return SensorTypeEXT::Accelerometer;
            case SDL_SENSOR_GYRO:    return SensorTypeEXT::Gyroscope;
            case SDL_SENSOR_ACCEL_L: return SensorTypeEXT::AccelerometerLeft;
            case SDL_SENSOR_GYRO_L:  return SensorTypeEXT::GyroscopeLeft;
            case SDL_SENSOR_ACCEL_R: return SensorTypeEXT::AccelerometerRight;
            case SDL_SENSOR_GYRO_R:  return SensorTypeEXT::GyroscopeRight;
            default:                 return SensorTypeEXT::Unknown;
            }
        }

        SDL_SensorType ext_sensor_type_to_sdl(CNA::Input::SensorTypeEXT type)
        {
            using CNA::Input::SensorTypeEXT;
            switch (type)
            {
            case SensorTypeEXT::Accelerometer:      return SDL_SENSOR_ACCEL;
            case SensorTypeEXT::Gyroscope:          return SDL_SENSOR_GYRO;
            case SensorTypeEXT::AccelerometerLeft:  return SDL_SENSOR_ACCEL_L;
            case SensorTypeEXT::GyroscopeLeft:      return SDL_SENSOR_GYRO_L;
            case SensorTypeEXT::AccelerometerRight: return SDL_SENSOR_ACCEL_R;
            case SensorTypeEXT::GyroscopeRight:     return SDL_SENSOR_GYRO_R;
            default:                                return SDL_SENSOR_UNKNOWN;
            }
        }

        struct RealSystemSensorBackend final : ISystemSensorBackend
        {
            std::vector<CNA::Input::SensorInfoEXT> GetSensors() override
            {
                std::vector<CNA::Input::SensorInfoEXT> sensors;
                int count = 0;
                SDL_SensorID* ids = SDL_GetSensors(&count);
                if (ids != nullptr)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        const char* name = SDL_GetSensorNameForID(ids[i]);
                        sensors.push_back({ids[i], name ? name : "",
                                           sdl_sensor_type_to_ext(SDL_GetSensorTypeForID(ids[i]))});
                    }
                    SDL_free(ids);
                }
                return sensors;
            }

            bool ReadSensor(CNA::Input::SensorTypeEXT type,
                            Microsoft::Xna::Framework::Vector3& out) override
            {
                const SDL_SensorType wanted = ext_sensor_type_to_sdl(type);
                if (wanted == SDL_SENSOR_UNKNOWN)
                    return false;

                bool read = false;
                int count = 0;
                SDL_SensorID* ids = SDL_GetSensors(&count);
                if (ids != nullptr)
                {
                    for (int i = 0; i < count && !read; ++i)
                    {
                        if (SDL_GetSensorTypeForID(ids[i]) != wanted)
                            continue;
                        SDL_Sensor* sensor = SDL_OpenSensor(ids[i]);
                        if (sensor == nullptr)
                            continue;
                        float data[3] = {0.0f, 0.0f, 0.0f};
                        if (SDL_GetSensorData(sensor, data, 3))
                        {
                            out = Microsoft::Xna::Framework::Vector3(data[0], data[1], data[2]);
                            read = true;
                        }
                        SDL_CloseSensor(sensor);
                    }
                    SDL_free(ids);
                }
                return read;
            }
        };

        RealSystemSensorBackend g_realBackend;
        ISystemSensorBackend*   g_currentBackend = &g_realBackend;
    }

    ISystemSensorBackend& system_sensor_backend()
    {
        return *g_currentBackend;
    }

    void SetSystemSensorBackendForTests(ISystemSensorBackend* backend)
    {
        g_currentBackend = backend ? backend : &g_realBackend;
    }
}
