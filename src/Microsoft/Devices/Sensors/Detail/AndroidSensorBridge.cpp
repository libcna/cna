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
#include <utility>
#endif

namespace Microsoft::Devices::Sensors::Detail
{
#ifdef __ANDROID__
    namespace
    {
        // Minimal RAII scope-exit guard, local to this translation unit —
        // mirrors Detail::ScopeExit's established pattern in
        // SdlSensorSubsystem.hpp for the identical reason (a cleanup step
        // that must run on *every* exit from a function, including exit
        // paths added later, must not depend on every future author
        // remembering to repeat it manually). Defined locally here rather
        // than reusing that header's version to avoid pulling SDL3 headers
        // into this deliberately SDL-free, NDK-only file.
        template <typename F>
        class RunExitGuard
        {
        public:
            explicit RunExitGuard(F onExit)
                : onExit_(std::move(onExit))
            {
            }

            ~RunExitGuard()
            {
                onExit_();
            }

            RunExitGuard(const RunExitGuard&) = delete;
            RunExitGuard& operator=(const RunExitGuard&) = delete;

        private:
            F onExit_;
        };

        template <typename F>
        RunExitGuard<F> MakeRunExitGuard(F onExit)
        {
            return RunExitGuard<F>(std::move(onExit));
        }
    } // namespace

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
        // deadlock against it.
        //
        // Task ANDROID-BRIDGE-003 (2026-07-06): this mutex alone previously
        // did NOT make it safe for two distinct external (non-worker)
        // threads to call Stop() on the same bridge concurrently -- both
        // would pass the "is a worker running" check (joinable() is still
        // true until someone actually calls join()/detach(), which happens
        // *after* this mutex is released) and both would then call
        // worker_.join() on the same std::thread object concurrently, a
        // real data race (join() is a non-const member function). Fixed by
        // joinClaimed_/stopFinishedCv_ below, the same "one winner claims
        // the teardown, everyone else waits for it to finish" pattern
        // SensorBase<T>::ClaimDisposalOnce()/WaitForDisposalToComplete()
        // already established for the analogous concurrent-Dispose() race.
        std::mutex stateMutex_;

        // Task ANDROID-BRIDGE-003: true once some external (non-worker)
        // thread has claimed the right to actually call worker_.join() for
        // the current Stop() request -- checked and set atomically under
        // stateMutex_. A second, concurrent external Stop() caller that
        // finds this already true does not call join() itself (which would
        // race the winner's own join() call); it instead waits on
        // stopFinishedCv_ until the winner's join() has completed, so
        // Stop() remains synchronous (the worker is guaranteed joined by
        // the time any caller's Stop() returns) for every caller, not just
        // the winner. Reset to false at the top of a fresh Start().
        bool joinClaimed_ = false;

        // Signaled once the join-claiming thread's worker_.join() call
        // returns, so any concurrent Stop() caller waiting in the branch
        // above wakes up. See joinClaimed_'s own comment.
        std::condition_variable stopFinishedCv_;

        std::thread worker_;
        std::atomic<bool> stopRequested_{false};
        SampleCallback callback_;
        System::TimeSpan timeBetweenUpdates_;

        // Task ANDROID-BRIDGE-002: SetSampleInterval()'s pending value,
        // guarded by stateMutex_ (same field access discipline as
        // timeBetweenUpdates_ above) -- separate from timeBetweenUpdates_
        // itself, which remains "the interval Start() was last called
        // with" and is otherwise unused after startup. rateChangeRequested_
        // is checked (and atomically cleared) by Run() every poll
        // iteration; true means pendingTimeBetweenUpdates_ holds a value
        // Run() has not yet applied via ASensorEventQueue_setEventRate()
        // on this bridge's own worker thread -- the only thread that ever
        // touches queue_/sensor_.
        std::atomic<bool> rateChangeRequested_{false};
        System::TimeSpan pendingTimeBetweenUpdates_;

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

            // Task: fix looper cleanup on all Run() exit paths. Guarantees
            // looper_ is reset to nullptr no matter which exit path out of
            // this function is taken -- previously only the normal
            // end-of-loop path did this; the two early-failure returns
            // below (and any later-added early return) forgot it, leaving
            // looper_ pointing at a looper that the NDK tears down the
            // moment this thread exits (thread-local, see the comment this
            // used to carry at the bottom of this function). This guard's
            // destructor runs at the very end of Run(), immediately after
            // whichever `return` statement is hit -- always before this
            // worker thread's OS-level teardown actually destroys the
            // looper, and always after this function's own last use of
            // `queue_`/`sensor_`, so Stop() (reading looper_ from another
            // thread) can never observe a stale, already-destroyed pointer.
            auto looperCleanup = MakeRunExitGuard(
                [this]() { looper_.store(nullptr, std::memory_order_release); });

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

                // Task ANDROID-BRIDGE-002: pick up a pending SetSampleInterval()
                // request, if any, before processing this iteration's events.
                // exchange(false) atomically clears the flag so a request that
                // arrives while this branch is running is not lost -- it will
                // simply be seen (and applied) on the next iteration instead.
                if (rateChangeRequested_.exchange(false, std::memory_order_acq_rel))
                {
                    System::TimeSpan newInterval;
                    {
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        newInterval = pendingTimeBetweenUpdates_;
                    }

                    // Same non-fatal-rejection handling as the initial
                    // Start()-time call above: a negative return means the
                    // platform rejected the new rate, not that delivery
                    // failed -- the sensor keeps delivering at whatever rate
                    // was already in effect.
                    ASensorEventQueue_setEventRate(
                        queue_, sensor_, ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(newInterval));
                }

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
                    // Task ANDROID-BRIDGE-001 (2026-07-06): ValueCount now
                    // reflects the real per-sensor-type value count (3 for
                    // a vector sensor, 5 for a rotation vector, see
                    // GetValueCountForAndroidSensorType()'s own doc comment)
                    // instead of the previous unconditional 16 -- Values
                    // itself still always copies the full raw union (16
                    // floats is the NDK's own fixed ASensorEvent::data size,
                    // never a variable-length array, so there is no actual
                    // out-of-bounds risk either way; this is a correctness/
                    // clarity fix for ValueCount's own meaning, not a bounds
                    // fix).
                    sample.ValueCount = GetValueCountForAndroidSensorType(sensorType_);
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

            // looperCleanup's destructor (running here, at the normal end
            // of this function) resets looper_ to nullptr -- see its own
            // declaration above for the full rationale covering every exit
            // path, not just this one.
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
            impl_->joinClaimed_ = false;

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
            // its own worker thread) can never deadlock against it.
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
            // Task ANDROID-BRIDGE-003 (2026-07-06): two distinct external
            // threads could previously both reach this branch and both
            // call worker_.join() concurrently on the same std::thread --
            // a real data race, since join() is non-const. Fixed with the
            // same "one winner claims the teardown, everyone else waits
            // for it" pattern SensorBase<T>'s ClaimDisposalOnce()/
            // WaitForDisposalToComplete() already established: only the
            // first external caller to observe joinClaimed_ == false
            // claims it and actually calls join(); every other concurrent
            // caller instead waits on stopFinishedCv_ until the winner's
            // join() has completed. This keeps Stop() synchronous (the
            // worker is guaranteed joined by the time *any* caller's
            // Stop() returns), not just for the winner.
            bool shouldJoin = false;
            {
                std::lock_guard<std::mutex> lock(impl_->stateMutex_);
                if (!impl_->joinClaimed_)
                {
                    impl_->joinClaimed_ = true;
                    shouldJoin = true;
                }
            }

            if (shouldJoin)
            {
                impl_->worker_.join();
                impl_->stopFinishedCv_.notify_all();
            }
            else
            {
                std::unique_lock<std::mutex> lock(impl_->stateMutex_);
                impl_->stopFinishedCv_.wait(lock, [this] { return !impl_->worker_.joinable(); });
            }
        }
#endif
    }

    void AndroidSensorBridge::SetSampleInterval(const System::TimeSpan& timeBetweenUpdates)
    {
#ifdef __ANDROID__
        std::lock_guard<std::mutex> lock(impl_->stateMutex_);

        if (!impl_->worker_.joinable())
        {
            // Not currently started -- nothing live to update. The next
            // Start() call already takes its own explicit interval
            // parameter, so there is nothing useful to stash here either.
            return;
        }

        impl_->pendingTimeBetweenUpdates_ = timeBetweenUpdates;
        impl_->rateChangeRequested_.store(true, std::memory_order_release);

        // Wakes the looper so Run()'s ALooper_pollOnce(100, ...) picks up
        // the pending request promptly rather than waiting out up to
        // ~100ms of its own poll timeout -- same technique Stop() already
        // uses to wake a possibly-blocked poll.
        ALooper* looper = impl_->looper_.load(std::memory_order_acquire);
        if (looper != nullptr)
        {
            ALooper_wake(looper);
        }
#else
        (void)timeBetweenUpdates;
#endif
    }
} // namespace Microsoft::Devices::Sensors::Detail
