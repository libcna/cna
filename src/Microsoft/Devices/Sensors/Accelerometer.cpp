//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"

#include <algorithm>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_sensor.h>

#include "CNA/Platform.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTime.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors {

    void* Accelerometer::g_sensor_ = nullptr;
    std::int64_t Accelerometer::g_sensorId_ = 0;
    int Accelerometer::instanceCount_ = 0;
    bool Accelerometer::eventWatchRegistered_ = false;
    std::vector<Accelerometer*> Accelerometer::startedInstances_;

    bool Accelerometer::EnsureSensorSubsystemInitialized()
    {
        if (SDL_WasInit(SDL_INIT_SENSOR)) {
            return true;
        }

        return SDL_InitSubSystem(SDL_INIT_SENSOR);
    }

    void* Accelerometer::OpenDefaultAccelerometer()
    {
        int sensorCount = 0;
        SDL_SensorID* sensors = SDL_GetSensors(&sensorCount);

        if (sensors == nullptr || sensorCount <= 0) {
            if (sensors != nullptr) {
                SDL_free(sensors);
            }
            return nullptr;
        }

        SDL_Sensor* openedSensor = nullptr;
        SDL_SensorID openedSensorId = 0;

        for (int i = 0; i < sensorCount; ++i) {
            const SDL_SensorID sensorId = sensors[i];

            SDL_Sensor* sensor = SDL_OpenSensor(sensorId);
            if (!sensor) {
                continue;
            }

            if (SDL_GetSensorType(sensor) == SDL_SENSOR_ACCEL) {
                openedSensor = sensor;
                openedSensorId = sensorId;
                break;
            }

            SDL_CloseSensor(sensor);
        }

        SDL_free(sensors);

        if (openedSensor != nullptr) {
            g_sensorId_ = static_cast<std::int64_t>(openedSensorId);
        }

        return static_cast<void*>(openedSensor);
    }

    void Accelerometer::RegisterEventWatchIfNeeded()
    {
        if (!eventWatchRegistered_) {
            const SDL_EventFilter eventFilter =
                reinterpret_cast<SDL_EventFilter>(&Accelerometer::SensorEventWatch);
            SDL_AddEventWatch(eventFilter, nullptr);
            eventWatchRegistered_ = true;
        }
    }

    void Accelerometer::UnregisterEventWatchIfNeeded()
    {
        if (eventWatchRegistered_ && startedInstances_.empty()) {
            const SDL_EventFilter eventFilter =
                reinterpret_cast<SDL_EventFilter>(&Accelerometer::SensorEventWatch);
            SDL_RemoveEventWatch(eventFilter, nullptr);
            eventWatchRegistered_ = false;
        }
    }

    bool Accelerometer::SensorEventWatch(void* userdata, void* eventData)
    {
        (void)userdata;

        SDL_Event* event = static_cast<SDL_Event*>(eventData);

        if (event == nullptr) {
            return true;
        }

        if (event->type != SDL_EVENT_SENSOR_UPDATE) {
            return true;
        }

        const std::int64_t sensorId = static_cast<std::int64_t>(event->sensor.which);
        const std::uint64_t timestampNs = static_cast<std::uint64_t>(SDL_GetTicksNS());

        for (Accelerometer* accelerometer : startedInstances_) {
            if (accelerometer != nullptr) {
                accelerometer->ProcessSensorUpdateEvent(
                    sensorId,
                    event->sensor.data[0],
                    event->sensor.data[1],
                    event->sensor.data[2],
                    timestampNs
                );
            }
        }

        return true;
    }

    bool Accelerometer::getIsSupportedProperty()
    {
        const CNA::Platform currentPlatform = CNA::getCurrentPlatform();

        if (!(currentPlatform == CNA::Platform::Android ||
              currentPlatform == CNA::Platform::iOS ||
              currentPlatform == CNA::Platform::Desktop)) {
            return false;
        }

        if (!EnsureSensorSubsystemInitialized()) {
            return false;
        }

        int sensorCount = 0;
        SDL_SensorID* sensors = SDL_GetSensors(&sensorCount);

        if (sensors == nullptr || sensorCount <= 0) {
            if (sensors != nullptr) {
                SDL_free(sensors);
            }
            return false;
        }

        bool supported = false;

        for (int i = 0; i < sensorCount; ++i) {
            SDL_Sensor* sensor = SDL_OpenSensor(sensors[i]);
            if (!sensor) {
                continue;
            }

            if (SDL_GetSensorType(sensor) == SDL_SENSOR_ACCEL) {
                supported = true;
                SDL_CloseSensor(sensor);
                break;
            }

            SDL_CloseSensor(sensor);
        }

        SDL_free(sensors);
        return supported;
    }

    SensorState Accelerometer::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");
        return state_;
    }

    Accelerometer::Accelerometer()
        : state_(SensorState::NotSupported),
          started_(false)
    {
        if (instanceCount_ >= MaxSensorCount) {
            throw SensorFailedException(
                "The limit of 10 simultaneous instances of the Accelerometer class per application has been exceeded.");
        }

        ++instanceCount_;
        state_ = getIsSupportedProperty() ? SensorState::Initializing : SensorState::NotSupported;
    }

    Accelerometer::~Accelerometer()
    {
        if (!getIsDisposedProperty()) {
            Dispose(true);
        }
    }

    void Accelerometer::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

        if (started_) {
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. Data acquisition already started.");
        }

        if (!EnsureSensorSubsystemInitialized()) {
            state_ = SensorState::NotSupported;
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. SDL sensor subsystem initialization failed.");
        }

        if (g_sensor_ == nullptr) {
            g_sensor_ = OpenDefaultAccelerometer();
        }

        if (g_sensor_ == nullptr) {
            state_ = SensorState::NotSupported;
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. No default sensor found.");
        }

        started_ = true;
        state_ = SensorState::Ready;

        if (std::find(startedInstances_.begin(), startedInstances_.end(), this) == startedInstances_.end()) {
            startedInstances_.push_back(this);
        }

        RegisterEventWatchIfNeeded();
    }

    void Accelerometer::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

        if (started_) {
            auto it = std::find(startedInstances_.begin(), startedInstances_.end(), this);
            if (it != startedInstances_.end()) {
                startedInstances_.erase(it);
            }
        }

        started_ = false;
        state_ = SensorState::Disabled;

        UnregisterEventWatchIfNeeded();
    }

    void Accelerometer::Dispose(bool disposing)
    {
        if (!getIsDisposedProperty() && disposing) {
            if (started_) {
                Stop();
            }

            --instanceCount_;
            if (instanceCount_ < 0) {
                instanceCount_ = 0;
            }

            if (instanceCount_ == 0) {
                startedInstances_.clear();
                UnregisterEventWatchIfNeeded();

                if (g_sensor_ != nullptr) {
                    SDL_CloseSensor(static_cast<SDL_Sensor*>(g_sensor_));
                    g_sensor_ = nullptr;
                    g_sensorId_ = 0;
                }

                if (SDL_WasInit(SDL_INIT_SENSOR)) {
                    SDL_QuitSubSystem(SDL_INIT_SENSOR);
                }
            }
        }

        SensorBase<AccelerometerReading>::Dispose(disposing);
    }

    void Accelerometer::ProcessSensorUpdateEvent(
        std::int64_t sensorId,
        float x,
        float y,
        float z,
        std::uint64_t timestampNs)
    {
        if (!started_) {
            return;
        }

        if (getIsDisposedProperty()) {
            return;
        }

        if (g_sensor_ == nullptr) {
            return;
        }

        if (sensorId != g_sensorId_) {
            return;
        }

        AccelerometerReading accelerometerReading;

        constexpr float StandardGravity = 9.80665f;

        const bool valid = true;
        setIsDataValidProperty(valid);

        if (getIsDataValidProperty()) {
            Microsoft::Xna::Framework::Vector3 acceleration(
                x / StandardGravity,
                y / StandardGravity,
                z / StandardGravity
            );

            accelerometerReading.setAccelerationProperty(acceleration);

            const SharpRuntime::longcs ticks = static_cast<SharpRuntime::longcs>(timestampNs / 100);

            System::DateTime dateTime(ticks);
            System::DateTimeOffset timestamp(dateTime, System::TimeSpan::Zero);
            accelerometerReading.setTimestampProperty(timestamp);
        }

        setCurrentValueProperty(accelerometerReading);
    }

    GetTypeNameCPP(Accelerometer, "Microsoft::Devices::Sensors::Accelerometer")

} // namespace Microsoft::Devices::Sensors