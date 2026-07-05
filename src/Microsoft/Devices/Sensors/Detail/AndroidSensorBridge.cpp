// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp"

#ifdef __ANDROID__
#include <android/looper.h>
#include <android/sensor.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace Microsoft::Devices::Sensors::Detail
{
#ifdef __ANDROID__
    struct AndroidSensorBridge::Impl
    {
        explicit Impl(int sensorType)
            : sensorType_(sensorType)
        {
        }

        int sensorType_;
        ASensorManager* manager_ = nullptr;
        ASensor const* sensor_ = nullptr;
        ASensorEventQueue* queue_ = nullptr;

        // Non-null only while a worker thread is actually running its poll
        // loop (set near the top of Run(), reset back to nullptr just
        // before Run() returns) -- so a stale/destroyed ALooper* is never
        // observable once the worker has fully exited (Task: reset stale
        // looper state).
        std::atomic<ALooper*> looper_{nullptr};

        // Guards worker_, callback_, timeBetweenUpdates_, and startOutcome_'s
        // reset -- i.e. everything Start()/Stop() touch before handing off
        // to (or waiting on) the worker thread. Deliberately never held
        // across either Start()'s bounded condition-variable wait or Stop()'s
        // blocking join()/non-blocking detach(): both of those happen after
        // this mutex is released, so a reentrant self-stop (called from
        // this bridge's own callback, on its own worker thread) can never
        // deadlock against it. What this mutex does NOT make safe: two
        // distinct external (non-worker) threads calling Stop() on the same
        // bridge at the same time would both pass the "is a worker
        // running" check and could both attempt to join() the same
        // std::thread concurrently -- see Start()'s/Stop()'s own doc
        // comments for why that case is documented as unsupported rather
        // than further serialized here.
        std::mutex stateMutex_;

        std::thread worker_;
        std::atomic<bool> stopRequested_{false};
        SampleCallback callback_;
        System::TimeSpan timeBetweenUpdates_;

        // Startup handshake (Task: async startup reporting): Run() signals
        // one of these exactly once, early in its own execution, so
        // Start() can block (briefly, bounded) until real success/failure
        // is known, instead of optimistically returning true the instant
        // the worker thread is merely spawned.
        enum class StartOutcome
        {
            Pending,
            Success,
            Failure,
        };

        std::mutex startMutex_;
        std::condition_variable startCv_;
        StartOutcome startOutcome_ = StartOutcome::Pending;

        void SignalStartOutcome(StartOutcome outcome)
        {
            {
                std::lock_guard<std::mutex> lock(startMutex_);
                startOutcome_ = outcome;
            }
            startCv_.notify_all();
        }

        // ASensorManager_getInstanceForPackage() (the non-deprecated
        // replacement) requires API 26+; this project's minimum target is
        // API 24 (docs/devices-build.md). The deprecated,
        // package-name-agnostic ASensorManager_getInstance() is the correct
        // choice for that minimum, not an oversight — silencing the
        // deprecation warning locally rather than raising the project's
        // minimum API level for this one call.
        static ASensorManager* GetManager()
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
            return ASensorManager_getInstance();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        [[nodiscard]] bool Probe()
        {
            if (manager_ == nullptr)
            {
                manager_ = GetManager();
            }
            if (manager_ == nullptr)
            {
                return false;
            }
            if (sensor_ == nullptr)
            {
                sensor_ = ASensorManager_getDefaultSensor(manager_, sensorType_);
            }
            return sensor_ != nullptr;
        }

        // Runs entirely on the dedicated worker thread. Takes a shared_ptr
        // to itself (captured by the thread's lambda, see Start() below) so
        // this object stays alive for this method's entire duration, even
        // if the owning AndroidSensorBridge (and everything above it) is
        // destroyed first — see Stop()'s doc comment for the full
        // rationale and its accepted, documented remaining boundary.
        void Run()
        {
            // ALooper is thread-affine: must be prepared on the same
            // thread that later polls it (Task DEVICES-0078) — hence this
            // dedicated worker thread, rather than requiring the game loop
            // to pump anything itself.
            ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
            looper_.store(looper, std::memory_order_release);

            queue_ = ASensorManager_createEventQueue(manager_, looper, 0, nullptr, nullptr);
            if (queue_ == nullptr)
            {
                SignalStartOutcome(StartOutcome::Failure);
                return;
            }

            // Task: async startup reporting must also cover a failed
            // enable, not just a failed queue creation -- ASensorEventQueue_
            // enableSensor() returning negative means the sensor was never
            // actually enabled, so no samples will ever arrive; previously
            // this return value was silently discarded and Start() still
            // reported success.
            if (ASensorEventQueue_enableSensor(queue_, sensor_) < 0)
            {
                ASensorManager_destroyEventQueue(manager_, queue_);
                queue_ = nullptr;
                SignalStartOutcome(StartOutcome::Failure);
                return;
            }

            // Task: handle ASensorEventQueue_setEventRate() explicitly. A
            // negative return here means the platform rejected the
            // requested rate, not that delivery failed -- the sensor is
            // already enabled (the check above already succeeded) and will
            // keep delivering events, just at whatever rate the platform
            // was already using (its own default, or a rate left over from
            // a previous Start() on this device) instead of the one
            // requested here. Deliberately non-fatal, unlike the
            // queue-creation and enable failures above: treating this as a
            // startup failure would abort otherwise-working sensor
            // delivery over a rate mismatch alone, which is worse than
            // accepting the degraded rate.
            if (ASensorEventQueue_setEventRate(
                    queue_, sensor_, ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(timeBetweenUpdates_))
                < 0)
            {
                // Intentionally not signaling Failure here -- see comment above.
            }

            SignalStartOutcome(StartOutcome::Success);

            // 100ms bounded wait: re-checks stopRequested_ promptly even if
            // ALooper_wake() is missed due to the benign startup race on
            // looper_ (Stop() may run before this store above completes) —
            // that race costs at most one extra ~100ms wait, never a hang.
            while (!stopRequested_.load(std::memory_order_acquire))
            {
                ALooper_pollOnce(100, nullptr, nullptr, nullptr);

                ASensorEvent event;
                // Re-checks stopRequested_ before every callback invocation,
                // not just once per outer iteration: a callback can
                // reentrantly call Stop() (e.g. the owning Compass/Motion
                // instance disposing itself from inside its own
                // CurrentValueChanged handler) -- once that happens, no
                // further callback_() call is safe, since callback_ itself
                // may have captured a pointer to an object whose *owner* is
                // being torn down as part of that same reentrant call (see
                // Stop()'s doc comment on the accepted remaining boundary).
                // Without this check, a second already-queued event could
                // still trigger callback_() again with a now-invalid
                // captured pointer even though Stop() had already run.
                while (!stopRequested_.load(std::memory_order_acquire)
                       && ASensorEventQueue_getEvents(queue_, &event, 1) > 0)
                {
                    AndroidSensorSample sample;
                    sample.ValueCount = 16;
                    for (int i = 0; i < 16; ++i)
                    {
                        sample.Values[i] = event.data[i];
                    }
                    // event.vector.status occupies the same union memory as
                    // Values[3] would (see AndroidSensorSample::Status's own
                    // doc comment) -- read through the correctly-typed
                    // .vector union member, not reinterpreted from Values.
                    sample.Status = event.vector.status;
                    // Wall-clock time of delivery, deliberately NOT
                    // event.timestamp (a monotonic boot-time nanosecond
                    // value) — see AndroidSensorSample::Timestamp's own
                    // doc comment.
                    sample.Timestamp = System::DateTimeOffset::getUtcNowProperty();

                    if (callback_)
                    {
                        // Task: callback exception policy. An exception
                        // escaping a std::thread's entry point calls
                        // std::terminate() and crashes the whole process --
                        // strictly worse than swallowing it. Mirrors
                        // Detail::SdlSensorSubsystem<TSensor>::
                        // DispatchToInstances()'s identical policy (Task
                        // P8-5) for the SDL-backed sensors.
                        try
                        {
                            callback_(sample);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }

            ASensorEventQueue_disableSensor(queue_, sensor_);
            ASensorManager_destroyEventQueue(manager_, queue_);
            queue_ = nullptr;

            // Task: reset stale Android looper state. A looper obtained via
            // ALooper_prepare() is thread-local and torn down by the NDK
            // once this (its owning) thread exits -- once Run() returns and
            // this worker thread terminates, `looper` above is no longer a
            // valid pointer. Resetting looper_ to nullptr here keeps the
            // invariant "a non-null looper_ implies a live, currently-
            // running worker" exactly true, so nothing downstream (a future
            // diagnostic, or a defensive read added later) can observe a
            // dangling ALooper* through this field.
            looper_.store(nullptr, std::memory_order_release);
        }
    };
#else
    struct AndroidSensorBridge::Impl
    {
        explicit Impl(int)
        {
        }
    };
#endif

    AndroidSensorBridge::AndroidSensorBridge(int androidSensorType)
        : impl_(std::make_shared<Impl>(androidSensorType))
    {
    }

    AndroidSensorBridge::~AndroidSensorBridge()
    {
        Stop();
    }

    bool AndroidSensorBridge::IsAvailable() const
    {
#ifdef __ANDROID__
        return impl_->Probe();
#else
        return false;
#endif
    }

    bool AndroidSensorBridge::Start(const System::TimeSpan& timeBetweenUpdates, SampleCallback callback)
    {
#ifdef __ANDROID__
        {
            // Task: AndroidSensorBridge::Start()/Stop() thread safety.
            // Guards the joinable() check, Probe(), and every field
            // written below against a second, concurrent Start() call (two
            // threads racing to reassign impl_->worker_ itself would be a
            // data race independent of the joinable() logic) and against
            // Stop()'s own matching check reading impl_->worker_ mid-write.
            // Released (end of this scope) before the bounded
            // condition-variable wait further down, so it is never held
            // across a blocking call and cannot deadlock against Stop() —
            // see this method's own Doxygen comment for what this does and
            // does not make safe.
            std::lock_guard<std::mutex> lock(impl_->stateMutex_);

            // Task: repeated Start/Stop safety. Assigning a new std::thread
            // over an already-joinable one calls std::terminate() (the C++
            // standard requires it) -- this must never happen. Calling
            // Start() while already started is a documented failure:
            // return false immediately, without touching the running
            // worker at all. Callers that want a restart must call Stop()
            // first.
            if (impl_->worker_.joinable())
            {
                return false;
            }

            if (!impl_->Probe())
            {
                return false;
            }

            impl_->timeBetweenUpdates_ = timeBetweenUpdates;
            impl_->callback_ = std::move(callback);
            impl_->stopRequested_.store(false, std::memory_order_release);
            impl_->startOutcome_ = Impl::StartOutcome::Pending;

            // Captures impl_ (a shared_ptr copy), not this -- see
            // Impl::Run()'s own doc comment and the shared_ptr<Impl>
            // member's doc comment for the full use-after-free rationale
            // this closes.
            std::shared_ptr<Impl> implForThread = impl_;
            impl_->worker_ = std::thread([implForThread]() { implForThread->Run(); });
        }

        // Task: async startup reporting. Blocks until Run() has genuinely
        // succeeded or failed to create/enable the sensor queue -- never
        // indefinitely: ASensorManager_createEventQueue()/
        // ASensorEventQueue_enableSensor() are expected to complete in
        // microseconds under normal conditions, so a generous bounded
        // timeout here is a safety net against a truly wedged platform
        // call, not a normal-path wait.
        std::unique_lock<std::mutex> lock(impl_->startMutex_);
        const bool signaled = impl_->startCv_.wait_for(
            lock, std::chrono::seconds(5),
            [this]() { return impl_->startOutcome_ != Impl::StartOutcome::Pending; });
        lock.unlock();

        if (!signaled || impl_->startOutcome_ != Impl::StartOutcome::Success)
        {
            // Timed out, or Run() reported failure: stop and join so we
            // never leak a half-started or permanently-wedged worker
            // thread, and report the failure honestly rather than the
            // stale "true" this method used to return unconditionally.
            Stop();
            return false;
        }

        return true;
#else
        (void)timeBetweenUpdates;
        (void)callback;
        return false;
#endif
    }

    void AndroidSensorBridge::Stop()
    {
#ifdef __ANDROID__
        bool isWorkerThread = false;
        {
            // Task: AndroidSensorBridge::Start()/Stop() thread safety. Same
            // mutex Start() uses, guarding the joinable() check and the
            // stop signal (stopRequested_ store + ALooper_wake) against a
            // concurrent Start()/Stop() race on impl_->worker_ itself.
            // Released (end of this scope) before the blocking join() /
            // non-blocking detach() below -- never held across either, so
            // a reentrant self-stop from this bridge's own callback (on
            // its own worker thread) can never deadlock against it. This
            // does NOT make two distinct external (non-worker) threads
            // calling Stop() concurrently safe from a double-join race —
            // see this method's own Doxygen comment for that documented,
            // unsupported boundary.
            std::lock_guard<std::mutex> lock(impl_->stateMutex_);

            if (!impl_->worker_.joinable())
            {
                return;
            }

            impl_->stopRequested_.store(true, std::memory_order_release);
            ALooper* looper = impl_->looper_.load(std::memory_order_acquire);
            if (looper != nullptr)
            {
                ALooper_wake(looper);
            }

            isWorkerThread = (std::this_thread::get_id() == impl_->worker_.get_id());
        }

        if (isWorkerThread)
        {
            // Reentrant call from within this bridge's own callback, on
            // its own worker thread (e.g. a Compass/Motion instance
            // stopping itself from inside its own CurrentValueChanged
            // handler). Joining our own thread would throw
            // std::system_error (resource_deadlock_would_occur) —
            // detach instead and let Run() finish exiting on its own.
            // Impl itself stays alive via the worker thread's own
            // shared_ptr<Impl> copy (captured in Start()'s lambda),
            // independent of this AndroidSensorBridge wrapper's
            // lifetime -- so Run()'s own `this` (Impl*) never dangles
            // even if this wrapper (and impl_, this object's own
            // shared_ptr reference) is destroyed immediately after this
            // call returns. This does NOT extend the lifetime of
            // whatever object owns *this bridge* (e.g.
            // AndroidCompassBackend) -- destroying that from within
            // this same callback remains an accepted, unsupported
            // boundary, identical in spirit to Accelerometer's own
            // documented "destroying from within your own callback"
            // limitation.
            impl_->worker_.detach();
        }
        else
        {
            impl_->worker_.join();
        }
#endif
    }
} // namespace Microsoft::Devices::Sensors::Detail
