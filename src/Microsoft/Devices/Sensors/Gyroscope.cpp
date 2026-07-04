// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Gyroscope.hpp"

#include <algorithm>
#include <mutex>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_sensor.h>

#include "CNA/Platform.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp"
#include "Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    Detail::SdlSensorSubsystem<Gyroscope>& Gyroscope::GetSubsystem()
    {
        // Task P5-4: function-local static, not a class-static member —
        // keeps SDL_Sensor*/SDL_SensorType and everything else
        // SdlSensorSubsystem.hpp touches out of Gyroscope.hpp entirely,
        // same discipline this class already used for its previous
        // `void* g_sensor_`.
        static Detail::SdlSensorSubsystem<Gyroscope> subsystem;
        return subsystem;
    }

    int Gyroscope::GetSdlSensorType()
    {
        return static_cast<int>(SDL_SENSOR_GYRO);
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

        return Detail::SdlSensorSubsystem<Gyroscope>::ProbeIsSupported();
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
        auto& subsystem = GetSubsystem();

        // Task P6-1: see Accelerometer::Accelerometer()'s identical fix for
        // the full rationale — the check+increment must be atomic with
        // Dispose()'s decrement (already under this same lock), and the
        // lock must be released before the slow SDL probe below.
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);

            if (subsystem.instanceCount_ >= MaxSensorCount)
            {
                throw SensorFailedException(
                    "The limit of 10 simultaneous instances of the Gyroscope class per application has been exceeded.");
            }

            ++subsystem.instanceCount_;
        }

        try
        {
            const bool supported = getIsSupportedProperty();
            state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
            setIsSupportedProperty(supported);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            --subsystem.instanceCount_;
            throw;
        }
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

        if (!subsystemHeld_)
        {
            if (!Detail::SdlSensorSubsystem<Gyroscope>::EnsureSubsystemInitialized())
            {
                state_ = SensorState::NotSupported;
                throw SensorFailedException(
                    "Failed to start gyroscope data acquisition. SDL sensor subsystem initialization failed.");
            }

            subsystemHeld_ = true;
        }

        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);

        if (subsystem.OpenDefaultSensorLocked() == nullptr)
        {
            state_ = SensorState::NotSupported;
            throw SensorFailedException(
                "Failed to start gyroscope data acquisition. No default sensor found.");
        }

        started_ = true;
        state_ = SensorState::Ready;

        subsystem.RegisterStartedInstanceLocked(this);
        subsystem.RegisterEventWatchIfNeededLocked();
    }

    void Gyroscope::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Gyroscope");

        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);

        if (started_)
        {
            subsystem.UnregisterStartedInstanceLocked(this);
        }

        started_ = false;
        state_ = SensorState::Disabled;

        subsystem.UnregisterEventWatchIfNeededLocked();
    }

    void Gyroscope::Dispose(bool disposing)
    {
        if (!getIsDisposedProperty() && disposing)
        {
            if (started_)
            {
                Stop();
            }

            auto& subsystem = GetSubsystem();
            std::unique_lock<std::mutex> lock(subsystem.mutex_);

            // Stop() (above) already removed this instance from
            // subsystem.startedInstances_, so no *new* callback can start
            // targeting it — but the shared event watch may already be
            // mid-call into this instance's ProcessSensorUpdateEvent() on
            // another thread. Wait for that to finish before letting this
            // object's lifetime end, closing the use-after-free window
            // left open by Task P3-4.
            // Task P5-2/P5-3: waits until no *other* thread's entry
            // remains in dispatchingThreadIds_ — see Accelerometer.cpp's
            // identical wait predicate for the full self-dispose rationale.
            subsystem.callbackFinished_.wait(lock, [this]
            {
                const std::thread::id currentThreadId = std::this_thread::get_id();
                const auto selfCount = std::count(
                    dispatchingThreadIds_.begin(), dispatchingThreadIds_.end(), currentThreadId);
                return dispatchingThreadIds_.size() == static_cast<std::size_t>(selfCount);
            });

            --subsystem.instanceCount_;
            if (subsystem.instanceCount_ < 0)
            {
                subsystem.instanceCount_ = 0;
            }

            if (subsystem.instanceCount_ == 0)
            {
                subsystem.startedInstances_.clear();
                subsystem.UnregisterEventWatchIfNeededLocked();

                if (subsystem.sensor_ != nullptr)
                {
                    SDL_CloseSensor(subsystem.sensor_);
                    subsystem.sensor_ = nullptr;
                    subsystem.sensorId_ = 0;
                }
            }

            // Balances this instance's own EnsureSubsystemInitialized()
            // call from Start() (if any) 1:1, independent of
            // instanceCount_ — SDL's internal ref-count (not this class)
            // decides whether this is the last holder across both
            // Accelerometer and Gyroscope (Task P4-8).
            if (subsystemHeld_)
            {
                SDL_QuitSubSystem(SDL_INIT_SENSOR);
                subsystemHeld_ = false;
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

        // Task P5-7: the actual sign-remap math is a pure function shared
        // with Accelerometer.cpp (identical for both), moved to
        // Detail::ConvertAndroidPortraitToXnaLandscape() so it can be unit
        // tested on any platform. This function's only remaining job is
        // mapping SDL's live display orientation to that function's
        // platform-independent enum.
        const Detail::AndroidSensorLandscapeOrientation mappedOrientation =
            (orient == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
                ? Detail::AndroidSensorLandscapeOrientation::Rotation270
                : Detail::AndroidSensorLandscapeOrientation::Rotation90;

        return Detail::ConvertAndroidPortraitToXnaLandscape(rawX, rawY, rawZ, mappedOrientation);
    }
#endif // __ANDROID__

    void Gyroscope::ProcessSensorUpdateEvent(
        std::int64_t sensorId,
        float x,
        float y,
        float z)
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
            auto& subsystem = GetSubsystem();
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            if (subsystem.sensor_ == nullptr)
            {
                return;
            }
            currentSensorId = subsystem.sensorId_;
        }

        if (sensorId != currentSensorId)
        {
            return;
        }

        DispatchSensorReading(x, y, z);
    }

    void Gyroscope::DispatchSensorReading(float x, float y, float z)
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

            // Wall-clock time of this reading (Task P4-7). Previously derived
            // from SDL_GetTicksNS() (monotonic ns since SDL init) fed into a
            // DateTime(ticks) constructor that expects ticks since the .NET
            // epoch (0001-01-01) — always produced a bogus near-year-1 value,
            // never the actual reading time.
            gyroscopeReading.setTimestampProperty(System::DateTimeOffset::getUtcNowProperty());
        }

        setCurrentValueProperty(gyroscopeReading);
    }

    void Gyroscope::InjectSyntheticSensorUpdate(float x, float y, float z)
    {
        if (!started_)
        {
            return;
        }

        if (getIsDisposedProperty())
        {
            return;
        }

        // Task P5-2/P5-3: participates in the same dispatchingThreadIds_
        // bookkeeping as the real event-watch path — see
        // Accelerometer.cpp's identical hook for the full rationale.
        const std::thread::id thisThreadId = std::this_thread::get_id();
        auto& subsystem = GetSubsystem();

        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            dispatchingThreadIds_.push_back(thisThreadId);
        }

        DispatchSensorReading(x, y, z);

        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            const auto it = std::find(dispatchingThreadIds_.begin(), dispatchingThreadIds_.end(), thisThreadId);
            if (it != dispatchingThreadIds_.end())
            {
                dispatchingThreadIds_.erase(it);
            }
        }
        subsystem.callbackFinished_.notify_all();
    }

    void Gyroscope::SetStartedForTesting(bool started)
    {
        started_ = started;
    }

    void Gyroscope::SetSupportedForTesting(bool supported)
    {
        setIsSupportedProperty(supported);
    }

    GetTypeNameCPP(Gyroscope, "Microsoft.Devices.Sensors.Gyroscope")
} // namespace Microsoft::Devices::Sensors
