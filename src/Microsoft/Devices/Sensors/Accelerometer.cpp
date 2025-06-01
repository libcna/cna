//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/Accelerometer.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL.h>

#include "CNA/Platform.h"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.h"
#include "Microsoft/Devices/Sensors/SensorFailedException.h"

namespace Microsoft::Devices::Sensors {
    SDL_Sensor* Accelerometer::g_sensor = nullptr;

    ////
    int Accelerometer::instanceCount = 0;

    bool Accelerometer::getIsSupported() {
        CNA::Platform currentPlatform = CNA::getCurrentPlatform();
        return currentPlatform == CNA::Android || currentPlatform == CNA::iOS ;
        //TODO: Implement this.
    }

    [[nodiscard]] SensorState Accelerometer::getState() const {
        if (!SDL_WasInit(SDL_INIT_SENSOR)) {
            return SensorState::NotSupported;
        }

        int sensorCount = 0;
        SDL_SensorID *sensors = SDL_GetSensors(&sensorCount);

        if (sensorCount <= 0 || sensors == nullptr) {
            return SensorState::NoData;
        }

        SDL_Sensor *sensor = SDL_OpenSensor(sensors[0]);
        if (!sensor) {
            return SensorState::NoPermissions;
        }

        SDL_CloseSensor(sensor);
        return SensorState::Ready;
    }

    Accelerometer::Accelerometer() {
        if (instanceCount >=Accelerometer::MaxSensorCount)
            throw SensorFailedException("The limit of 10 simultaneous instances of the Accelerometer class per application has been exceeded.");
        ++instanceCount;
        instances.push_back(*this);
    }

    void Accelerometer::Start() {
        if (g_sensor) {
            return;
        }

        if (SDL_InitSubSystem(SDL_INIT_SENSOR) < 0) {
            SDL_Log("Failed to initialize SDL sensor subsystem: %s", SDL_GetError());
            throw Microsoft::Devices::Sensors::AccelerometerFailedException();
        }

        int numSensors = 0;
        SDL_SensorID * sensors = SDL_GetSensors(&numSensors);
        for (int i = 0; i < numSensors; ++i) {
            ;
            if (SDL_GetSensorType(SDL_GetSensorFromID(sensors[i])) == SDL_SENSOR_ACCEL) {
                g_sensor = SDL_OpenSensor(i);
                if (!g_sensor) {
                    SDL_Log("Failed to open accelerometer sensor: %s", SDL_GetError());
                }
                break;
            }
        }
        SDL_free(sensors);

        if (!g_sensor) {
            SDL_Log("No accelerometer sensor found.");
            throw AccelerometerFailedException();
        }
    }

    void Accelerometer::Stop() {
        try {
            if (g_sensor) {
                SDL_CloseSensor(g_sensor);
                g_sensor = nullptr;
            }

            SDL_QuitSubSystem(SDL_INIT_SENSOR);
        } catch (const std::exception& ex) {
            throw AccelerometerFailedException();
        }
    }

    void Accelerometer::Dispose(bool disposing) {
        if (disposing) {
            Stop();
        }
        SensorBase::Dispose(disposing);
    }
} // Microsoft::Devices::Sensors