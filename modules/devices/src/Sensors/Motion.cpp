// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Motion.hpp"

#include <cassert>

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    int Motion::instanceCount_ = 0;
    std::mutex Motion::instanceCountMutex_;

    bool Motion::getIsSupportedProperty()
    {
#if defined(__ANDROID__)
        // Stateless probe, independent of any Motion instance (matching the
        // real WP7 API's static IsSupported contract) — cheap, does not
        // hold any resource open.
        Detail::AndroidMotionBackend probe;
        return probe.IsSupported();
#else
        // SDL3 exposes no fused-orientation/motion API on any supported platform.
        return false;
#endif
    }

    SensorState Motion::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");
        std::lock_guard<std::mutex> lock(control_->mutex);
        return state_;
    }

    bool Motion::getIsAttitudeNorthReferencedProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");
        std::lock_guard<std::mutex> lock(control_->mutex);
        // No backend at all (no native platform support) or never started:
        // nothing has ever claimed a north-referenced heading, so there is
        // nothing to warn about -- `true` here is a vacuous default, not an
        // affirmative claim about a real attitude source.
        return !backend_ || backend_->IsUsingNorthReferencedAttitudeSource();
    }

    Motion::Motion()
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
                    "The limit of 10 simultaneous instances of the Motion class per application has been exceeded.");
            }

            ++instanceCount_;
        }

        // Task LIFE-008: see Compass::Compass()'s identical fix for the full
        // rationale.
        try
        {
#if defined(__ANDROID__)
            backend_ = std::make_unique<Detail::AndroidMotionBackend>();
#endif

            const bool supported = backend_ ? backend_->IsSupported() : getIsSupportedProperty();
            state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
            setIsSupportedProperty(supported);

            // Task ANDROID-BRIDGE-002/DEV-AUD-006: see Compass::Compass()'s
            // identical fix for the full rationale (captures the new interval
            // before locking the lock, closing a race between this handler and
            // SetBackendForTesting() on the shared backend_ pointer). Runs on
            // `this` directly, not through the control block -- see
            // Compass::Compass()'s identical comment for why that's safe here.
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
        catch (...)
        {
            std::lock_guard<std::mutex> lock(instanceCountMutex_);
            --instanceCount_;
            throw;
        }
    }

    Motion::~Motion()
    {
        if (!getIsDisposedProperty())
        {
            Dispose(true);
        }
    }

    void Motion::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");

        // Task LIFE-001/LIFE-002/LIFE-003/LIFE-005: see Compass::Start()'s
        // identical fix for the full rationale (reserve under the lock,
        // release before calling into backend_, generation-gated callbacks
        // capturing control_ instead of `this`, orphaned-start cleanup,
        // in-flight counting for SetBackendForTesting()'s benefit).
        std::uint64_t myGeneration;
        Detail::IMotionBackend* backendPtr;
        {
            std::unique_lock<std::mutex> lock(control_->mutex);

            if (started_ || transitioning_)
            {
                throw SensorFailedException("Motion is already started.");
            }

            // Task TEST2-001 (2026-07-17): see Compass::Start()'s identical
            // comment for the full rationale (found by a real ThreadSanitizer
            // run under concurrent multi-thread Start()/Stop() stress) --
            // waits for any earlier, now-orphaned Start() attempt's own
            // cleanup call into backend_ to finish before this attempt makes
            // its own, closing a real overlapping-backend-call data race
            // started_/transitioning_ alone do not prevent.
            backendQuiescent_.wait(lock, [this] { return backendCallsInFlight_ == 0; });

            if (started_ || transitioning_)
            {
                throw SensorFailedException("Motion is already started.");
            }

            if (!backend_ || !backend_->IsSupported())
            {
                state_ = SensorState::NotSupported;
                throw SensorFailedException("Motion is not supported on this platform.");
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
                [control = control_, myGeneration](const MotionReading& reading)
                {
                    Motion* owner;
                    {
                        std::lock_guard<std::mutex> lock(control->mutex);
                        if (control->generation != myGeneration || control->owner == nullptr)
                        {
                            return;
                        }
                        owner = control->owner;
                    }
                    // Task BASE2-002 (2026-07-17, external audit
                    // `audit_devices_2026-07-17.md`): previously two separate
                    // calls (setIsDataValidProperty(true) then
                    // setCurrentValueProperty(reading)) -- a concurrent
                    // reader could observe IsDataValid already true while
                    // CurrentValue still held the previous (or, for the very
                    // first reading, still default-constructed) value. See
                    // SetCurrentValueAndMarkDataValid()'s own doc comment.
                    owner->SetCurrentValueAndMarkDataValid(reading);
                },
                [control = control_, myGeneration]()
                {
                    Motion* owner;
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
            throw SensorFailedException("Motion is not supported on this platform.");
        }
    }

    void Motion::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");

        // Task LIFE-001: see Compass::Stop()'s identical fix for the full
        // rationale. Task TEST2-001: also see Compass::Stop()'s comment on
        // why this deliberately does not wait for backendCallsInFlight_ to
        // reach zero, unlike Start()'s own reserve phase.
        Detail::IMotionBackend* backendPtr = nullptr;
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

    void Motion::Dispose(bool disposing)
    {
        if (!disposing)
        {
            SensorBase<MotionReading>::Dispose(disposing);
            return;
        }

        // Task P6-3/P7-2: see Compass::Dispose(bool)'s identical fix for
        // the full rationale.
        if (!ClaimDisposalOnce())
        {
            WaitForDisposalToComplete();
            return;
        }

        // Task LIFE-006: see Accelerometer::Dispose(bool)'s identical fix
        // for the full rationale.
        DisposalTerminalStateGuard terminalStateGuard(*this);

        // Task LIFE-002/LIFE-003/LIFE-005: see Compass::Dispose(bool)'s
        // identical fix for the full rationale.
        {
            std::lock_guard<std::mutex> lock(control_->mutex);
            control_->owner = nullptr;
        }

        Stop();

        {
            std::lock_guard<std::mutex> lock(instanceCountMutex_);
            --instanceCount_;
            // Task BASE2-007: see Accelerometer::Dispose(bool)'s identical
            // fix for the full rationale.
            assert(instanceCount_ >= 0
                && "Motion::instanceCount_ underflowed -- Dispose(bool) ran more than once for one instance");
        }

        SensorBase<MotionReading>::Dispose(disposing);
    }

    void Motion::SetBackendForTesting(std::unique_ptr<Detail::IMotionBackend> backend)
    {
        // Enforced, not just documented: see Compass::SetBackendForTesting()'s
        // identical fix for the full rationale.
        std::unique_lock<std::mutex> lock(control_->mutex);

        if (started_ || transitioning_)
        {
            throw SensorFailedException(
                "Cannot replace the motion backend while data acquisition is started. Call Stop() first.");
        }

        backendQuiescent_.wait(lock, [this] { return backendCallsInFlight_ == 0; });

        backend_ = std::move(backend);
        setIsSupportedProperty(backend_ ? backend_->IsSupported() : getIsSupportedProperty());

        if (!backend_)
        {
            // The retained reading came from the backend that has just been withdrawn, and
            // nothing stands behind it now. Leaving IsDataValid true made the sensor contradict
            // itself: it reported that its current data was valid while getCurrentValueProperty()
            // refused to hand that data over -- it throws once IsSupported is false, which a
            // withdrawn unsupported backend leaves behind. A reader has to be able to believe one
            // of the two.
            setIsDataValidProperty(false);
        }
    }

    GetTypeNameCPP(Motion, "Microsoft.Devices.Sensors.Motion")
} // namespace Microsoft::Devices::Sensors
