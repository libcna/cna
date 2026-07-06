// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

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
    Detail::SdlSensorSubsystem<Accelerometer>& Accelerometer::GetSubsystem()
    {
        // Task P5-4: function-local static, not a class-static member —
        // keeps SDL_Sensor*/SDL_SensorType and everything else
        // SdlSensorSubsystem.hpp touches out of Accelerometer.hpp
        // entirely, same discipline this class already used for its
        // previous `void* g_sensor_`.
        static Detail::SdlSensorSubsystem<Accelerometer> subsystem;
        return subsystem;
    }

    int Accelerometer::GetSdlSensorType()
    {
        return static_cast<int>(SDL_SENSOR_ACCEL);
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

        // Task P6-9: ProbeIsSupported() calls real SDL sensor-subsystem
        // functions (SDL_InitSubSystem/SDL_GetSensors/SDL_OpenSensor/
        // SDL_CloseSensor/SDL_QuitSubSystem) with no synchronization of
        // its own. SDL3's own documentation states SDL_InitSubSystem()
        // "should only be called on the main thread" and
        // SDL_QuitSubSystem() "is not thread safe" — calling this from
        // multiple threads at once (e.g. concurrently constructing several
        // Accelerometer instances, which each call this from their own
        // constructor) reliably corrupted the heap under real stress
        // testing (a P6-1 regression test constructing instances from 8
        // threads at once reproduced glibc's "unaligned tcache chunk
        // detected"/"malloc(): unaligned tcache chunk detected" abort in
        // roughly 1 in 4 runs).
        //
        // Task P7-1: subsystem.mutex_ alone (as P6-9 used it) only
        // serializes this against *this class's own* other SDL calls —
        // Gyroscope's identical getIsSupportedProperty() locks a
        // *different* mutex (Gyroscope::GetSubsystem().mutex_), so the two
        // classes' real SDL sensor-subsystem calls could still run fully
        // concurrently with each other, against the exact same SDL global
        // SDL_INIT_SENSOR subsystem the P6-1 addendum bug already proved
        // unsafe. ProbeIsSupported() touches no per-class subsystem state
        // (sensor_/sensorId_/instanceCount_/...) at all — it only needs to
        // be serialized against every other real SDL sensor call, from
        // *any* sensor class — so this now locks only the shared global SDL
        // sensor mutex, not subsystem.mutex_ (dropped entirely; see
        // GetGlobalSdlSensorMutex()'s doc comment for the full lock-order
        // rule this participates in).
        std::lock_guard<std::mutex> lock(Detail::GetGlobalSdlSensorMutex());
        return Detail::SdlSensorSubsystem<Accelerometer>::ProbeIsSupported(lock);
    }

    SensorState Accelerometer::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

        // Task P6-3: state_ is only ever written under subsystem.mutex_
        // (constructor aside, before the object is published to any other
        // thread), but was previously read here without any lock — a real
        // data race against Start()/Stop()/Dispose() running concurrently
        // on another thread.
        std::lock_guard<std::mutex> lock(GetSubsystem().mutex_);
        return state_;
    }

    Accelerometer::Accelerometer()
        : state_(SensorState::NotSupported),
          started_(false),
          dispatchToken_(std::make_shared<std::vector<std::thread::id>>())
    {
        auto& subsystem = GetSubsystem();

        // Task P6-1: the instanceCount_ check+increment must be atomic with
        // respect to every other constructor/Dispose() running concurrently
        // (Dispose() already decrements under this same lock) — previously
        // unlocked here, a real data race under the C++ memory model, not
        // just a benign lost-update risk. The lock is released before
        // getIsSupportedProperty() below, which performs real (slow) SDL
        // probing and must never run while holding subsystem.mutex_.
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);

            if (subsystem.instanceCount_ >= MaxSensorCount)
            {
                throw SensorFailedException(
                    "The limit of 10 simultaneous instances of the Accelerometer class per application has been exceeded.");
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

        // Task P6-3: the whole body below is now guarded by a single
        // subsystem.mutex_ acquisition — previously the started_ check and
        // the initial subsystemHeld_ read/write ran before any lock was
        // taken at all, racing against Stop()/Dispose()/
        // ProcessSensorUpdateEvent() on another thread. EnsureSubsystemInitialized()
        // (a plain SDL_InitSubSystem() call, not a device-enumerating probe)
        // is safe to call while holding this lock — it doesn't call back
        // into this class's own code.
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);

        if (started_)
        {
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. Data acquisition already started.");
        }

        // Task P6-2: tracks whether *this* Start() call is the one that
        // just transitioned subsystemHeld_ false -> true, as opposed to it
        // already having been true from an earlier successful Start()
        // (e.g. after a Stop()/Start() cycle). Only a hold newly acquired
        // by this call should be released on failure below — an
        // already-true subsystemHeld_ from before still owes its eventual
        // release to Dispose(), same as always.
        bool acquiredSubsystemThisCall = false;

        {
            // Task P7-1: EnsureSubsystemInitialized()/OpenDefaultSensorLocked()
            // below make real SDL_InitSubSystem/SDL_GetSensors/SDL_OpenSensor/
            // SDL_GetSensorType/SDL_CloseSensor calls — serialize them against
            // every other SDL sensor call across both Accelerometer and
            // Gyroscope via the shared global mutex, nested inside
            // subsystem.mutex_ (already held for this whole method) — always
            // acquired in that order, never the reverse; see
            // GetGlobalSdlSensorMutex()'s doc comment for the full rule.
            std::lock_guard<std::mutex> sdlLock(Detail::GetGlobalSdlSensorMutex());

            if (!subsystemHeld_)
            {
                if (!Detail::SdlSensorSubsystem<Accelerometer>::EnsureSubsystemInitialized(sdlLock))
                {
                    state_ = SensorState::NotSupported;
                    throw AccelerometerFailedException(
                        "Failed to start accelerometer data acquisition. SDL sensor subsystem initialization failed.");
                }

                subsystemHeld_ = true;
                acquiredSubsystemThisCall = true;
            }

            if (subsystem.OpenDefaultSensorLocked(sdlLock) == nullptr)
            {
                state_ = SensorState::NotSupported;

                // Task P6-2: previously left subsystemHeld_ true here forever
                // (until this instance's eventual Dispose()) even though
                // Start() itself failed — a real subsystem-hold leak for any
                // caller that constructs, fails Start(), and never disposes
                // promptly (e.g. a retry loop). Release only the hold this
                // call itself just acquired.
                if (acquiredSubsystemThisCall)
                {
                    SDL_QuitSubSystem(SDL_INIT_SENSOR);
                    subsystemHeld_ = false;
                }

                throw AccelerometerFailedException(
                    "Failed to start accelerometer data acquisition. No default sensor found.");
            }
        }

        started_ = true;
        state_ = SensorState::Ready;

        // Task ACCEL-005: a fresh Start() must always deliver an immediate
        // first sample, not stay throttled by a stale last-accepted-update
        // timestamp left over from a previous Start()/Stop() cycle.
        ResetUpdateThrottle();

        subsystem.RegisterStartedInstanceLocked(this);
        subsystem.RegisterEventWatchIfNeededLocked();
    }

    void Accelerometer::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

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

    void Accelerometer::Dispose(bool disposing)
    {
        if (!disposing)
        {
            SensorBase<AccelerometerReading>::Dispose(disposing);
            return;
        }

        // Task P6-3/P7-2: ClaimDisposalOnce() closes a race where two
        // threads calling Dispose() on the same instance concurrently could
        // both pass the getIsDisposedProperty() check and both run cleanup
        // below — e.g. both decrementing instanceCount_ for what should be
        // a single logical disposal. Only the call that wins
        // ClaimDisposalOnce() runs cleanup; the loser waits for that
        // cleanup to actually finish (Task P7-2) instead of proceeding
        // immediately — see WaitForDisposalToComplete()'s doc comment for
        // why racing ahead here was a real bug (a losing call flipping
        // disposed_ true via the base Dispose(bool) below, while the
        // winner's own Stop() call — invoked from the cleanup body below —
        // was still relying on disposed_ being false).
        if (!ClaimDisposalOnce())
        {
            WaitForDisposalToComplete();
            return;
        }

        if (disposalTestHook_)
        {
            disposalTestHook_();
        }

        auto& subsystem = GetSubsystem();

        // Task P6-3: started_ is only ever written under
        // subsystem.mutex_, but was previously read here directly with
        // no lock at all. Stop() re-locks subsystem.mutex_ internally
        // (not a recursive mutex), so the read and the Stop() call
        // itself cannot share one lock scope.
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
            // remains in *dispatchToken_ — a handler reentrantly disposing
            // its own sender must not wait on itself; only genuinely other
            // threads' in-flight dispatches are waited for. Task P8-1: reads
            // dispatchToken_ (a shared_ptr) via `this`, which is always
            // valid for the entire duration of this Dispose(bool) call
            // (unlike the dispatch-loop's own cleanup guard, this method
            // never runs after the object has been destroyed).
            subsystem.callbackFinished_.wait(lock, [this]
            {
                const std::thread::id currentThreadId = std::this_thread::get_id();
                const auto& ids = *dispatchToken_;
                const auto selfCount = std::count(ids.begin(), ids.end(), currentThreadId);
                return ids.size() == static_cast<std::size_t>(selfCount);
            });

            --subsystem.instanceCount_;
            if (subsystem.instanceCount_ < 0)
            {
                subsystem.instanceCount_ = 0;
            }

            // Task P7-1: SDL_CloseSensor()/SDL_QuitSubSystem() below are
            // real SDL sensor-subsystem calls — serialize them against
            // every other SDL sensor call across both Accelerometer and
            // Gyroscope via the shared global mutex, nested inside the
            // subsystem.mutex_ already held above (same lock order as
            // Start(); see GetGlobalSdlSensorMutex()'s doc comment).
            std::lock_guard<std::mutex> sdlLock(Detail::GetGlobalSdlSensorMutex());

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

        // Task P7-2: only the winning caller (this point is only reached
        // after ClaimDisposalOnce() returned true and cleanup above has
        // fully finished) flips disposed_ to true and wakes any concurrent
        // loser blocked in WaitForDisposalToComplete(). Runs after the
        // subsystem.mutex_/global SDL mutex scope above has already
        // released both locks.
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

        // Task P5-7: the actual sign-remap math is a pure function shared
        // with Gyroscope.cpp (identical for both), moved to
        // Detail::ConvertAndroidPortraitToXnaLandscape() so it can be unit
        // tested on any platform. This function's only remaining job is
        // mapping SDL's live display orientation to that function's
        // platform-independent enum, plus the debug log below.
        const Detail::AndroidSensorLandscapeOrientation mappedOrientation =
            (orient == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
                ? Detail::AndroidSensorLandscapeOrientation::Rotation270
                : Detail::AndroidSensorLandscapeOrientation::Rotation90;

        const Microsoft::Xna::Framework::Vector3 converted =
            Detail::ConvertAndroidPortraitToXnaLandscape(rawX, rawY, rawZ, mappedOrientation);

#ifndef NDEBUG
    const char* orientName =
        (orient == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
            ? "LANDSCAPE_FLIPPED(ROTATION_270)"
            : "LANDSCAPE(ROTATION_90)";
    SDL_Log (
"[SpeedyBlupi][Accelerometer] displayRotation=%s raw=(%.3f,%.3f,%.3f) converted=(%.3f,%.3f,%.3f) orientation=sensorLandscape",
    orientName, rawX, rawY, rawZ, converted.X, converted.Y, converted.Z);
#endif

    return converted;
    }
#endif // __ANDROID__

    void Accelerometer::ProcessSensorUpdateEvent(
        std::int64_t sensorId,
        float x,
        float y,
        float z)
    {
        if (getIsDisposedProperty())
        {
            return;
        }

        // Task P6-3: started_'s read folded into the same lock scope that
        // already reads subsystem.sensor_/sensorId_ — previously read
        // separately with no lock at all, racing against
        // Start()/Stop()/Dispose() on another thread.
        std::int64_t currentSensorId;
        {
            auto& subsystem = GetSubsystem();
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            if (!started_ || subsystem.sensor_ == nullptr)
            {
                return;
            }
            currentSensorId = subsystem.sensorId_;
        }

        if (sensorId != currentSensorId)
        {
            return;
        }

        // Task ACCEL-005/SDL-SENSOR-002: honor TimeBetweenUpdates by
        // dropping events that arrive too soon after the last accepted one.
        // Scoped to this instance alone (ShouldAcceptUpdateAt() reads/writes
        // only this object's own fields), so two Accelerometer instances
        // with different TimeBetweenUpdates values throttle independently.
        // std::chrono::steady_clock, not wall-clock time (2026-07-06
        // stabilization pass) -- see ShouldAcceptUpdateAt()'s own doc
        // comment for why a throttle decision must use a clock immune to
        // NTP steps/clock changes.
        if (!ShouldAcceptUpdateAt(std::chrono::steady_clock::now()))
        {
            return;
        }

        DispatchSensorReading(x, y, z);
    }

    void Accelerometer::DispatchSensorReading(float x, float y, float z)
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

            // Wall-clock time of this reading (Task P4-7). Previously derived
            // from SDL_GetTicksNS() (monotonic ns since SDL init) fed into a
            // DateTime(ticks) constructor that expects ticks since the .NET
            // epoch (0001-01-01) — always produced a bogus near-year-1 value,
            // never the actual reading time.
            accelerometerReading.setTimestampProperty(System::DateTimeOffset::getUtcNowProperty());
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

    void Accelerometer::InjectSyntheticSensorUpdate(float x, float y, float z)
    {
        if (getIsDisposedProperty())
        {
            return;
        }

        // Task P5-2/P5-3: participates in the same dispatch-tracking
        // bookkeeping as the real event-watch path, so a handler that
        // calls Dispose() on this same instance from within a
        // synthetic-update-triggered callback is recognized identically to
        // the real event path (see Dispose(bool)'s self-dispose check).
        const std::thread::id thisThreadId = std::this_thread::get_id();
        auto& subsystem = GetSubsystem();

        // Task P8-1: copies dispatchToken_ (a shared_ptr) into a local
        // *before* DispatchSensorReading() below, while `this` is still
        // definitely valid. The cleanup guard captures this local `token`
        // copy, not `this` — so if the callback this triggers destroys this
        // same instance, the guard (which runs after the callback returns)
        // still safely erases the entry via the token, never touching the
        // now-possibly-freed `this` again. See Accelerometer.hpp's
        // dispatchToken_ doc comment and plan_devices_phase8.md Task P8-1
        // for the full analysis, including the boundary this does not
        // cover (DispatchSensorReading() itself still touches `this` again
        // before conditionally raising ReadingChanged).
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

        // Task P6-4: cleanup now runs via a ScopeExit guard, so it still
        // happens if DispatchSensorReading() (or transitively a user's
        // CurrentValueChanged/ReadingChanged handler) throws — previously
        // a plain post-call statement, skipped entirely on an exception
        // and permanently corrupting the dispatch token. Unlike the
        // real SDL event-watch path (SensorEventWatch()), this is a
        // regular C++ call site, not a C-library callback boundary, so the
        // exception is allowed to propagate to this method's own caller
        // after cleanup runs.
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

    void Accelerometer::SetStartedForTesting(bool started)
    {
        std::lock_guard<std::mutex> lock(GetSubsystem().mutex_);
        started_ = started;
    }

    void Accelerometer::SetSupportedForTesting(bool supported)
    {
        setIsSupportedProperty(supported);
    }

    bool Accelerometer::GetSubsystemHeldForTesting() const
    {
        // Task P7-4: subsystemHeld_ is written only under subsystem.mutex_
        // (Start()/Dispose(bool)), but was previously read here with no
        // lock at all — a real, if narrow, data race under the C++ memory
        // model.
        std::lock_guard<std::mutex> lock(GetSubsystem().mutex_);
        return subsystemHeld_;
    }

    void Accelerometer::SetDisposalCleanupHookForTesting(std::function<void()> hook)
    {
        disposalTestHook_ = std::move(hook);
    }

    void Accelerometer::RegisterStartedInstanceForTesting(Accelerometer& instance)
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        subsystem.RegisterStartedInstanceLocked(&instance);
    }

    void Accelerometer::UnregisterStartedInstanceForTesting(Accelerometer& instance)
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        subsystem.UnregisterStartedInstanceLocked(&instance);
    }

    void Accelerometer::DispatchToInstancesForTesting(
        const std::vector<Accelerometer*>& instances, float x, float y, float z)
    {
        GetSubsystem().DispatchToInstances(instances, [x, y, z](Accelerometer* instance)
        {
            instance->DispatchSensorReading(x, y, z);
        });
    }

    GetTypeNameCPP(Accelerometer, "Microsoft.Devices.Sensors.Accelerometer")
} // namespace Microsoft::Devices::Sensors
