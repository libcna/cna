// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Compass.hpp"

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    int Compass::instanceCount_ = 0;
    std::mutex Compass::instanceCountMutex_;

    bool Compass::getIsSupportedProperty()
    {
#if defined(__ANDROID__)
        // Stateless probe, independent of any Compass instance (matching
        // the real WP7 API's static IsSupported contract) — cheap, does
        // not hold any resource open (Detail::AndroidSensorBridge::
        // IsAvailable()'s own discipline).
        Detail::AndroidCompassBackend probe;
        return probe.IsSupported();
#else
        // SDL3 exposes no magnetometer/compass API on any supported platform.
        return false;
#endif
    }

    SensorState Compass::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");
        std::lock_guard<std::mutex> lock(control_->mutex);
        return state_;
    }

    Compass::Compass()
        : state_(SensorState::NotSupported),
          started_(false)
    {
        control_->owner = this;

        // Task P6-1: previously unguarded, a real data race between
        // concurrent constructors/Dispose() calls on this shared static
        // counter.
        {
            std::lock_guard<std::mutex> lock(instanceCountMutex_);

            if (instanceCount_ >= MaxSensorCount)
            {
                throw SensorFailedException(
                    "The limit of 10 simultaneous instances of the Compass class per application has been exceeded.");
            }

            ++instanceCount_;
        }

#if defined(__ANDROID__)
        backend_ = std::make_unique<Detail::AndroidCompassBackend>();
#endif

        const bool supported = backend_ ? backend_->IsSupported() : getIsSupportedProperty();
        state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
        setIsSupportedProperty(supported);

        // Task ANDROID-BRIDGE-002: forwards a TimeBetweenUpdates change to
        // the live backend (if any) without requiring Stop()/Start(). Reads
        // backend_ fresh each time this fires, not a captured copy, so it
        // still reaches a backend swapped in later via
        // SetBackendForTesting(). A safe no-op if not currently started —
        // ICompassBackend::SetSampleInterval()'s own contract.
        //
        // Task DEV-AUD-006 (2026-07-16, external audit `audit_devices.md`):
        // previously read backend_ here without holding the lock, while
        // SetBackendForTesting() replaces the same unique_ptr under it -- a
        // concurrent setTimeBetweenUpdatesProperty() call and the NOXNA
        // test seam could race on the pointer. The new interval is
        // captured first (getTimeBetweenUpdatesProperty() takes its own,
        // separate SensorBase<T>::mutex_, already released by the time this
        // lambda runs -- see that method's own doc comment), then
        // control_->mutex is locked before touching backend_, matching
        // Start()/Stop()/SetBackendForTesting()'s existing discipline
        // exactly. This handler runs on `this` directly (not through the
        // control block) -- it is only ever installed once, from this
        // constructor, and only ever fires while `this` is genuinely alive
        // (SensorBase<T>'s own TimeBetweenUpdatesChanged is a plain member
        // event, not reachable from a destroyed instance).
        TimeBetweenUpdatesChanged += [this](System::Object*, const System::EventArgs&)
        {
            const System::TimeSpan newInterval = getTimeBetweenUpdatesProperty();
            std::lock_guard<std::mutex> lock(control_->mutex);
            if (backend_)
            {
                backend_->SetSampleInterval(newInterval);
            }
        };
    }

    Compass::~Compass()
    {
        if (!getIsDisposedProperty())
        {
            Dispose(true);
        }
    }

    void Compass::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");

        // Task LIFE-001: reserve the transition under the lock, then
        // release it before calling into backend_ -- backend_->Start() can
        // block (spawn/join worker threads) and, per Task LIFE-002, may
        // synchronously invoke a reading/calibration callback before
        // returning. Previously this entire call ran under mutex_; a user
        // handler reentering Stop()/getStateProperty()/SetBackendForTesting()
        // from within a callback fired mid-Start() would then stall behind
        // this method's own still-in-flight backend_->Start() call.
        std::uint64_t myGeneration;
        Detail::ICompassBackend* backendPtr;
        {
            std::lock_guard<std::mutex> lock(control_->mutex);

            if (started_ || transitioning_)
            {
                throw SensorFailedException(
                    "Failed to start compass data acquisition. Data acquisition already started.");
            }

            if (!backend_ || !backend_->IsSupported())
            {
                state_ = SensorState::NotSupported;
                throw SensorFailedException(
                    "Failed to start compass data acquisition. The compass sensor is not supported on this platform.");
            }

            transitioning_ = true;
            myGeneration = ++control_->generation;
            backendPtr = backend_.get();
            ++backendCallsInFlight_;
        }

        bool started = false;
        try
        {
            started = backendPtr->Start(
                getTimeBetweenUpdatesProperty(),
                // Task LIFE-002/LIFE-003/LIFE-005: captures `control_` (a
                // shared_ptr copy) and `myGeneration` by value -- never
                // `this`. See Detail::SensorOwnerControlBlock's own doc
                // comment for the full rationale: a callback that arrives
                // after a concurrent Stop()/newer Start() (stale
                // generation) or after this Compass has begun disposing
                // (owner == nullptr) safely no-ops instead of publishing
                // stale data or touching a possibly-destroyed object.
                [control = control_, myGeneration](const CompassReading& reading)
                {
                    Compass* owner;
                    {
                        std::lock_guard<std::mutex> lock(control->mutex);
                        if (control->generation != myGeneration || control->owner == nullptr)
                        {
                            return;
                        }
                        owner = control->owner;
                    }
                    // Never call user code (CurrentValueChanged.Raise(), inside
                    // setCurrentValueProperty()) while holding control->mutex --
                    // a handler may legitimately re-enter Compass's own public
                    // API (Task LIFE-001).
                    owner->setIsDataValidProperty(true);
                    owner->setCurrentValueProperty(reading);
                },
                [control = control_, myGeneration]()
                {
                    Compass* owner;
                    {
                        std::lock_guard<std::mutex> lock(control->mutex);
                        if (control->generation != myGeneration || control->owner == nullptr)
                        {
                            return;
                        }
                        owner = control->owner;
                    }
                    if (!owner->Calibrate.Empty())
                    {
                        CalibrationEventArgs args;
                        owner->Calibrate.Raise(static_cast<System::Object*>(owner), args);
                    }
                });
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(control_->mutex);
            --backendCallsInFlight_;
            if (backendCallsInFlight_ == 0)
            {
                backendQuiescent_.notify_all();
            }
            if (control_->generation == myGeneration)
            {
                transitioning_ = false;
                state_ = SensorState::NotSupported;
                transitionFinished_.notify_all();
            }
            throw;
        }

        bool orphanedStart = false;
        {
            std::lock_guard<std::mutex> lock(control_->mutex);
            --backendCallsInFlight_;
            if (backendCallsInFlight_ == 0)
            {
                backendQuiescent_.notify_all();
            }

            if (control_->generation != myGeneration)
            {
                // Task LIFE-001: a concurrent Stop() claimed and completed
                // (or is still completing) a newer generation while our own
                // backend_->Start() call was in flight -- do not touch
                // transitioning_/stopClaimed_ here, the claiming Stop()
                // owns them and will clear them itself. If our own attempt
                // actually succeeded, it must still be torn down below, or
                // it leaks (the Stop() that superseded us already stopped
                // whatever existed *before* this call, not what we just
                // started concurrently afterward).
                orphanedStart = started;
            }
            else if (started)
            {
                transitioning_ = false;
                started_ = true;
                state_ = SensorState::Ready;
                transitionFinished_.notify_all();
            }
            else
            {
                transitioning_ = false;
                state_ = SensorState::NotSupported;
                transitionFinished_.notify_all();
            }
        }

        if (orphanedStart)
        {
            {
                std::lock_guard<std::mutex> lock(control_->mutex);
                ++backendCallsInFlight_;
            }
            backendPtr->Stop();
            {
                std::lock_guard<std::mutex> lock(control_->mutex);
                --backendCallsInFlight_;
                if (backendCallsInFlight_ == 0)
                {
                    backendQuiescent_.notify_all();
                }
            }
        }

        if (!started)
        {
            throw SensorFailedException(
                "Failed to start compass data acquisition. The compass sensor is not supported on this platform.");
        }
    }

    void Compass::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");

        Detail::ICompassBackend* backendPtr = nullptr;
        {
            std::unique_lock<std::mutex> lock(control_->mutex);

            if (!started_ && !transitioning_)
            {
                // Nothing live to stop, but Stop()'s own established
                // contract (matching the pre-Task-LIFE-001 implementation)
                // still unconditionally transitions state_ to Disabled,
                // even when Start() was never called or never succeeded.
                state_ = SensorState::Disabled;
                return;
            }

            if (!stopClaimed_)
            {
                stopClaimed_ = true;
                transitioning_ = true;
                ++control_->generation;
                ++backendCallsInFlight_;
                backendPtr = backend_.get();
            }
            else
            {
                // Task LIFE-001 (mirrors Detail::AndroidSensorBridge's own
                // reclaimClaimed_/runExitedCv_ pattern, Task ANDROID-BRIDGE-005):
                // someone else already claimed stopping the current
                // generation -- wait for them to finish rather than also
                // calling backend_->Stop() concurrently.
                transitionFinished_.wait(lock, [this] { return !transitioning_; });
                return;
            }
        }

        backendPtr->Stop();

        {
            std::lock_guard<std::mutex> lock(control_->mutex);
            --backendCallsInFlight_;
            if (backendCallsInFlight_ == 0)
            {
                backendQuiescent_.notify_all();
            }
            transitioning_ = false;
            stopClaimed_ = false;
            started_ = false;
            state_ = SensorState::Disabled;
        }
        transitionFinished_.notify_all();
    }

    void Compass::Dispose(bool disposing)
    {
        if (!disposing)
        {
            SensorBase<CompassReading>::Dispose(disposing);
            return;
        }

        // Task P6-3/P7-2: ClaimDisposalOnce() closes a race where two
        // threads calling Dispose() on the same instance concurrently could
        // both pass the getIsDisposedProperty() check and both decrement
        // instanceCount_ for what should be a single logical disposal — see
        // SensorBase::Dispose()'s doc comment for the full rationale. The
        // loser waits for the winner's cleanup to actually finish (Task
        // P7-2) instead of proceeding immediately. Was latent when this was
        // first written (Start() always threw before setting started_ true,
        // so the Stop() call below was never actually reached) — fixed
        // uniformly with Accelerometer/Gyroscope anyway, anticipating a real
        // backend (plan_devices_phase7.md's Audit finding B). That
        // anticipated backend now exists (Task DEVICES-0095): on Android,
        // Start() can genuinely succeed and set started_ true, so this path
        // is real, not just defensive, on that platform.
        if (!ClaimDisposalOnce())
        {
            WaitForDisposalToComplete();
            return;
        }

        // Task LIFE-002/LIFE-003/LIFE-005: null out control_->owner *before*
        // Stop() below -- see Detail::SensorOwnerControlBlock's own doc
        // comment. Any reading/calibration callback that has not yet passed
        // its own generation/owner check (including one arriving from
        // whatever Stop() is about to tear down) will now safely no-op
        // instead of touching this object, which is unconditionally
        // committed to being torn down from this point on.
        {
            std::lock_guard<std::mutex> lock(control_->mutex);
            control_->owner = nullptr;
        }

        Stop();

        {
            std::lock_guard<std::mutex> lock(instanceCountMutex_);
            --instanceCount_;
            if (instanceCount_ < 0)
            {
                instanceCount_ = 0;
            }
        }

        SensorBase<CompassReading>::Dispose(disposing);
    }

    void Compass::SetBackendForTesting(std::unique_ptr<Detail::ICompassBackend> backend)
    {
        // Enforced, not just documented (Task: SetBackendForTesting()
        // contract): swapping backend_ out from under a running Start()/
        // Stop() session would leave the old backend's own worker state
        // (e.g. AndroidSensorBridge's background threads) running
        // unmanaged, orphaned from anything that could still Stop() it.
        std::unique_lock<std::mutex> lock(control_->mutex);

        if (started_ || transitioning_)
        {
            throw SensorFailedException(
                "Cannot replace the compass backend while data acquisition is started. Call Stop() first.");
        }

        // Task LIFE-003: even once transitioning_ has already been reset to
        // false, a just-orphaned Start() attempt's own cleanup call into
        // the backend (see Start()'s own comment) can still be executing --
        // wait for every in-flight call to finish so this never swaps or
        // destroys a backend object while something is still calling into
        // it.
        backendQuiescent_.wait(lock, [this] { return backendCallsInFlight_ == 0; });

        backend_ = std::move(backend);
        setIsSupportedProperty(backend_ ? backend_->IsSupported() : getIsSupportedProperty());
    }

    GetTypeNameCPP(Compass, "Microsoft.Devices.Sensors.Compass")
} // namespace Microsoft::Devices::Sensors
