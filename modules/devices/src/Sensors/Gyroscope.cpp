// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Gyroscope.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/TargetPlatform.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp"
#include "Microsoft/Devices/Sensors/Detail/PlatformSensorSubsystem.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    Detail::PlatformSensorSubsystem<Gyroscope>& Gyroscope::GetSubsystem()
    {
        // Function-local static keeps the shared registration/session state private to this
        // translation unit while retaining one manager for every Gyroscope instance.
        static Detail::PlatformSensorSubsystem<Gyroscope> subsystem;
        return subsystem;
    }

    CNA::Platform::SensorKind Gyroscope::GetPlatformSensorKind()
    {
        return CNA::Platform::SensorKind::Gyroscope;
    }

    bool Gyroscope::getIsSupportedProperty()
    {
        const CNA::TargetPlatform currentPlatform = CNA::getCurrentPlatform();

        if (!(currentPlatform == CNA::TargetPlatform::Android ||
            currentPlatform == CNA::TargetPlatform::iOS ||
            currentPlatform == CNA::TargetPlatform::Desktop))
        {
            return false;
        }

        // Task P7-1: see Accelerometer::getIsSupportedProperty()'s
        // identical fix for the full rationale — this class's own
        // subsystem.mutex_ (as P6-9 used it) did not serialize against
        // Accelerometer's identical native subsystem calls. ProbeIsSupported() touches no
        // per-class state; process-wide serialization now belongs to the selected platform.
        return Detail::PlatformSensorSubsystem<Gyroscope>::ProbeIsSupported(
            GetPlatformSensorKind());
    }

    SensorState Gyroscope::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Gyroscope");

        // Task P6-3: see Accelerometer::getStateProperty()'s identical fix
        // for the full rationale.
        std::lock_guard<std::mutex> lock(GetSubsystem().mutex_);
        return state_;
    }

    Gyroscope::Gyroscope()
        : state_(SensorState::NotSupported),
          started_(false),
          dispatchToken_(std::make_shared<std::vector<std::thread::id>>())
    {
        auto& subsystem = GetSubsystem();

        // Task P6-1: see Accelerometer::Accelerometer()'s identical fix for
        // the full rationale — the check+increment must be atomic with
        // Dispose()'s decrement (already under this same lock), and the
        // lock must be released before the slow platform probe below.
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

        // Task P6-3: see Accelerometer::Start()'s identical fix for the
        // full rationale — the whole body below is now guarded by a single
        // subsystem.mutex_ acquisition.
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);

        if (started_)
        {
            throw SensorFailedException(
                "Failed to start gyroscope data acquisition. Data acquisition already started.");
        }

        // Task P6-2: see Accelerometer::Start()'s identical fix for the
        // full rationale — only a subsystem hold newly acquired by *this*
        // call is released on failure below.
        bool acquiredSubsystemThisCall = false;
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        if (subsystemHeld_ && acquiredPlatform_ != &platform)
        {
            state_ = SensorState::NotSupported;
            throw SensorFailedException(
                "Failed to start gyroscope data acquisition: selected platform changed "
                "while this sensor still owns its previous subsystem reference.");
        }
        if (!subsystemHeld_)
        {
            try
            {
                platform.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
            }
            catch (const CNA::Platform::PlatformException& ex)
            {
                state_ = SensorState::NotSupported;
                throw SensorFailedException(ex.what());
            }
            subsystemHeld_ = true;
            acquiredPlatform_ = &platform;
            acquiredSubsystemThisCall = true;
        }

        if (!subsystem.EnsureSessionLocked(GetPlatformSensorKind()))
        {
            state_ = SensorState::NotSupported;
            if (acquiredSubsystemThisCall)
            {
                platform.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
                subsystemHeld_ = false;
                acquiredPlatform_ = nullptr;
            }
            const std::string message =
                "Failed to start gyroscope data acquisition: "
                + subsystem.lastEventWatchError_;
            throw SensorFailedException(message.c_str());
        }

        started_ = true;
        state_ = SensorState::Ready;

        // Task GYRO-004: see Accelerometer::Start()'s identical fix for the
        // full rationale — a fresh Start() must always deliver an immediate
        // first sample.
        ResetUpdateThrottle();

        subsystem.RegisterStartedInstanceLocked(this);
    }

    void Gyroscope::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Gyroscope");

        auto& subsystem = GetSubsystem();
        std::unique_ptr<CNA::Platform::IPlatformSensorSession> inactive;
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            if (started_)
            {
                subsystem.UnregisterStartedInstanceLocked(this);
            }
            started_ = false;
            state_ = SensorState::Disabled;
            inactive = subsystem.TakeInactiveSessionLocked();
        }
    }

    void Gyroscope::Dispose(bool disposing)
    {
        if (!disposing)
        {
            SensorBase<GyroscopeReading>::Dispose(disposing);
            return;
        }

        // Task P6-3/P7-2: see Accelerometer::Dispose(bool)'s identical fix
        // for the full rationale. The loser of a concurrent Dispose() race
        // waits for the winner's cleanup to actually finish (Task P7-2)
        // instead of proceeding immediately.
        if (!ClaimDisposalOnce())
        {
            WaitForDisposalToComplete();
            return;
        }

        // Task LIFE-006: see Accelerometer::Dispose(bool)'s identical fix
        // for the full rationale.
        DisposalTerminalStateGuard terminalStateGuard(*this);

        if (disposalTestHook_)
        {
            disposalTestHook_();
        }

        auto& subsystem = GetSubsystem();

        bool wasStarted;
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            wasStarted = started_;
        }

        if (wasStarted)
        {
            Stop();
        }

        {
            std::unique_lock<std::mutex> lock(subsystem.mutex_);

            // Stop() (above) already removed this instance from
            // subsystem.startedInstances_, so no *new* callback can start
            // targeting it — but the shared event watch may already be
            // mid-call into this instance's ProcessSensorUpdateEvent() on
            // another thread. Wait for that to finish before letting this
            // object's lifetime end, closing the use-after-free window
            // left open by Task P3-4.
            // Task P5-2/P5-3: waits until no *other* thread's entry
            // remains in *dispatchToken_ — see Accelerometer.cpp's
            // identical wait predicate for the full self-dispose rationale.
            // Task P8-1: reads dispatchToken_ via `this`, always valid for
            // this method's entire execution.
            subsystem.callbackFinished_.wait(lock, [this]
            {
                const std::thread::id currentThreadId = std::this_thread::get_id();
                const auto& ids = *dispatchToken_;
                const auto selfCount = std::count(ids.begin(), ids.end(), currentThreadId);
                return ids.size() == static_cast<std::size_t>(selfCount);
            });

            --subsystem.instanceCount_;
            // Task BASE2-007: see Accelerometer::Dispose(bool)'s identical
            // fix for the full rationale.
            assert(subsystem.instanceCount_ >= 0
                && "Gyroscope::instanceCount_ underflowed -- Dispose(bool) ran more than once for one instance");

            if (subsystem.instanceCount_ == 0)
            {
                subsystem.startedInstances_.clear();
            }
        }

        if (subsystemHeld_ && acquiredPlatform_ != nullptr)
        {
            acquiredPlatform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Sensor);
            subsystemHeld_ = false;
            acquiredPlatform_ = nullptr;
        }

        // Task P7-2: only the winning caller reaches here, after cleanup
        // above has fully finished and both locks have been released.
        SensorBase<GyroscopeReading>::Dispose(disposing);
    }

#ifdef __ANDROID__
    /**
     * Converts raw gyroscope data (portrait device frame) to the XNA Windows
     * Phone landscape coordinate convention, for both allowed landscape rotations
     * (ROTATION_90 or ROTATION_270 — not an `android:screenOrientation` manifest
     * attribute; see `Detail::AndroidSensorLandscapeOrientation`'s doc comment,
     * corrected Task ACCEL-004, for the actual mechanism). Mirrors the equivalent
     * accelerometer remap in Accelerometer.cpp; see that file for the full
     * coordinate-system rationale.
     *
     * Task ACCEL-008: this whole remap is a deliberate **CNA convenience
     * deviation** from real WP7 behavior, not part of the XNA 4.0 contract —
     * kept enabled by default for existing CNA games/demos; see
     * `Detail::SetAndroidLandscapeRemapEnabled()` for the opt-out.
     *
     * @param rawX Platform gyroscope X, in radians/second.
     * @param rawY Platform gyroscope Y, in radians/second.
     * @param rawZ Platform gyroscope Z, in radians/second.
     * @return      Rotation rate vector in XNA landscape coordinate convention.
     */
    static Microsoft::Xna::Framework::Vector3 ConvertAndroidGyroscopeToXnaLandscape(
        float rawX, float rawY, float rawZ)
    {
        CNA::Platform::IPlatformSensors* sensors =
            CNA::Platform::GetCurrentPlatform().GetSensors();
        const CNA::Platform::SensorDisplayRotation rotation = sensors != nullptr
            ? sensors->GetDisplayRotation()
            : CNA::Platform::SensorDisplayRotation::Unknown;
        const Detail::AndroidSensorLandscapeOrientation mappedOrientation =
            (rotation == CNA::Platform::SensorDisplayRotation::Degrees270)
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
        if (getIsDisposedProperty())
        {
            return;
        }

        {
            auto& subsystem = GetSubsystem();
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            if (!started_)
            {
                return;
            }
        }
        (void)sensorId;

        // See Accelerometer::ProcessSensorUpdateEvent()'s
        // identical fix for the full rationale. std::chrono::steady_clock,
        // not wall-clock time (2026-07-06 stabilization pass) -- see
        // ShouldAcceptUpdateAt()'s own doc comment.
        if (!ShouldAcceptUpdateAt(std::chrono::steady_clock::now()))
        {
            return;
        }

        DispatchSensorReading(x, y, z);
    }

    void Gyroscope::DispatchSensorReading(float x, float y, float z)
    {
        GyroscopeReading gyroscopeReading;

        // IPlatformSensors and WP7 both define gyroscope values as radians per second around the
        // natural-orientation device axes, so no unit conversion is needed here.
        // Task BASE2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
        // see Accelerometer::DispatchSensorReading()'s identical fix for the
        // full rationale -- checked directly as the local `valid` from here
        // on, instead of an early setIsDataValidProperty(valid) call
        // immediately re-read back via getIsDataValidProperty().
        const bool valid = true;

        if (valid)
        {
#ifdef __ANDROID__
            // On Android, remap raw portrait-frame axes to the XNA landscape
            // convention so that the game layer remains platform-agnostic -- unless
            // Task ACCEL-008's opt-out has been used to request real WP7's raw,
            // unremapped, device-fixed axes instead (see
            // Detail::SetAndroidLandscapeRemapEnabled()'s own doc comment).
            const Microsoft::Xna::Framework::Vector3 rotationRate =
                Detail::IsAndroidLandscapeRemapEnabled()
                    ? ConvertAndroidGyroscopeToXnaLandscape(x, y, z)
                    : Microsoft::Xna::Framework::Vector3(x, y, z);
#else
            const Microsoft::Xna::Framework::Vector3 rotationRate(x, y, z);
#endif

            gyroscopeReading.setRotationRateProperty(rotationRate);

            // Wall-clock time of this reading (Task P4-7), rather than the platform session's
            // arbitrary monotonic epoch. This is the project-wide cross-sensor policy —
            // see docs/devices-api-coverage.md's "Timestamp policy" section.
            gyroscopeReading.setTimestampProperty(System::DateTimeOffset::getUtcNowProperty());
        }

        // Task BASE2-002: atomically publishes the new reading and marks
        // IsDataValid true together -- see
        // SetCurrentValueAndMarkDataValid()'s own doc comment.
        SetCurrentValueAndMarkDataValid(gyroscopeReading);
    }

    void Gyroscope::InjectSyntheticSensorUpdate(float x, float y, float z)
    {
        if (getIsDisposedProperty())
        {
            return;
        }

        // Task P5-2/P5-3: participates in the same dispatch-tracking
        // bookkeeping as the real event-watch path — see
        // Accelerometer.cpp's identical hook for the full rationale.
        const std::thread::id thisThreadId = std::this_thread::get_id();
        auto& subsystem = GetSubsystem();

        // Task P8-1: see Accelerometer::InjectSyntheticSensorUpdate()'s
        // identical fix for the full rationale — copies dispatchToken_
        // into a local before DispatchSensorReading() below, so the
        // cleanup guard never touches `this` again.
        std::shared_ptr<std::vector<std::thread::id>> token;
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            if (!started_)
            {
                return;
            }
            token = dispatchToken_;
            token->push_back(thisThreadId);
        }

        // Task P6-4: see Accelerometer::InjectSyntheticSensorUpdate()'s
        // identical fix for the full rationale.
        auto cleanupGuard = Detail::MakeScopeExit([&subsystem, token, thisThreadId]()
        {
            {
                std::lock_guard<std::mutex> lock(subsystem.mutex_);
                auto& ids = *token;
                const auto it = std::find(ids.begin(), ids.end(), thisThreadId);
                if (it != ids.end())
                {
                    ids.erase(it);
                }
            }
            subsystem.callbackFinished_.notify_all();
        });

        DispatchSensorReading(x, y, z);
    }

    void Gyroscope::SetStartedForTesting(bool started)
    {
        std::lock_guard<std::mutex> lock(GetSubsystem().mutex_);
        started_ = started;
    }

    void Gyroscope::SetSupportedForTesting(bool supported)
    {
        setIsSupportedProperty(supported);
    }

    bool Gyroscope::GetSubsystemHeldForTesting() const
    {
        // Task P7-4: see Accelerometer::GetSubsystemHeldForTesting()'s
        // identical fix for the full rationale.
        std::lock_guard<std::mutex> lock(GetSubsystem().mutex_);
        return subsystemHeld_;
    }

    void Gyroscope::SetDisposalCleanupHookForTesting(std::function<void()> hook)
    {
        disposalTestHook_ = std::move(hook);
    }

    void Gyroscope::RegisterStartedInstanceForTesting(Gyroscope& instance)
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        subsystem.RegisterStartedInstanceLocked(&instance);
    }

    void Gyroscope::UnregisterStartedInstanceForTesting(Gyroscope& instance)
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        subsystem.UnregisterStartedInstanceLocked(&instance);
    }

    void Gyroscope::DispatchToInstancesForTesting(
        const std::vector<Gyroscope*>& instances, float x, float y, float z)
    {
        // See Accelerometer::DispatchToInstancesForTesting()'s
        // identical comment -- DispatchToInstances() now takes a snapshot of
        // DispatchRegistration nodes, not raw pointers.
        auto& subsystem = GetSubsystem();

        std::vector<std::shared_ptr<Detail::PlatformSensorSubsystem<Gyroscope>::DispatchRegistration>> registrations;
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            for (Gyroscope* instance : instances)
            {
                for (const auto& registration : subsystem.startedInstances_)
                {
                    if (registration->owner == instance)
                    {
                        registrations.push_back(registration);
                        break;
                    }
                }
            }
        }

        subsystem.DispatchToInstances(registrations, [x, y, z](Gyroscope* instance)
        {
            instance->DispatchSensorReading(x, y, z);
        });
    }

    void Gyroscope::SetEventWatchRegistrationFailureForTesting(bool shouldFail)
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        subsystem.forceEventWatchRegistrationFailureForTesting_ = shouldFail;
    }

    int Gyroscope::GetDispatchExceptionCountForTesting()
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        return subsystem.dispatchExceptionCountForTesting_;
    }

    std::string Gyroscope::GetLastDispatchExceptionMessageForTesting()
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        return subsystem.lastDispatchExceptionMessageForTesting_;
    }

    bool Gyroscope::IsSensorConnectedForTesting(std::int64_t sensorId)
    {
        return Detail::PlatformSensorSubsystem<Gyroscope>::IsSensorConnected(sensorId);
    }

    GetTypeNameCPP(Gyroscope, "Microsoft.Devices.Sensors.Gyroscope")
} // namespace Microsoft::Devices::Sensors
