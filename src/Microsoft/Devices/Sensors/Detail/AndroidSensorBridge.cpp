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
        //
        // Task ANDR2-004 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
        // previously called onExit_() directly, with no explicit noexcept
        // and no try/catch -- a destructor with a user-provided body is
        // implicitly noexcept(true) unless stated otherwise (this one was
        // never stated otherwise), so a throwing onExit_() (e.g. Run()'s own
        // exit guard's std::lock_guard construction, if std::mutex::lock()
        // ever threw std::system_error for a genuine OS-level failure) would
        // have already called std::terminate() immediately -- matching
        // ScopeExit's own already-fixed reasoning (Task P7-5) exactly, just
        // never applied here when this class was defined locally for
        // AndroidSensorBridge. Run()'s own exit guard is the one thing that
        // *must* still publish runState_ = NotRunning and notify
        // runExitedCv_ even if some earlier part of its own body somehow
        // threw -- an unswallowed exception here would strand every
        // Stop()/destructor caller waiting on that notification forever, in
        // addition to crashing the process outright.
        template <typename F>
        class RunExitGuard
        {
        public:
            explicit RunExitGuard(F onExit)
                : onExit_(std::move(onExit))
            {
            }

            ~RunExitGuard() noexcept
            {
                try
                {
                    onExit_();
                }
                catch (...)
                {
                    // Swallowed deliberately -- see this class's own doc
                    // comment: letting this propagate would std::terminate()
                    // the whole process (this destructor is noexcept) and,
                    // even if it somehow didn't, would skip publishing the
                    // terminal run state every Stop()/destructor caller is
                    // waiting on.
                }
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

        // Guards worker_, workerThreadId_, runState_, reclaimClaimed_,
        // callback_, timeBetweenUpdates_, and startOutcome_'s reset -- i.e.
        // everything Start()/Stop() touch before handing off to (or waiting
        // on) the worker thread. Deliberately never held across either
        // Start()'s bounded condition-variable wait or Stop()'s blocking
        // join()/non-blocking detach(): both of those happen after this
        // mutex is released, so a reentrant self-stop (called from this
        // bridge's own callback, on its own worker thread) can never
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
        // reclaimClaimed_/runExitedCv_ below, the same "one winner claims
        // the teardown, everyone else waits for it to finish" pattern
        // SensorBase<T>::ClaimDisposalOnce()/WaitForDisposalToComplete()
        // already established for the analogous concurrent-Dispose() race.
        std::mutex stateMutex_;

        // Task ANDROID-BRIDGE-005 (2026-07-16): std::thread::joinable() is
        // NOT a safe proxy for "is this bridge's worker still genuinely
        // running". detach() (used by a reentrant self-stop, see Stop())
        // clears it immediately even though the underlying OS thread keeps
        // executing for a while longer -- using joinable() for that purpose
        // (the pre-fix behavior) let a second Start() reuse this Impl while
        // the just-detached old worker was still alive, corrupting shared
        // state, and let a concurrent external Stop() race a reentrant
        // self-stop's detach() on the same std::thread object (join() on an
        // already-detached thread throws std::system_error). runState_ is
        // the authoritative signal instead: it becomes NotRunning only from
        // inside Run() itself, in the same exit guard that resets looper_
        // (see Run()'s own comment), the one place that knows Run() has
        // truly finished, independent of whether/when the std::thread
        // object itself has been reclaimed.
        enum class RunState
        {
            NotRunning,
            Running,
            Stopping,
        };

        RunState runState_ = RunState::NotRunning;

        // Task ANDROID-BRIDGE-005: stable for the lifetime of one Start()
        // cycle, captured once right after the worker thread is spawned --
        // unlike worker_.get_id() (which becomes the default "no thread" id
        // the instant *anyone* calls detach() or join() on worker_), this
        // never changes underneath a Stop() call trying to determine
        // whether it is running on the worker's own thread (a reentrant
        // self-stop) or an external one, even if a second, nested reentrant
        // Stop() call runs after the first one already reclaimed worker_.
        std::thread::id workerThreadId_;

        // Task ANDROID-BRIDGE-005: true once nobody still needs to call
        // join()/detach() on worker_ for the current Start() cycle --
        // starts true (nothing to reclaim before the first Start()), reset
        // to false when Start() spawns a fresh worker, and flipped back to
        // true by whichever Stop() call is first to claim it (checked and
        // set atomically under stateMutex_). Every other, concurrent Stop()
        // caller that finds this already true never calls joinable()/
        // join()/detach() on worker_ at all for that cycle -- the object may
        // be concurrently touched by the winner's own unlocked join()/
        // detach() call at that exact moment, and std::thread is not safe
        // for concurrent access from two threads even when one side is only
        // reading.
        bool reclaimClaimed_ = true;

        // Signaled from Run()'s own exit guard once runState_ becomes
        // NotRunning, so every external Stop() caller waiting below wakes
        // up once the worker has genuinely finished -- not merely once its
        // std::thread object has been reclaimed (see reclaimClaimed_'s own
        // comment for why those are different events).
        std::condition_variable runExitedCv_;

        std::thread worker_;
        std::atomic<bool> stopRequested_{false};
        SampleCallback callback_;
        System::TimeSpan timeBetweenUpdates_;

        // Task ANDR2-003 (2026-07-17, external audit
        // `audit_devices_2026-07-17.md`): sticky, permanent "this bridge's
        // worker was abandoned rather than genuinely reclaimed" flag, set
        // exactly once by Stop() (see its own comment) if a bounded wait for
        // Run() to finish ever times out -- meaning Run() is presumed stuck
        // inside a real native call (ASensorManager_createEventQueue()/
        // ASensorEventQueue_enableSensor(), the only calls Run() makes
        // before its own poll loop starts checking stopRequested_) that may
        // never return. There is no safe way in C++ to forcibly cancel a
        // thread blocked inside a native call, so once this is set, this
        // AndroidSensorBridge instance is permanently unable to Start()
        // again (a deliberate "fatal backend-health state", not a bug) --
        // even though runState_ may *later* still transition back to
        // NotRunning on its own, if the abandoned worker's blocked call
        // eventually does return. Never reset back to false once set.
        std::atomic<bool> abandoned_{false};

        // Task ANDR2-003: shared bound for both Start()'s startup handshake
        // wait (startCv_) and Stop()'s worker-finished wait (runExitedCv_) --
        // the same underlying concern (a real native NDK call that may never
        // return) applies to both, so both use the same, named timeout
        // rather than two separately-chosen literals.
        static constexpr std::chrono::seconds kNativeCallTimeout{5};

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
            //
            // Task ANDROID-BRIDGE-005 (2026-07-16): also the single,
            // authoritative place that transitions runState_ back to
            // NotRunning and wakes runExitedCv_ -- this is what lets
            // Start()/Stop() tell "Run() has truly finished" apart from
            // "the std::thread object has been reclaimed" (a reentrant
            // self-stop's detach() satisfies the latter immediately, long
            // before Run() actually returns). Runs on every exit path,
            // including both early-failure returns below, so a failed
            // startup also correctly unblocks a caller waiting on this.
            auto looperCleanup = MakeRunExitGuard(
                [this]()
                {
                    looper_.store(nullptr, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        runState_ = RunState::NotRunning;
                    }
                    runExitedCv_.notify_all();
                });

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
                    // doc comment. This project's one consistent
                    // cross-sensor-class timestamp policy (Task
                    // READINGS-003) — see docs/devices-api-coverage.md's
                    // "Timestamp policy" section.
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
            // Guards runState_, Probe(), and every field written below
            // against a second, concurrent Start() call (two threads
            // racing to reassign impl_->worker_ itself would be a data race
            // independent of the gating logic) and against Stop()'s own
            // matching checks. Released (end of this scope) before the
            // bounded condition-variable wait further down, so it is never
            // held across a blocking call and cannot deadlock against
            // Stop() — see this method's own Doxygen comment for what this
            // does and does not make safe.
            std::lock_guard<std::mutex> lock(impl_->stateMutex_);

            // Task: repeated Start/Stop safety. Assigning a new std::thread
            // over an already-joinable one calls std::terminate() (the C++
            // standard requires it) -- this must never happen. Calling
            // Start() while already started is a documented failure:
            // return false immediately, without touching the running
            // worker at all. Callers that want a restart must call Stop()
            // first.
            //
            // Task ANDROID-BRIDGE-005 (2026-07-16): gated on runState_, not
            // worker_.joinable() -- a reentrant self-stop's detach() (see
            // Stop()) clears joinable() immediately even though the old
            // worker's Run() is still executing, which previously let a
            // too-soon Start() reuse this Impl while the old worker was
            // still alive. runState_ only returns to NotRunning once Run()
            // itself confirms it has actually finished, closing that gap.
            //
            // Task ANDR2-003 (2026-07-17): also gated on abandoned_ -- once
            // a prior Stop() call gave up waiting for a genuinely wedged
            // worker (see Stop()'s own comment) and set this, runState_ may
            // *still* eventually flip back to NotRunning on its own later,
            // if the abandoned worker's blocked native call ever does
            // return -- abandoned_ is never reset, so this check keeps
            // refusing to Start() again regardless, treating a once-wedged
            // bridge as permanently unusable rather than risking a new
            // worker racing whatever the abandoned one is still doing.
            if (impl_->abandoned_.load(std::memory_order_acquire)
                || impl_->runState_ != Impl::RunState::NotRunning)
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
            impl_->reclaimClaimed_ = false;
            impl_->runState_ = Impl::RunState::Running;

            // Task ANDR2-001 (2026-07-17, external audit
            // `audit_devices_2026-07-17.md`): clear any stale
            // SetSampleInterval() request left over from a PREVIOUS
            // Start()/Stop() cycle. rateChangeRequested_ is only ever
            // cleared by Run()'s own poll loop (see its own comment) -- if a
            // SetSampleInterval() call's request arrived too late in a
            // previous run (e.g. after the worker's poll loop already
            // observed a concurrent Stop()'s stopRequested_ and committed to
            // exiting before reaching its next rateChangeRequested_ check),
            // the flag survives untouched across runState_ returning to
            // NotRunning. Without this reset, the *new* worker spawned by
            // this very Start() call would see rateChangeRequested_ still
            // true on an early poll iteration and wrongly re-apply
            // pendingTimeBetweenUpdates_ -- a value from an already-ended
            // run -- silently overriding the fresh timeBetweenUpdates_ this
            // call just applied. Safe to reset unconditionally here: any
            // SetSampleInterval() call that should legitimately affect the
            // run this Start() call is about to begin can only be made after
            // this locked section releases stateMutex_ (SetSampleInterval()
            // requires runState_ != NotRunning, which only becomes true
            // inside this very section), so it is impossible for a
            // legitimate new-run request to be mistakenly cleared by this
            // reset. pendingTimeBetweenUpdates_ is also reset to this call's
            // own interval (rather than left holding whatever stale value it
            // last had) purely for state consistency -- it has no observable
            // effect unless rateChangeRequested_ is true, which it no longer
            // is at this point.
            impl_->rateChangeRequested_.store(false, std::memory_order_release);
            impl_->pendingTimeBetweenUpdates_ = timeBetweenUpdates;

            // Captures impl_ (a shared_ptr copy), not this -- see
            // Impl::Run()'s own doc comment and the shared_ptr<Impl>
            // member's doc comment for the full use-after-free rationale
            // this closes.
            std::shared_ptr<Impl> implForThread = impl_;
            impl_->worker_ = std::thread([implForThread]() { implForThread->Run(); });

            // Task ANDROID-BRIDGE-005: captured once, immediately, on this
            // (the calling, not the worker) thread, while still holding the
            // lock -- see workerThreadId_'s own doc comment for why this
            // must not be re-derived later from worker_.get_id() itself.
            impl_->workerThreadId_ = impl_->worker_.get_id();
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
            lock, Impl::kNativeCallTimeout,
            [this]() { return impl_->startOutcome_ != Impl::StartOutcome::Pending; });
        lock.unlock();

        if (!signaled || impl_->startOutcome_ != Impl::StartOutcome::Success)
        {
            // Timed out, or Run() reported failure: stop so we never leak a
            // half-started or permanently-wedged worker thread, and report
            // the failure honestly rather than the stale "true" this method
            // used to return unconditionally.
            //
            // Task ANDR2-003 (2026-07-17): if this timed out (!signaled),
            // Run() is presumed stuck inside a real native call
            // (ASensorManager_createEventQueue()/ASensorEventQueue_enableSensor())
            // that may never return. Stop() itself is now bounded (see its
            // own comment) -- it no longer risks blocking indefinitely on
            // this same wedged call, so calling it unconditionally here
            // remains correct and safe.
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
        // Task ANDROID-BRIDGE-005 (2026-07-16): isWorkerThread is derived
        // from the stable workerThreadId_ captured at spawn time, never
        // from impl_->worker_.get_id() -- the latter collapses to the
        // default "no thread" id the instant *anyone* calls join()/detach()
        // on worker_, which would silently misclassify a second, nested
        // reentrant self-stop call (one that runs after a first self-stop
        // already detached the thread object) as "not the worker thread"
        // and send it into the blocking wait below, deadlocking against
        // its own still-unwound call stack.
        bool isWorkerThread = false;
        bool shouldReclaim = false;
        {
            // Task: AndroidSensorBridge::Start()/Stop() thread safety. Same
            // mutex Start() uses, guarding runState_/reclaimClaimed_ and the
            // stop signal (stopRequested_ store + ALooper_wake) against a
            // concurrent Start()/Stop() race on impl_->worker_ itself.
            // Released (end of this scope) before the blocking join() /
            // non-blocking detach() below -- never held across either, so
            // a reentrant self-stop from this bridge's own callback (on
            // its own worker thread) can never deadlock against it.
            std::lock_guard<std::mutex> lock(impl_->stateMutex_);

            isWorkerThread = (std::this_thread::get_id() == impl_->workerThreadId_);

            if (impl_->runState_ == Impl::RunState::Running)
            {
                impl_->stopRequested_.store(true, std::memory_order_release);
                ALooper* looper = impl_->looper_.load(std::memory_order_acquire);
                if (looper != nullptr)
                {
                    ALooper_wake(looper);
                }
                impl_->runState_ = Impl::RunState::Stopping;
            }

            // Task ANDROID-BRIDGE-005: exactly one caller, across every
            // combination of self/external and however many call Stop()
            // concurrently, ever claims the right to touch impl_->worker_
            // (join()/detach()) for a given Start() cycle -- reclaimClaimed_
            // starts true (Start() resets it to false only once it has
            // spawned a fresh, genuinely joinable worker_, so the first
            // claimant here is guaranteed to find it truly joinable without
            // needing to call joinable() itself, which would otherwise risk
            // racing a concurrent unlocked detach()/join() from a previous
            // claimant of an *earlier* cycle).
            if (!impl_->reclaimClaimed_)
            {
                impl_->reclaimClaimed_ = true;
                shouldReclaim = true;
            }
        }

        if (shouldReclaim)
        {
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
                // Task ANDROID-BRIDGE-003 (2026-07-06): two distinct
                // external threads could previously both reach this branch
                // and both call worker_.join() concurrently on the same
                // std::thread -- a real data race, since join() is
                // non-const. Fixed with the "one winner claims the
                // teardown, everyone else waits for it" pattern
                // SensorBase<T>'s ClaimDisposalOnce()/
                // WaitForDisposalToComplete() already established --
                // reclaimClaimed_ above is that claim, now shared with the
                // self-stop branch as well (Task ANDROID-BRIDGE-005), which
                // additionally closes the case where a reentrant self-stop
                // and a concurrent external caller raced on this same
                // worker_ object.
                //
                // Task ANDR2-003 (2026-07-17, external audit
                // `audit_devices_2026-07-17.md`): an unconditional join()
                // here previously defeated Start()'s own bounded startup
                // wait -- if Run() is stuck inside a real native call
                // (ASensorManager_createEventQueue()/
                // ASensorEventQueue_enableSensor(), the only calls Run()
                // makes before its poll loop starts re-checking
                // stopRequested_ -- see Run()'s own body), a plain join()
                // here blocks for exactly as long as that stuck call does,
                // which may be forever, no matter how quickly Start()'s own
                // startCv_ wait_for() gave up. There is no safe way in C++
                // to forcibly cancel a thread blocked inside a native call,
                // so this now waits, bounded, for Run() to genuinely finish
                // (runState_ reaching NotRunning, signaled by Run()'s own
                // exit guard) instead of joining unconditionally. If Run()
                // finishes within the bound, join() immediately afterward is
                // safe and near-instant (runState_ only reaches NotRunning
                // once Run() is already at its very last statement). If it
                // does not, this gives up: detach() (safe on any joinable
                // thread regardless of whether the underlying OS thread has
                // actually finished -- it just severs this std::thread
                // object's association with it) and permanently mark this
                // bridge abandoned_ (see that field's own doc comment) --
                // this bridge instance can never Start() again afterward, a
                // deliberate fatal backend-health state rather than an
                // attempt to reuse or race whatever the abandoned worker is
                // still doing.
                std::unique_lock<std::mutex> waitLock(impl_->stateMutex_);
                const bool finished = impl_->runExitedCv_.wait_for(
                    waitLock, Impl::kNativeCallTimeout,
                    [this] { return impl_->runState_ == Impl::RunState::NotRunning; });
                waitLock.unlock();

                if (finished)
                {
                    impl_->worker_.join();
                }
                else
                {
                    impl_->worker_.detach();
                    impl_->abandoned_.store(true, std::memory_order_release);
                }
            }
        }

        if (isWorkerThread)
        {
            // Never block waiting for runState_ to reach NotRunning here:
            // Run() -- several stack frames below this very call, since
            // this is a reentrant self-stop from the worker's own
            // callback -- cannot finish executing (and therefore cannot
            // reach the exit guard that sets runState_ = NotRunning) until
            // this call, and the callback it is part of, returns. Waiting
            // here would self-deadlock (Task ANDROID-BRIDGE-005).
            return;
        }

        // Every external caller's Stop() is synchronous with respect to
        // Run() genuinely finishing, not merely with respect to worker_
        // having been reclaimed -- those are different events whenever the
        // claimant above was a reentrant self-stop (which reclaims via a
        // near-instant detach(), long before the still-running Run() call
        // actually returns). runState_ becomes NotRunning only from inside
        // Run()'s own exit guard, so this predicate is already true
        // immediately whenever the claimant instead reclaimed via a
        // blocking join() (which cannot return before Run() has finished).
        //
        // Task ANDR2-003 (2026-07-17): also unblocks on abandoned_ -- an
        // unconditional wait for NotRunning here would otherwise still hang
        // indefinitely for a *second*, non-claimant concurrent Stop() caller
        // even after the claimant above already gave up (bounded) and
        // detach()'d a genuinely wedged worker, since a permanently-stuck
        // native call may never let runState_ reach NotRunning at all.
        std::unique_lock<std::mutex> lock(impl_->stateMutex_);
        impl_->runExitedCv_.wait(lock, [this] {
            return impl_->runState_ == Impl::RunState::NotRunning
                || impl_->abandoned_.load(std::memory_order_acquire);
        });
#endif
    }

    void AndroidSensorBridge::SetSampleInterval(const System::TimeSpan& timeBetweenUpdates)
    {
#ifdef __ANDROID__
        std::lock_guard<std::mutex> lock(impl_->stateMutex_);

        // Task ANDROID-BRIDGE-005 (2026-07-16): checks runState_, not
        // worker_.joinable() -- this method must never call any method on
        // impl_->worker_ at all (see reclaimClaimed_'s own doc comment for
        // why an unguarded read of it here could race a concurrent Stop()
        // claimant's unlocked join()/detach() call on the very same
        // std::thread object).
        if (impl_->runState_ == Impl::RunState::NotRunning)
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
