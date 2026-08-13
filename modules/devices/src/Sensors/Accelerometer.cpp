// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"

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
    Detail::PlatformSensorSubsystem<Accelerometer>& Accelerometer::GetSubsystem()
    {
        static Detail::PlatformSensorSubsystem<Accelerometer> subsystem;
        return subsystem;
    }

    CNA::Platform::SensorKind Accelerometer::GetPlatformSensorKind()
    {
        return CNA::Platform::SensorKind::Accelerometer;
    }

    bool Accelerometer::getIsSupportedProperty()
    {
        // Desktop is deliberately treated like Android/iOS: if the selected platform reports a
        // real accelerometer (for example in a 2-in-1 laptop), CNA uses it. Browser support remains
        // excluded by the established target policy and requires a separate compatibility choice.
        const CNA::TargetPlatform currentPlatform = CNA::getCurrentPlatform();

        if (!(currentPlatform == CNA::TargetPlatform::Android ||
            currentPlatform == CNA::TargetPlatform::iOS ||
            currentPlatform == CNA::TargetPlatform::Desktop))
        {
            return false;
        }

        // Process-wide native subsystem serialization belongs to IPlatform. The per-class manager
        // protects only registrations and callback lifetime, so Accelerometer and Gyroscope cannot
        // accidentally use different locks around the same native subsystem.
        return Detail::PlatformSensorSubsystem<Accelerometer>::ProbeIsSupported(
            GetPlatformSensorKind());
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
        // getIsSupportedProperty() below, which performs a real (slow) platform
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
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);

        if (started_)
        {
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition. Data acquisition already started.");
        }

        bool acquiredSubsystemThisCall = false;
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        if (subsystemHeld_ && acquiredPlatform_ != &platform)
        {
            state_ = SensorState::NotSupported;
            throw AccelerometerFailedException(
                "Failed to start accelerometer data acquisition: selected platform changed "
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
                throw AccelerometerFailedException(ex.what());
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
                "Failed to start accelerometer data acquisition: "
                + subsystem.lastEventWatchError_;
            throw AccelerometerFailedException(message.c_str());
        }

        started_ = true;
        state_ = SensorState::Ready;

        // Task ACCEL-005: a fresh Start() must always deliver an immediate
        // first sample, not stay throttled by a stale last-accepted-update
        // timestamp left over from a previous Start()/Stop() cycle.
        ResetUpdateThrottle();

        subsystem.RegisterStartedInstanceLocked(this);
    }

    void Accelerometer::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Accelerometer");

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
        // Destruction is outside subsystem.mutex_: the platform session destructor is a callback
        // barrier, and an in-flight callback may itself be waiting to take that mutex.
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

        // Task LIFE-006 (2026-07-17, external audit
        // `audit_devices_2026-07-17.md`): see DisposalTerminalStateGuard's
        // own doc comment -- guarantees disposed_ is published and every
        // concurrent losing Dispose() caller's WaitForDisposalToComplete()
        // unblocks, even if the cleanup below throws.
        DisposalTerminalStateGuard terminalStateGuard(*this);

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
            // Task BASE2-007 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
            // instanceCount_ going negative would mean this instance's own
            // Dispose(bool) cleanup ran more than once despite
            // ClaimDisposalOnce() (see above) supposedly guaranteeing
            // exactly one winner -- i.e. a real bug in that guarantee, not a
            // recoverable condition. Silently clamping back to zero (the
            // prior behavior) would mask exactly that bug instead of
            // surfacing it. `ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount`/
            // `EleventhSimultaneousInstanceThrows`/`DisposingOneOfTenAllowsAnotherConstruction`
            // already provide strong behavioral evidence this invariant
            // holds today; this assert is defense-in-depth against a future
            // regression, not a fix for a currently-reproducible defect.
            assert(subsystem.instanceCount_ >= 0
                && "Accelerometer::instanceCount_ underflowed -- Dispose(bool) ran more than once for one instance");

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

        // Task P7-2: only the winning caller (this point is only reached
        // after ClaimDisposalOnce() returned true and cleanup above has
        // fully finished) flips disposed_ to true and wakes any concurrent
        // loser blocked in WaitForDisposalToComplete(). Runs after the
        // subsystem and platform lock scopes above have already ended.
        SensorBase<AccelerometerReading>::Dispose(disposing);
    }

#ifdef __ANDROID__
    /**
     * Converts raw Android accelerometer data (portrait device frame) to the XNA Windows Phone
     * landscape convention, for both allowed landscape rotations.
     *
     * The platform contract reports axes in the device's natural orientation:
     *   +X  right edge of device (portrait)
     *   +Y  top  edge of device (portrait)
     *   +Z  out of screen (toward the user)
     * Values are already normalised to fractions of g before this function is called.
     *
     * --- landscape-only display orientation ---
     * Corrected 2026-07-06 (Task ACCEL-004): this is not an
     * `android:screenOrientation` manifest attribute (the demo's manifest sets
     * none, confirmed by inspection) — see
     * `Detail::AndroidSensorLandscapeOrientation`'s own doc comment. Only two rotations are
     * modeled here:
     *
     *   ROTATION_90:
     *     Device rotated 90° CCW from portrait — portrait-top points landscape-LEFT.
     *       Portrait +X → landscape DOWN,  Portrait +Y → landscape LEFT
     *     Tilt right in landscape → portrait-Y goes down → rawY more negative.
     *     To match WP7 (right tilt → xnaY > 0): xnaX = rawX, xnaY = -rawY, xnaZ = rawZ.
     *
     *   ROTATION_270:
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
     * Task ACCEL-008: this whole remap is a deliberate **CNA convenience
     * deviation** from real WP7 behavior, not part of the XNA 4.0 contract — the
     * real WP7 `Accelerometer` never remaps axes based on display orientation at
     * all (archived MSDN Magazine article, "Touch and Go - Getting Oriented with
     * the Windows Phone Compass"). Kept enabled by default for existing CNA
     * games/demos; see `Detail::SetAndroidLandscapeRemapEnabled()` for the opt-out.
     *
     * @param rawX Platform accelerometer X normalised to g.
     * @param rawY Platform accelerometer Y normalised to g.
     * @param rawZ Platform accelerometer Z normalised to g.
     * @return      Acceleration vector in XNA landscape coordinate convention.
     */
    static Microsoft::Xna::Framework::Vector3 ConvertAndroidAccelerometerToXnaLandscape(
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

        return Detail::ConvertAndroidPortraitToXnaLandscape(
            rawX, rawY, rawZ, mappedOrientation);
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

        {
            auto& subsystem = GetSubsystem();
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            if (!started_)
            {
                return;
            }
        }
        (void)sensorId;

        // Honor TimeBetweenUpdates by
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

        // IPlatformSensors reports acceleration in SI metres per second squared. WP7 exposes the
        // reading in fractions of standard gravity, so conversion belongs at this API boundary.
        constexpr float StandardGravity = 9.80665f;

        // Raw values use natural-orientation device axes. Only the Android compatibility policy
        // below applies a display-relative remap.
        // Task BASE2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
        // previously set via an early setIsDataValidProperty(valid) call,
        // then immediately re-read back via getIsDataValidProperty() below --
        // a redundant round-trip through mutex_-guarded shared state for a
        // value already known locally, and one that let IsDataValid become
        // observably true well before CurrentValue actually held this new
        // reading (SetCurrentValueAndMarkDataValid() below closes that
        // window). Checked directly as the local `valid` from here on.
        const bool valid = true;

        if (valid)
        {
#ifdef __ANDROID__
            // On Android, remap raw portrait-frame axes to the XNA landscape
            // convention so that the game layer remains platform-agnostic -- unless
            // Task ACCEL-008's opt-out has been used to request real WP7's raw,
            // unremapped, device-fixed axes instead (see
            // Detail::SetAndroidLandscapeRemapEnabled()'s own doc comment).
            const Microsoft::Xna::Framework::Vector3 acceleration =
                Detail::IsAndroidLandscapeRemapEnabled()
                    ? ConvertAndroidAccelerometerToXnaLandscape(
                          x / StandardGravity,
                          y / StandardGravity,
                          z / StandardGravity)
                    : Microsoft::Xna::Framework::Vector3(
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

            // Wall-clock time of this reading (Task P4-7), rather than the platform session's
            // arbitrary monotonic epoch. This is the project-wide cross-sensor policy —
            // see docs/devices-api-coverage.md's "Timestamp policy" section.
            accelerometerReading.setTimestampProperty(System::DateTimeOffset::getUtcNowProperty());
        }

        // Task LIFE-004 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
        // decide *before* raising CurrentValueChanged whether ReadingChanged
        // must also fire, and take a local copy of the ReadingChanged
        // event-handler collection itself -- System::EventHandler<T> is a
        // plain copyable value (a std::vector of subscriber callbacks, no
        // pointer back to its owner), so this copy is completely independent
        // of `this` from this point on. Previously, the code below called
        // getIsDataValidProperty() and ReadingChanged.Raise() *after*
        // setCurrentValueProperty() (which raises CurrentValueChanged) had
        // already returned -- if a CurrentValueChanged handler destroyed
        // this Accelerometer, both of those calls would dereference an
        // already-freed `this`. The real WP7 firing order (CurrentValueChanged
        // always first, ReadingChanged second, Task ACCEL-002) is unchanged;
        // only *how* the second event survives the first potentially
        // destroying the sender is what changed.
        System::EventHandler<AccelerometerReadingEventArgs> readingChangedSnapshot;
        AccelerometerReadingEventArgs readingChangedArgs;
        bool shouldRaiseReadingChanged = false;
        if (valid && !ReadingChanged.Empty())
        {
            readingChangedSnapshot = ReadingChanged;
            const Microsoft::Xna::Framework::Vector3& acceleration = accelerometerReading.getAccelerationProperty();
            readingChangedArgs = AccelerometerReadingEventArgs(
                acceleration.X,
                acceleration.Y,
                acceleration.Z,
                accelerometerReading.getTimestampProperty());
            shouldRaiseReadingChanged = true;
        }

        // Task BASE2-002: atomically publishes the new reading and marks
        // IsDataValid true together -- see SetCurrentValueAndMarkDataValid()'s
        // own doc comment for the race this closes (previously two separate
        // calls, one at the very top of this method).
        SetCurrentValueAndMarkDataValid(accelerometerReading);

        if (shouldRaiseReadingChanged)
        {
            // Raised through the local snapshot, not `this->ReadingChanged`
            // -- safe even if `this` no longer exists. `sender` is still
            // `this` itself, matching the real WP7 contract that every
            // event's sender is the raising Accelerometer instance; passing
            // a possibly-by-now-dangling `this` as an opaque sender value is
            // a pointer-arithmetic-only operation (no dereference) and is
            // already the same accepted contract every other event in this
            // codebase relies on when its own handler destroys the sender.
            readingChangedSnapshot.Raise(static_cast<System::Object*>(this), readingChangedArgs);
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
        // real platform callback path, this is a
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
        // DispatchToInstances() takes a snapshot of
        // DispatchRegistration nodes, not raw pointers -- reconstruct that
        // snapshot from the *currently* active registration for each raw
        // test pointer, exactly as the platform callback does from the
        // live startedInstances_ list, before any dispatch (and thus any
        // test callback that might dispose/destroy one of these instances)
        // has had a chance to run.
        auto& subsystem = GetSubsystem();

        std::vector<std::shared_ptr<Detail::PlatformSensorSubsystem<Accelerometer>::DispatchRegistration>> registrations;
        {
            std::lock_guard<std::mutex> lock(subsystem.mutex_);
            for (Accelerometer* instance : instances)
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

        subsystem.DispatchToInstances(registrations, [x, y, z](Accelerometer* instance)
        {
            instance->DispatchSensorReading(x, y, z);
        });
    }

    void Accelerometer::SetEventWatchRegistrationFailureForTesting(bool shouldFail)
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        subsystem.forceEventWatchRegistrationFailureForTesting_ = shouldFail;
    }

    int Accelerometer::GetDispatchExceptionCountForTesting()
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        return subsystem.dispatchExceptionCountForTesting_;
    }

    std::string Accelerometer::GetLastDispatchExceptionMessageForTesting()
    {
        auto& subsystem = GetSubsystem();
        std::lock_guard<std::mutex> lock(subsystem.mutex_);
        return subsystem.lastDispatchExceptionMessageForTesting_;
    }

    bool Accelerometer::IsSensorConnectedForTesting(std::int64_t sensorId)
    {
        return Detail::PlatformSensorSubsystem<Accelerometer>::IsSensorConnected(sensorId);
    }

    GetTypeNameCPP(Accelerometer, "Microsoft.Devices.Sensors.Accelerometer")
} // namespace Microsoft::Devices::Sensors
