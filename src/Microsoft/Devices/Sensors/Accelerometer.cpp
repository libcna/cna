// SPDX-License-Identifier: MS-PL
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

namespace Microsoft::Devices::Sensors
{
    void* Accelerometer::g_sensor_ = nullptr;
    std::int64_t Accelerometer::g_sensorId_ = 0;
    int Accelerometer::instanceCount_ = 0;
    bool Accelerometer::eventWatchRegistered_ = false;
    std::vector<Accelerometer*> Accelerometer::startedInstances_;
    std::mutex Accelerometer::mutex_;
    std::condition_variable Accelerometer::callbackFinished_;

    bool Accelerometer::EnsureSensorSubsystemInitialized()
    {
        if (SDL_WasInit(SDL_INIT_SENSOR))
        {
            return true;
        }

        return SDL_InitSubSystem(SDL_INIT_SENSOR);
    }

    void* Accelerometer::OpenDefaultAccelerometer()
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

            if (SDL_GetSensorType(sensor) == SDL_SENSOR_ACCEL)
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

    void Accelerometer::RegisterEventWatchIfNeeded()
    {
        if (!eventWatchRegistered_)
        {
            const SDL_EventFilter eventFilter =
                reinterpret_cast<SDL_EventFilter>(&Accelerometer::SensorEventWatch);
            SDL_AddEventWatch(eventFilter, nullptr);
            eventWatchRegistered_ = true;
        }
    }

    void Accelerometer::UnregisterEventWatchIfNeeded()
    {
        if (eventWatchRegistered_ && startedInstances_.empty())
        {
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

        std::vector<Accelerometer*> instancesSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (Accelerometer* accelerometer : startedInstances_)
            {
                if (accelerometer != nullptr)
                {
                    accelerometer->inFlightCallback_ = true;
                    instancesSnapshot.push_back(accelerometer);
                }
            }
        }

        for (Accelerometer* accelerometer : instancesSnapshot)
        {
            accelerometer->ProcessSensorUpdateEvent(
                sensorId,
                event->sensor.data[0],
                event->sensor.data[1],
                event->sensor.data[2],
                timestampNs
            );

            {
                std::lock_guard<std::mutex> lock(mutex_);
                accelerometer->inFlightCallback_ = false;
            }
            callbackFinished_.notify_all();
        }

        return true;
    }

    bool Accelerometer::getIsSupportedProperty()
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

            if (SDL_GetSensorType(sensor) == SDL_SENSOR_ACCEL)
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

    SensorState Accelerometer::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");
        return state_;
    }

    Accelerometer::Accelerometer()
        : state_(SensorState::NotSupported),
          started_(false)
    {
        if (instanceCount_ >= MaxSensorCount)
        {
            throw SensorFailedException(
                "The limit of 10 simultaneous instances of the Accelerometer class per application has been exceeded.");
        }

        ++instanceCount_;
        const bool supported = getIsSupportedProperty();
        state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
        setIsSupportedProperty(supported);
    }

    Accelerometer::~Accelerometer()
    {
        if (!getIsDisposedProperty())
        {
            Dispose(true);
        }
    }

    void Accelerometer::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

        if (started_)
        {
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. Data acquisition already started.");
        }

        if (!EnsureSensorSubsystemInitialized())
        {
            state_ = SensorState::NotSupported;
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. SDL sensor subsystem initialization failed.");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (g_sensor_ == nullptr)
            {
                g_sensor_ = OpenDefaultAccelerometer();
            }

            if (g_sensor_ == nullptr)
            {
                state_ = SensorState::NotSupported;
                throw AccelerometerFailedException(
                    "Failed to start accelerometer data acquisition. No default sensor found.");
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

    void Accelerometer::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

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

    void Accelerometer::Dispose(bool disposing)
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

        SensorBase<AccelerometerReading>::Dispose(disposing);
    }

#ifdef __ANDROID__
    /**
     * Converts raw SDL3 accelerometer data (portrait device frame) to the XNA Windows
     * Phone landscape coordinate convention expected by the game, for both allowed
     * landscape rotations (sensorLandscape = ROTATION_90 or ROTATION_270).
     *
     * --- SDL3 / Android raw sensor coordinate system ---
     * SDL3 on Android always delivers accelerometer values in the device's NATURAL
     * (portrait) orientation, regardless of the current display rotation:
     *   +X  right edge of device (portrait)
     *   +Y  top  edge of device (portrait)
     *   +Z  out of screen (toward the user)
     * Values are already normalised to fractions of g before this function is called.
     *
     * --- sensorLandscape display orientation ---
     * AndroidManifest.xml uses android:screenOrientation="sensorLandscape", which
     * allows two rotations:
     *
     *   ROTATION_90  (SDL_ORIENTATION_LANDSCAPE):
     *     Device rotated 90° CCW from portrait — portrait-top points landscape-LEFT.
     *       Portrait +X → landscape DOWN,  Portrait +Y → landscape LEFT
     *     Tilt right in landscape → portrait-Y goes down → rawY more negative.
     *     To match WP7 (right tilt → xnaY > 0): xnaX = rawX, xnaY = -rawY, xnaZ = rawZ.
     *
     *   ROTATION_270 (SDL_ORIENTATION_LANDSCAPE_FLIPPED):
     *     Device rotated 270° CCW from portrait — portrait-top points landscape-RIGHT.
     *       Portrait +X → landscape UP,   Portrait +Y → landscape RIGHT
     *     Tilt right in landscape → portrait-Y goes down → rawY more positive.
     *     To match WP7 (right tilt → xnaY > 0): xnaX = -rawX, xnaY = rawY, xnaZ = rawZ.
     *
     * --- XNA / Windows Phone 7 expected coordinate system ---
     *   Acceleration.Y > 0  →  tilt RIGHT (landscape screen right goes down)
     *   Acceleration.Y < 0  →  tilt LEFT  (landscape screen left  goes down)
     *   Acceleration.X      →  tilt forward/backward (landscape up/down)
     *   Acceleration.Z      →  perpendicular to screen
     *
     * To fix a reversed tilt direction, adjust only the signs here — NOT in game code.
     *
     * @param rawX  SDL accelerometer X normalised to g.
     * @param rawY  SDL accelerometer Y normalised to g.
     * @param rawZ  SDL accelerometer Z normalised to g.
     * @return      Acceleration vector in XNA landscape coordinate convention.
     */
    static Microsoft::Xna::Framework::Vector3 ConvertAndroidAccelerometerToXnaLandscape(
        float rawX, float rawY, float rawZ)
    {
        const SDL_DisplayOrientation orient =
            SDL_GetCurrentDisplayOrientation(SDL_GetPrimaryDisplay());

        float xnaX, xnaY, xnaZ;

        if (orient == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
        {
            // ROTATION_270: portrait-top → landscape-RIGHT.
            // Tilt right → rawY positive; negate X to keep forward/back consistent.
            xnaX = -rawX;
            xnaY = rawY;
            xnaZ = rawZ;
        }
        else
        {
            // ROTATION_90 (default / fallback): portrait-top → landscape-LEFT.
            // Tilt right → rawY negative; negate Y to match WP7 convention.
            xnaX = rawX;
            xnaY = -rawY;
            xnaZ = rawZ;
        }

#ifndef NDEBUG
    const char* orientName =
        (orient == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
            ? "LANDSCAPE_FLIPPED(ROTATION_270)"
            : "LANDSCAPE(ROTATION_90)";
    SDL_Log (
"[SpeedyBlupi][Accelerometer] displayRotation=%s raw=(%.3f,%.3f,%.3f) converted=(%.3f,%.3f,%.3f) orientation=sensorLandscape",
    orientName, rawX, rawY, rawZ, xnaX, xnaY, xnaZ);
#endif

    return {xnaX, xnaY, xnaZ};
    }
#endif // __ANDROID__

    void Accelerometer::ProcessSensorUpdateEvent(
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

    void Accelerometer::DispatchSensorReading(float x, float y, float z, std::uint64_t timestampNs)
    {
        AccelerometerReading accelerometerReading;

        constexpr float StandardGravity = 9.80665f;

        const bool valid = true;
        setIsDataValidProperty(valid);

        if (getIsDataValidProperty())
        {
#ifdef __ANDROID__
            // On Android, remap raw SDL portrait-frame axes to the XNA landscape
            // convention so that the game layer remains platform-agnostic.
            const Microsoft::Xna::Framework::Vector3 acceleration =
                ConvertAndroidAccelerometerToXnaLandscape(
                    x / StandardGravity,
                    y / StandardGravity,
                    z / StandardGravity);
#else
            const Microsoft::Xna::Framework::Vector3 acceleration(
                x / StandardGravity,
                y / StandardGravity,
                z / StandardGravity);
#endif

            accelerometerReading.setAccelerationProperty(acceleration);

            const SharpRuntime::longcs ticks = static_cast<SharpRuntime::longcs>(timestampNs / 100);

            System::DateTime dateTime(ticks);
            System::DateTimeOffset timestamp(dateTime, System::TimeSpan::Zero);
            accelerometerReading.setTimestampProperty(timestamp);
        }

        setCurrentValueProperty(accelerometerReading);

        if (getIsDataValidProperty() && !ReadingChanged.Empty())
        {
            const Microsoft::Xna::Framework::Vector3& acceleration = accelerometerReading.getAccelerationProperty();
            const AccelerometerReadingEventArgs eventArgs(
                acceleration.X,
                acceleration.Y,
                acceleration.Z,
                accelerometerReading.getTimestampProperty());

            ReadingChanged.Raise(static_cast<System::Object*>(this), eventArgs);
        }
    }

    void Accelerometer::InjectSyntheticSensorUpdate(float x, float y, float z, std::uint64_t timestampNs)
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

    void Accelerometer::SetStartedForTesting(bool started)
    {
        started_ = started;
    }

    GetTypeNameCPP(Accelerometer, "Microsoft.Devices.Sensors.Accelerometer")
} // namespace Microsoft::Devices::Sensors
