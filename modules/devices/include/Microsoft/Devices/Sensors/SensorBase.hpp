// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <ratio>
#include <type_traits>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/IDisposable.hpp"
#include "System/EventHandler.hpp"
#include "System/EventArgs.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Abstract base class for device sensors; provides current-value and event-notification infrastructure.
     *
     * See `docs/devices-thread-safety.md` for the full, consolidated
     * thread-safety contract shared by every derived sensor class.
     */
    template <typename TSensorReading>
    class SensorBase : public System::Object, public System::IDisposable
    {
        static_assert(std::is_base_of_v<ISensorReading, TSensorReading>,
                      "TSensorReading must derive from ISensorReading");

    private:
        bool disposed_;
        bool disposalClaimed_;
        bool isDataValid_;
        bool isSupported_;
        System::TimeSpan timeBetweenUpdates_;
        TSensorReading currentValue_;

        /**
         * True once ShouldAcceptUpdateAt() has accepted at least one update
         * since construction or the last ResetUpdateThrottle() call (Task
         * SENSORBASE-001/ACCEL-005/GYRO-004). Guarded by mutex_, same as
         * lastAcceptedUpdateTime_ below.
         */
        bool hasAcceptedUpdate_;

        /**
         * @brief Monotonic time of the last update ShouldAcceptUpdateAt() accepted. Guarded by mutex_.
         *
         * Task (2026-07-06 stabilization pass): deliberately `std::chrono::steady_clock`,
         * not `System::DateTimeOffset`/wall-clock time. A throttle *interval*
         * measurement must never be able to go backward or jump — wall-clock
         * time can (NTP step corrections, manual clock changes, leap-second
         * slew), which would either wedge the throttle open (a backward
         * step makes `now - lastAcceptedUpdateTime_` go negative, comparing
         * as "less than any non-negative interval," so every subsequent
         * update is rejected until real time catches back up to where it
         * was) or throw it wide open (a forward step makes the very next
         * update look like it arrived after an arbitrarily long gap,
         * defeating the throttle entirely for one event). `steady_clock` is
         * specified by the C++ standard to never be adjusted, making this
         * risk structurally impossible rather than just unlikely. Sensor
         * *reading* timestamps (`AccelerometerReading::Timestamp` etc.)
         * deliberately stay wall-clock (`DateTimeOffset`, matching the real
         * WP7 API's `DateTimeOffset`-typed `Timestamp` property) — only this
         * internal throttle *decision* changed.
         */
        std::chrono::steady_clock::time_point lastAcceptedUpdateTime_;

        /**
         * Guards currentValue_/isDataValid_ (Task P5-2): for real,
         * SDL3-backed sensors (Accelerometer/Gyroscope), setCurrentValueProperty()/
         * setIsDataValidProperty() are called from DispatchSensorReading(),
         * which may run on whatever thread SDL invokes the sensor
         * event-watch callback on — not necessarily the game/user thread
         * that calls getCurrentValueProperty()/getIsDataValidProperty().
         * Never held while calling CurrentValueChanged.Raise() — a
         * subscriber's handler can legitimately call back into this sensor
         * (e.g. Dispose(), another getter), and raising under a lock risks
         * deadlock.
         *
         * See `docs/devices-thread-safety.md` for the full, consolidated
         * thread-safety contract this member is part of.
         */
        mutable std::mutex mutex_;

        /**
         * Signaled whenever disposed_ transitions to true (Task P7-2).
         * Lets a concurrent Dispose() call that lost ClaimDisposalOnce()
         * wait for the winning call's cleanup to actually finish, instead
         * of proceeding immediately — see WaitForDisposalToComplete().
         */
        std::condition_variable disposalFinishedCv_;

    protected:
        /**
         * @brief Event raised when TimeBetweenUpdates changes.
         *
         * Task SENSORBASE-007: this is a CNA-only extension, not real XNA/WP7
         * API — the real `SensorBase(TSensorReading)` class's own archived
         * MSDN reference page (`hh239315(v=vs.105)`) lists exactly one event,
         * `CurrentValueChanged`; no `TimeBetweenUpdatesChanged` or equivalent
         * exists (confirmed by a dedicated web search finding zero hits for
         * the exact member name anywhere). A prior doc comment here claimed
         * "In the original .NET version this event is protected" with no
         * citation — that claim was incorrect and has been removed. This
         * hook exists purely so `Compass`/`Motion`'s Android backend
         * (`ANDROID-BRIDGE-002`) can forward a live `TimeBetweenUpdates`
         * change to the running native sensor queue without requiring
         * `Stop()`/`Start()`; kept `protected` (not made `public`) since
         * only a derived sensor class, not game code, needs to subscribe to
         * it.
         */
        CNAEXT System::EventHandler<System::EventArgs> TimeBetweenUpdatesChanged;

        /**
         * @brief Gets whether this instance has already been disposed.
         *
         * Task P6-3: guarded by mutex_ (shared with currentValue_/
         * isDataValid_/isSupported_) since this is read from both the
         * game/user thread and, for real SDL3-backed sensors, the SDL
         * sensor event-watch thread (Accelerometer/Gyroscope's
         * ProcessSensorUpdateEvent()). Previously unguarded — a real data
         * race under the C++ memory model.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return disposed_;
        }

        /**
         * @brief Atomically claims this instance's one-time disposal cleanup (Task P6-3).
         *
         * Derived classes must call this instead of checking
         * getIsDisposedProperty() directly to decide whether to run their
         * own disposal cleanup (Stop(), releasing native resources,
         * decrementing a shared instance counter, etc.) — a plain "if
         * (!getIsDisposedProperty())" check-then-act is not atomic and lets
         * two threads calling Dispose() on the same instance concurrently
         * both pass the check and both run cleanup once each, e.g. both
         * decrementing a shared instance counter for what should be a
         * single logical disposal.
         *
         * Deliberately a separate flag from disposed_ (still set only by
         * Dispose(bool) itself, at its normal point in each derived
         * override): a derived Dispose(bool)'s own cleanup body may call
         * other public methods (e.g. Stop()) that themselves guard on
         * getIsDisposedProperty() being still false — claiming disposal
         * must not make the object appear fully disposed before its own
         * cleanup has actually run.
         *
         * Task P7-2: the caller that receives `false` back (the "loser" of
         * a concurrent Dispose() race) must not itself call the base
         * Dispose(bool) below — doing so would flip disposed_ to true while
         * the winning caller's own cleanup (which may itself call Stop(),
         * guarded by the same getIsDisposedProperty() precondition) is
         * still running, causing the winner's own Stop() call to observe
         * disposed_ == true and throw ObjectDisposedException mid-cleanup —
         * a real bug found by re-auditing this exact interaction (see
         * plan_devices_phase7.md's Audit finding B). The loser must instead
         * call WaitForDisposalToComplete() and then simply return.
         *
         * @return True if this call is the first to claim disposal (the
         * caller must run cleanup, then call the base Dispose(bool)); false
         * if another call already has, concurrently or previously (the
         * caller must call WaitForDisposalToComplete() instead).
         */
        bool ClaimDisposalOnce()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (disposalClaimed_)
            {
                return false;
            }
            disposalClaimed_ = true;
            return true;
        }

        /**
         * @brief Blocks until the winning concurrent Dispose() caller's
         * cleanup has actually completed (Task P7-2).
         *
         * Only meaningful for a caller that just received `false` from
         * ClaimDisposalOnce() — i.e. lost a race against another thread
         * concurrently calling Dispose() on this same instance. Waits for
         * disposed_ to become true (set by the base Dispose(bool) override,
         * called only by the winning caller once its own cleanup finishes)
         * rather than proceeding immediately, so a losing caller's
         * Dispose(bool) override returns only after the object is *actually*
         * fully disposed.
         *
         * Task LIFE-006 (2026-07-17): no longer assumes the winning caller's
         * cleanup path cannot throw — every derived class now constructs a
         * DisposalTerminalStateGuard immediately after winning
         * ClaimDisposalOnce(), which unconditionally publishes disposed_ =
         * true (and notifies the condition variable this waits on) even if
         * that cleanup throws, closing what was previously a documented,
         * accepted "a concurrent loser would wait here indefinitely" gap —
         * see that guard's own doc comment for the full design, including
         * the explicit decision that a losing caller's own Dispose(bool)
         * still simply returns (never observes or rethrows the winner's
         * specific failure) once this wait unblocks.
         */
        void WaitForDisposalToComplete()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            disposalFinishedCv_.wait(lock, [this] { return disposed_; });
        }

        /**
         * @brief RAII guard that unconditionally publishes the `disposed_`
         * terminal state, even if the derived class's own cleanup between
         * winning `ClaimDisposalOnce()` and reaching the base `Dispose(bool)`
         * call throws (Task LIFE-006, 2026-07-17, external audit
         * `audit_devices_2026-07-17.md`).
         *
         * Previously (see `WaitForDisposalToComplete()`'s own doc comment,
         * which already documented this as a known, accepted assumption
         * rather than something actually enforced): if the winning caller's
         * cleanup — `Stop()`, native resource release, instance-count
         * bookkeeping, whatever a derived class's `Dispose(bool)` override
         * does between claiming disposal and calling `SensorBase::Dispose(bool)`
         * below — threw, `disposed_` never became `true` and
         * `disposalFinishedCv_` never fired, stranding every concurrent
         * losing `Dispose()` caller (blocked in `WaitForDisposalToComplete()`)
         * forever.
         *
         * Construct this immediately after `ClaimDisposalOnce()` returns
         * `true`, before running any derived cleanup. Its destructor sets
         * `disposed_ = true` (if not already) and notifies
         * `disposalFinishedCv_` — idempotent with the normal-completion path
         * (the base `Dispose(bool)` override below already does the same
         * thing on success), so on a normal, non-throwing cleanup this
         * guard's own action is a harmless no-op; it only matters on the
         * exceptional path.
         *
         * **Explicit decision (required by this task): what a concurrent
         * losing `Dispose()` call does after the winner's cleanup fails.**
         * `WaitForDisposalToComplete()` unblocks once `disposed_` becomes
         * `true` (via this guard, whether the winner's cleanup succeeded or
         * threw) and the losing caller's own `Dispose(bool)` override then
         * simply returns, `void`, exactly as it already does today — it
         * does **not** observe, rethrow, or otherwise learn that the winner's
         * cleanup specifically failed. This matches `IDisposable.Dispose()`'s
         * established, conventional contract (never throw from `Dispose()`),
         * and this codebase has no other precedent for propagating one
         * thread's exception into an unrelated thread's call — inventing
         * cross-thread exception propagation for this one corner case, with
         * no WP7 reference behavior to justify a specific shape for it, was
         * judged unwarranted complexity. The *winning* caller's own
         * `Dispose(bool)` call still lets the original cleanup exception
         * propagate out normally (unchanged) — only the losing side's
         * behavior is being defined here.
         */
        class DisposalTerminalStateGuard
        {
        public:
            explicit DisposalTerminalStateGuard(SensorBase& owner)
                : owner_(owner)
            {
            }

            DisposalTerminalStateGuard(const DisposalTerminalStateGuard&) = delete;
            DisposalTerminalStateGuard& operator=(const DisposalTerminalStateGuard&) = delete;

            ~DisposalTerminalStateGuard()
            {
                try
                {
                    bool needsNotify = false;
                    {
                        std::lock_guard<std::mutex> lock(owner_.mutex_);
                        if (!owner_.disposed_)
                        {
                            owner_.disposed_ = true;
                            needsNotify = true;
                        }
                    }
                    if (needsNotify)
                    {
                        owner_.disposalFinishedCv_.notify_all();
                    }
                }
                catch (...)
                {
                    // Swallowed deliberately -- this destructor's own body
                    // is implicitly noexcept (a user-provided destructor
                    // with no explicit exception specification is
                    // noexcept(true) by default); letting anything escape
                    // (in practice, only std::mutex::lock() throwing
                    // std::system_error for a genuine OS-level failure)
                    // would call std::terminate() instead of achieving this
                    // guard's whole purpose (see Task ANDR2-004's identical
                    // reasoning for AndroidSensorBridge's own RunExitGuard).
                }
            }

        private:
            SensorBase& owner_;
        };

        /**
         * @brief Sets the current sensor reading and raises CurrentValueChanged.
         *
         * This mirrors the behavior of the .NET protected setter:
         * assigning CurrentValue updates the cached event args and raises the event.
         *
         * @param value New sensor reading.
         */
        void setCurrentValueProperty(const TSensorReading& value)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentValue_ = value;
            }

            if (!CurrentValueChanged.Empty())
            {
                // A local, per-dispatch event-args instance (Task P5-2) —
                // not a shared member — so a concurrent dispatch on another
                // thread can never mutate the args this call is still
                // raising with.
                SensorReadingEventArgs<TSensorReading> args(value);
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), args);
            }
        }

        /**
         * @brief Sets the current sensor reading and raises CurrentValueChanged.
         *
         * @param value New sensor reading to move.
         */
        void setCurrentValueProperty(TSensorReading&& value)
        {
            TSensorReading valueForEvent{};
            bool shouldRaise = false;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentValue_ = std::move(value);
                shouldRaise = !CurrentValueChanged.Empty();
                if (shouldRaise)
                {
                    // value is moved-from at this point; copy the
                    // now-authoritative currentValue_ instead, still under
                    // the lock, so the event gets the real new value.
                    valueForEvent = currentValue_;
                }
            }

            if (shouldRaise)
            {
                SensorReadingEventArgs<TSensorReading> args(std::move(valueForEvent));
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), args);
            }
        }

        /**
         * @brief Atomically stores a new reading and marks data valid, then raises CurrentValueChanged (Task BASE2-002).
         *
         * Every derived sensor class's own dispatch path previously called
         * `setIsDataValidProperty(true)` and `setCurrentValueProperty(value)`
         * as two independently-locked calls (`Accelerometer`/`Gyroscope`/
         * `Compass`/`Motion` all did this identically) — each call is itself
         * race-free (`docs/devices-thread-safety.md`'s existing guarantee),
         * but nothing prevented a concurrent reader on another thread from
         * observing the window *between* the two calls, where
         * `getIsDataValidProperty()` had already flipped to `true` while
         * `getCurrentValueProperty()` still returned the previous (or, for a
         * sensor's very first ever reading, still default-constructed)
         * value — an inconsistent, misleading snapshot a caller checking
         * "if valid, use CurrentValue" could genuinely observe. Combining
         * both assignments into one lock scope closes that window entirely:
         * a concurrent reader now always sees either the state from before
         * this call or the fully-updated state after it, never a mix of the
         * two. Derived classes should prefer this over the two separate
         * calls whenever a new reading is what makes data valid (the
         * overwhelmingly common case) — the separate `setIsDataValidProperty()`/
         * `setCurrentValueProperty()` setters remain available for the rarer
         * case of changing one without the other (e.g. explicitly
         * invalidating data with no new reading to publish).
         *
         * @param value New sensor reading; also becomes the new CurrentValue.
         */
        void SetCurrentValueAndMarkDataValid(const TSensorReading& value)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentValue_ = value;
                isDataValid_ = true;
            }

            if (!CurrentValueChanged.Empty())
            {
                // Same discipline as setCurrentValueProperty() above: a
                // local, per-dispatch event-args instance, never held under
                // mutex_ while raising (a subscriber's handler may
                // legitimately call back into this sensor).
                SensorReadingEventArgs<TSensorReading> args(value);
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), args);
            }
        }

        /**
         * @brief Sets whether the current sensor data is valid.
         *
         * @param value New validity flag.
         */
        void setIsDataValidProperty(bool value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            isDataValid_ = value;
        }

        /**
         * @brief Sets whether the underlying sensor hardware is supported on this device.
         *
         * Derived classes must call this once from their constructor with the result of
         * their own static IsSupported check, since SensorBase has no access to it.
         *
         * Task P6-3: guarded by mutex_ — previously written here without a
         * lock but read under mutex_ inside getCurrentValueProperty(), an
         * inconsistent locking discipline on the same field.
         *
         * @param value New support flag.
         */
        void setIsSupportedProperty(bool value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            isSupported_ = value;
        }

        /**
         * @brief Decides whether an incoming update at monotonic time `now`
         * should actually be accepted (dispatched), given the current
         * TimeBetweenUpdates interval and the last accepted update's time
         * (Task SENSORBASE-001/ACCEL-005/GYRO-004/SDL-SENSOR-002).
         *
         * The very first call after construction, or after
         * ResetUpdateThrottle(), always accepts (there is no prior update to
         * measure an interval against — matches Start() always delivering an
         * immediate first sample). A subsequent call accepts only if at
         * least `getTimeBetweenUpdatesProperty()` has elapsed since the last
         * accepted call's `now`; the current TimeBetweenUpdates value is read
         * fresh on every call, so a change while the sensor is running takes
         * effect on the very next incoming update without requiring
         * Stop()/Start(). A call that accepts records `now` as the new
         * last-accepted time; a call that does not accept leaves it
         * unchanged, so throttling is measured against the last
         * *dispatched* update, not the last *attempted* one.
         *
         * Deliberately takes `now` as a parameter rather than calling
         * `std::chrono::steady_clock::now()` internally: this keeps the
         * decision a pure function of its inputs, so it can be unit tested
         * with synthetic `time_point`s (no real-time sleeps, no test
         * flakiness) — the same technique this codebase already uses for
         * Android sensor math (Detail::ConvertAndroidPortraitToXnaLandscape(),
         * Detail::ExtractYawPitchRollFromQuaternion()). Production call
         * sites (Accelerometer::ProcessSensorUpdateEvent(),
         * Gyroscope::ProcessSensorUpdateEvent()) pass
         * `std::chrono::steady_clock::now()`. Scoped per sensor *instance*
         * (this method reads/writes only this instance's own fields) — two
         * instances with different TimeBetweenUpdates values throttle
         * independently, as required.
         *
         * Deliberately `std::chrono::steady_clock`, not `System::DateTimeOffset`
         * wall-clock time — see `lastAcceptedUpdateTime_`'s own doc comment
         * for why a throttle *decision* must use a clock the standard
         * guarantees never steps backward or jumps. `TimeBetweenUpdates`
         * itself stays a `System::TimeSpan` (matching the real WP7 API);
         * converted here via `getTicksProperty()` (100ns units, exact
         * integer conversion, no floating-point rounding) into a
         * `std::chrono` duration for the comparison.
         *
         * Not applied to the CNAEXT synthetic-injection test hooks
         * (InjectSyntheticSensorUpdate(), DispatchToInstancesForTesting())
         * on either class — those deliberately bypass ProcessSensorUpdateEvent()
         * entirely (see its own doc comment) so tests can exercise dispatch
         * behavior without depending on real elapsed time between calls.
         *
         * @param now Monotonic time of the incoming update.
         * @return true if this update should be dispatched; false if it
         * should be silently dropped (too soon after the last accepted one).
         */
        bool ShouldAcceptUpdateAt(const std::chrono::steady_clock::time_point& now)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
            const Ticks interval(timeBetweenUpdates_.getTicksProperty());

            // Task BASE2-001 (2026-07-17, external audit
            // `audit_devices_2026-07-17.md`): explicitly casts the *elapsed*
            // duration down to `interval`'s own (coarser, 100ns-tick) period
            // before comparing, rather than comparing the two durations
            // directly. A direct `(now - lastAcceptedUpdateTime_) >= interval`
            // comparison implicitly promotes both operands to their
            // std::chrono::common_type -- here, the *finer* of the two
            // periods (steady_clock's own, typically nanoseconds) -- which
            // converts `interval` by multiplying its count by 100. For
            // TimeSpan::MaxValue (its own tick count already near INT64_MAX),
            // that multiplication itself overflows a signed 64-bit integer --
            // confirmed by UBSan under devices-ubsan ("signed integer
            // overflow: 9223372036854775807 * 100..."), a real, previously
            // undetected bug, not a theoretical one. Casting the elapsed
            // duration down to ticks instead is always safe: an int64 count
            // of 100ns ticks can represent roughly 29,000 years, far beyond
            // any realistic elapsed wall-clock time, so this direction of
            // cast never approaches the same overflow.
            const Ticks elapsedTicks = std::chrono::duration_cast<Ticks>(now - lastAcceptedUpdateTime_);

            if (!hasAcceptedUpdate_ || elapsedTicks >= interval)
            {
                hasAcceptedUpdate_ = true;
                lastAcceptedUpdateTime_ = now;
                return true;
            }

            return false;
        }

        /**
         * @brief Forgets the last accepted-update time, so the next
         * ShouldAcceptUpdateAt() call always accepts.
         *
         * Derived classes call this from Start(), so a fresh Start() always
         * delivers an immediate first sample rather than staying throttled
         * by a stale timestamp from a previous Start()/Stop() cycle.
         */
        void ResetUpdateThrottle()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasAcceptedUpdate_ = false;
        }

        /**
         * @brief Derived classes override this method to dispose managed-like
         * and unmanaged/native resources.
         *
         * @param disposing true when called from Dispose(); false when called
         *        from the destructor path.
         *
         * @note This mirrors the .NET pattern, but C++ destructor behavior is
         * not identical to .NET finalization.
         *
         * @note Task P7-2: derived overrides must call this (the base
         * implementation) only from the winning side of a ClaimDisposalOnce()
         * race — i.e. only after that caller's own cleanup has fully run.
         * This notifies any concurrent loser blocked in
         * WaitForDisposalToComplete().
         */
        virtual void Dispose(bool disposing)
        {
            (void)disposing;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disposed_ = true;
            }
            disposalFinishedCv_.notify_all();
        }

    public:
        /**
         * @brief Event raised when CurrentValue changes.
         *
         * In the original .NET version this is a public event.
         */
        System::EventHandler<SensorReadingEventArgs<TSensorReading>> CurrentValueChanged;

        /**
         * @brief Initializes a new instance of the SensorBase class.
         *
         * The default TimeBetweenUpdates value matches the .NET source:
         * 2 milliseconds.
         */
        SensorBase()
            : disposed_(false),
              disposalClaimed_(false),
              isDataValid_(false),
              isSupported_(false),
              timeBetweenUpdates_(System::TimeSpan::Zero),
              currentValue_(),
              hasAcceptedUpdate_(false),
              lastAcceptedUpdateTime_()
        {
            setTimeBetweenUpdatesProperty(System::TimeSpan::FromMilliseconds(2.0));
        }

        /**
         * @brief Virtual destructor.
         *
         * This attempts to mirror the .NET finalizer path by calling Dispose(false)
         * if the object has not yet been disposed.
         *
         * @note Unlike .NET, virtual dispatch from destructors in C++ does not behave
         * the same way for derived classes.
         */
        virtual ~SensorBase()
        {
            if (!getIsDisposedProperty())
            {
                Dispose(false);
            }
        }

        /**
         * @brief Gets the current sensor reading.
         *
         * Returns a copy (Task P5-2) rather than a reference to internal
         * state, since that state can be concurrently overwritten from the
         * SDL sensor event-watch callback thread for real, SDL3-backed
         * sensors — a caller holding a reference into currentValue_ across
         * such a write would see a torn/inconsistent value. This also
         * matches the real WP7 API more closely: the C# CurrentValue
         * property returns a value-type reading, not a reference.
         *
         * @return Current sensor reading.
         *
         * @throws System::InvalidOperationException if the sensor is not supported on
         * this device. Use IsSupported to check before accessing this property.
         */
        [[nodiscard]] TSensorReading getCurrentValueProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!isSupported_)
            {
                throw System::InvalidOperationException(
                    "The sensor is not supported on this device. Check IsSupported before accessing CurrentValue.");
            }

            return currentValue_;
        }

        /**
         * @brief Gets whether the current sensor data is valid.
         *
         * @return true if valid; otherwise false.
         */
        [[nodiscard]] bool getIsDataValidProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return isDataValid_;
        }

        /**
         * @brief Gets the time interval between sensor updates.
         *
         * Task P8-2: returns a copy, not `const TimeSpan&`, and is guarded
         * by `mutex_` — previously read with no lock at all, the one
         * remaining field on this class with that gap (`currentValue_`/
         * `isDataValid_`/`isSupported_`/`disposed_` were all already fixed
         * across Tasks P5-2/P6-3). Returning by value rather than by
         * reference matches the precedent `getCurrentValueProperty()`
         * already set in Task P5-2 for the same reason: a caller holding a
         * reference into `timeBetweenUpdates_` across a concurrent write
         * from another thread would see a torn/inconsistent value, and the
         * real WP7 `TimeSpan` property is a value type in C# anyway, so
         * this is a more faithful match, not a breaking API change.
         *
         * @note Task SENSORBASE-001/ACCEL-005/GYRO-004: `Accelerometer` and
         * `Gyroscope` now honor this value as a real dispatch-rate throttle
         * via `ShouldAcceptUpdateAt()`, called from each class's own
         * `ProcessSensorUpdateEvent()` (the real platform event path only — the
         * CNAEXT synthetic-injection test hooks deliberately bypass it, see
         * `ShouldAcceptUpdateAt()`'s doc comment). The underlying desktop
         * sensor polling rate cannot be adjusted through the platform service, so
         * so throttling is software-side, dropping events that arrive too
         * soon after the last accepted one. `Compass`/`Motion`'s Android
         * backend (Task `ANDROID-BRIDGE-002`, closed) forwards a live change
         * to `Detail::AndroidSensorBridge::SetSampleInterval()`, which
         * re-applies `ASensorEventQueue_setEventRate()` on the already-running
         * queue — no longer applied only once at `Start()` time.
         *
         * @return Time between updates.
         */
        [[nodiscard]] System::TimeSpan getTimeBetweenUpdatesProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return timeBetweenUpdates_;
        }

        /**
         * @brief Sets the time interval between sensor updates.
         *
         * If the value changes, TimeBetweenUpdatesChanged is raised.
         *
         * Task P8-2: the compare-and-write is now guarded by `mutex_`, but
         * the lock is released before `TimeBetweenUpdatesChanged.Raise()` —
         * never held while raising an event, same discipline every other
         * event-raising setter on this class already follows, since a
         * subscriber's handler can legitimately call back into this sensor
         * and raising under a lock risks deadlock.
         *
         * @param value New update interval.
         */
        void setTimeBetweenUpdatesProperty(const System::TimeSpan& value)
        {
            bool changed = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                changed = timeBetweenUpdates_ != value;
                if (changed)
                {
                    timeBetweenUpdates_ = value;
                }
            }

            // Task VERIFY-003/DEV-API-002: this class's own internal use of its
            // own CNAEXT-tagged TimeBetweenUpdatesChanged is not the kind of
            // "leak into strict XNA API surface" the strict-mode check
            // (CNA_STRICT_XNA_API, CNAHelper.hpp) exists to catch — only an
            // *external* caller referencing a CNAEXT member is. Suppressed
            // here so this genuinely-real XNA method (setTimeBetweenUpdatesProperty())
            // stays callable from tools/devices/StrictXnaApiSurfaceCheck.cpp
            // without that check flagging this internal implementation detail.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            if (changed && !TimeBetweenUpdatesChanged.Empty())
            {
                System::EventArgs args;
                TimeBetweenUpdatesChanged.Raise(static_cast<System::Object*>(this), args);
            }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        }

        /**
         * @brief Releases the resources used by the current object.
         *
         * Mirrors the .NET Dispose() method. Calling Dispose() more than once
         * throws ObjectDisposedException, just like the decompiled source.
         *
         * @note Calling Dispose() concurrently from two different threads
         * on the *same* instance is not a fully supported usage pattern
         * (matching the conventional .NET IDisposable contract, which does
         * not generally require Dispose() itself to be thread-safe against
         * concurrent callers): the getIsDisposedProperty() check above and
         * the call into Dispose(true) below are not a single atomic
         * transaction, so both concurrent callers may reach Dispose(true)
         * without either seeing ObjectDisposedException here. Derived
         * classes' own cleanup is still safe in that race (Tasks P6-3/P7-2):
         * ClaimDisposalOnce() ensures only one of the two calls actually
         * runs the derived cleanup body (e.g. decrementing a shared
         * instance counter once, not twice); the losing call blocks in
         * WaitForDisposalToComplete() until the winner's cleanup has
         * genuinely finished (Task P7-2 — previously the loser proceeded
         * immediately and could flip disposed_ to true while the winner's
         * own cleanup, e.g. its internal Stop() call, was still relying on
         * disposed_ being false), then returns as a no-op rather than a
         * clean second-call exception.
         */
        void Dispose() override
        {
            if (getIsDisposedProperty())
            {
                throw System::ObjectDisposedException("SensorBase");
            }

            Dispose(true);
        }

        /**
         * @brief Starts the sensor.
         */
        virtual void Start() = 0;

        /**
         * @brief Stops the sensor.
         */
        virtual void Stop() = 0;
    };
} // namespace Microsoft::Devices::Sensors
