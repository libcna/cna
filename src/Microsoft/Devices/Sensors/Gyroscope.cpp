// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Gyroscope.hpp"

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

namespace Microsoft::Devices::Sensors
{
    void* Gyroscope::g_sensor_ = nullptr;
    std::int64_t Gyroscope::g_sensorId_ = 0;
    int Gyroscope::instanceCount_ = 0;
    bool Gyroscope::eventWatchRegistered_ = false;
    std::vector<Gyroscope*> Gyroscope::startedInstances_;
    std::mutex Gyroscope::mutex_;
    std::condition_variable Gyroscope::callbackFinished_;

    bool Gyroscope::EnsureSensorSubsystemInitialized()
    {
        if (SDL_WasInit(SDL_INIT_SENSOR))
        {
            return true;
        }

        return SDL_InitSubSystem(SDL_INIT_SENSOR);
    }

    void* Gyroscope::OpenDefaultGyroscope()
    {
        int sensorCount = 0;
        SDL_SensorID* sensors = SDL_GetSensors(&sensorCount);

        if (sensors == nullptr || sensorCount <= 0)
        {
            if (sensors != nullptr)
            {
                SDL_free(sensors);
            }
            return nullptr;
        }

        SDL_Sensor* openedSensor = nullptr;
        SDL_SensorID openedSensorId = 0;

        for (int i = 0; i < sensorCount; ++i)
        {
            const SDL_SensorID sensorId = sensors[i];

            SDL_Sensor* sensor = SDL_OpenSensor(sensorId);
            if (!sensor)
            {
                continue;
            }

            if (SDL_GetSensorType(sensor) == SDL_SENSOR_GYRO)
            {
                openedSensor = sensor;
                openedSensorId = sensorId;
                break;
            }

            SDL_CloseSensor(sensor);
        }

        SDL_free(sensors);

        if (openedSensor != nullptr)
        {
            g_sensorId_ = static_cast<std::int64_t>(openedSensorId);
        }

        return static_cast<void*>(openedSensor);
    }

    void Gyroscope::RegisterEventWatchIfNeeded()
    {
        if (!eventWatchRegistered_)
        {
            const SDL_EventFilter eventFilter =
                reinterpret_cast<SDL_EventFilter>(&Gyroscope::SensorEventWatch);
            SDL_AddEventWatch(eventFilter, nullptr);
            eventWatchRegistered_ = true;
        }
    }

    void Gyroscope::UnregisterEventWatchIfNeeded()
    {
        if (eventWatchRegistered_ && startedInstances_.empty())
        {
            const SDL_EventFilter eventFilter =
                reinterpret_cast<SDL_EventFilter>(&Gyroscope::SensorEventWatch);
            SDL_RemoveEventWatch(eventFilter, nullptr);
            eventWatchRegistered_ = false;
        }
    }

    bool Gyroscope::SensorEventWatch(void* userdata, void* eventData)
    {
        (void)userdata;

        SDL_Event* event = static_cast<SDL_Event*>(eventData);

        if (event == nullptr)
        {
            return true;
        }

        if (event->type != SDL_EVENT_SENSOR_UPDATE)
        {
            return true;
        }

        const std::int64_t sensorId = static_cast<std::int64_t>(event->sensor.which);
        const std::uint64_t timestampNs = static_cast<std::uint64_t>(SDL_GetTicksNS());

        std::vector<Gyroscope*> instancesSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (Gyroscope* gyroscope : startedInstances_)
            {
                if (gyroscope != nullptr)
                {
                    gyroscope->inFlightCallback_ = true;
                    instancesSnapshot.push_back(gyroscope);
                }
            }
        }

        for (Gyroscope* gyroscope : instancesSnapshot)
        {
            gyroscope->ProcessSensorUpdateEvent(
                sensorId,
                event->sensor.data[0],
                event->sensor.data[1],
                event->sensor.data[2],
                timestampNs
            );

            {
                std::lock_guard<std::mutex> lock(mutex_);
                gyroscope->inFlightCallback_ = false;
            }
            callbackFinished_.notify_all();
        }

        return true;
    }

    bool Gyroscope::getIsSupportedProperty()
    {
        const CNA::Platform currentPlatform = CNA::getCurrentPlatform();

        if (!(currentPlatform == CNA::Platform::Android ||
            currentPlatform == CNA::Platform::iOS ||
            currentPlatform == CNA::Platform::Desktop))
        {
            return false;
        }

        if (!EnsureSensorSubsystemInitialized())
        {
            return false;
        }

        int sensorCount = 0;
        SDL_SensorID* sensors = SDL_GetSensors(&sensorCount);

        if (sensors == nullptr || sensorCount <= 0)
        {
            if (sensors != nullptr)
            {
                SDL_free(sensors);
            }
            return false;
        }

        bool supported = false;

        for (int i = 0; i < sensorCount; ++i)
        {
            SDL_Sensor* sensor = SDL_OpenSensor(sensors[i]);
            if (!sensor)
            {
                continue;
            }

            if (SDL_GetSensorType(sensor) == SDL_SENSOR_GYRO)
            {
                supported = true;
                SDL_CloseSensor(sensor);
                break;
            }

            SDL_CloseSensor(sensor);
        }

        SDL_free(sensors);
        return supported;
    }

    SensorState Gyroscope::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Gyroscope");
        return state_;
    }

    Gyroscope::Gyroscope()
        : state_(SensorState::NotSupported),
          started_(false)
    {
        if (instanceCount_ >= MaxSensorCount)
        {
            throw SensorFailedException(
                "The limit of 10 simultaneous instances of the Gyroscope class per application has been exceeded.");
        }

        ++instanceCount_;
        const bool supported = getIsSupportedProperty();
        state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
        setIsSupportedProperty(supported);
    }

    Gyroscope::~Gyroscope()
    {
        if (!getIsDisposedProperty())
        {
            Dispose(true);
        }
    }

    void Gyroscope::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Gyroscope");

        if (started_)
        {
            throw SensorFailedException(
                "Failed to start gyroscope data acquisition. Data acquisition already started.");
        }

        if (!EnsureSensorSubsystemInitialized())
        {
            state_ = SensorState::NotSupported;
            throw SensorFailedException(
                "Failed to start gyroscope data acquisition. SDL sensor subsystem initialization failed.");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (g_sensor_ == nullptr)
            {
                g_sensor_ = OpenDefaultGyroscope();
            }

            if (g_sensor_ == nullptr)
            {
                state_ = SensorState::NotSupported;
                throw SensorFailedException(
                    "Failed to start gyroscope data acquisition. No default sensor found.");
            }

            started_ = true;
            state_ = SensorState::Ready;

            if (std::find(startedInstances_.begin(), startedInstances_.end(), this) == startedInstances_.end())
            {
                startedInstances_.push_back(this);
            }

            RegisterEventWatchIfNeeded();
        }
    }

    void Gyroscope::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Gyroscope");

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (started_)
            {
                auto it = std::find(startedInstances_.begin(), startedInstances_.end(), this);
                if (it != startedInstances_.end())
                {
                    startedInstances_.erase(it);
                }
            }

            started_ = false;
            state_ = SensorState::Disabled;

            UnregisterEventWatchIfNeeded();
        }
    }

    void Gyroscope::Dispose(bool disposing)
    {
        if (!getIsDisposedProperty() && disposing)
        {
            if (started_)
            {
                Stop();
            }

            std::unique_lock<std::mutex> lock(mutex_);

            // Stop() (above) already removed this instance from
            // startedInstances_, so no *new* callback can start targeting
            // it — but SensorEventWatch() may already be mid-call into this
            // instance's ProcessSensorUpdateEvent() on another thread. Wait
            // for that to finish before letting this object's lifetime end,
            // closing the use-after-free window left open by Task P3-4.
            callbackFinished_.wait(lock, [this] { return !inFlightCallback_; });

            --instanceCount_;
            if (instanceCount_ < 0)
            {
                instanceCount_ = 0;
            }

            if (instanceCount_ == 0)
            {
                startedInstances_.clear();
                UnregisterEventWatchIfNeeded();

                if (g_sensor_ != nullptr)
                {
                    SDL_CloseSensor(static_cast<SDL_Sensor*>(g_sensor_));
                    g_sensor_ = nullptr;
                    g_sensorId_ = 0;
                }

                if (SDL_WasInit(SDL_INIT_SENSOR))
                {
                    SDL_QuitSubSystem(SDL_INIT_SENSOR);
                }
            }
        }

        SensorBase<GyroscopeReading>::Dispose(disposing);
    }

#ifdef __ANDROID__
    /**
     * Converts raw SDL3 gyroscope data (portrait device frame) to the XNA Windows
     * Phone landscape coordinate convention, for both allowed landscape rotations
     * (sensorLandscape = ROTATION_90 or ROTATION_270). Mirrors the equivalent
     * accelerometer remap in Accelerometer.cpp; see that file for the full
     * coordinate-system rationale.
     *
     * @param rawX  SDL gyroscope X, in radians/second.
     * @param rawY  SDL gyroscope Y, in radians/second.
     * @param rawZ  SDL gyroscope Z, in radians/second.
     * @return      Rotation rate vector in XNA landscape coordinate convention.
     */
    static Microsoft::Xna::Framework::Vector3 ConvertAndroidGyroscopeToXnaLandscape(
        float rawX, float rawY, float rawZ)
    {
        const SDL_DisplayOrientation orient =
            SDL_GetCurrentDisplayOrientation(SDL_GetPrimaryDisplay());

        float xnaX, xnaY, xnaZ;

        if (orient == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
        {
            xnaX = -rawX;
            xnaY = rawY;
            xnaZ = rawZ;
        }
        else
        {
            xnaX = rawX;
            xnaY = -rawY;
            xnaZ = rawZ;
        }

        return {xnaX, xnaY, xnaZ};
    }
#endif // __ANDROID__

    void Gyroscope::ProcessSensorUpdateEvent(
        std::int64_t sensorId,
        float x,
        float y,
        float z,
        std::uint64_t timestampNs)
    {
        if (!started_)
        {
            return;
        }

        if (getIsDisposedProperty())
        {
            return;
        }

        std::int64_t currentSensorId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (g_sensor_ == nullptr)
            {
                return;
            }
            currentSensorId = g_sensorId_;
        }

        if (sensorId != currentSensorId)
        {
            return;
        }

        DispatchSensorReading(x, y, z, timestampNs);
    }

    void Gyroscope::DispatchSensorReading(float x, float y, float z, std::uint64_t timestampNs)
    {
        GyroscopeReading gyroscopeReading;

        const bool valid = true;
        setIsDataValidProperty(valid);

        if (getIsDataValidProperty())
        {
#ifdef __ANDROID__
            // On Android, remap raw SDL portrait-frame axes to the XNA landscape
            // convention so that the game layer remains platform-agnostic.
            const Microsoft::Xna::Framework::Vector3 rotationRate =
                ConvertAndroidGyroscopeToXnaLandscape(x, y, z);
#else
            const Microsoft::Xna::Framework::Vector3 rotationRate(x, y, z);
#endif

            gyroscopeReading.setRotationRateProperty(rotationRate);

            const SharpRuntime::longcs ticks = static_cast<SharpRuntime::longcs>(timestampNs / 100);

            System::DateTime dateTime(ticks);
            System::DateTimeOffset timestamp(dateTime, System::TimeSpan::Zero);
            gyroscopeReading.setTimestampProperty(timestamp);
        }

        setCurrentValueProperty(gyroscopeReading);
    }

    void Gyroscope::InjectSyntheticSensorUpdate(float x, float y, float z, std::uint64_t timestampNs)
    {
        if (!started_)
        {
            return;
        }

        if (getIsDisposedProperty())
        {
            return;
        }

        DispatchSensorReading(x, y, z, timestampNs);
    }

    void Gyroscope::SetStartedForTesting(bool started)
    {
        started_ = started;
    }

    GetTypeNameCPP(Gyroscope, "Microsoft.Devices.Sensors.Gyroscope")
} // namespace Microsoft::Devices::Sensors
