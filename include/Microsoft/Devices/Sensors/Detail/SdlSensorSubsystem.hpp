// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_sensor.h>

#include "Microsoft/Devices/Detail/SdlSubsystemMutex.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Minimal RAII scope-exit guard (Task P6-4).
     *
     * Runs a callable on destruction, including during exception
     * unwinding — used by SensorEventWatch() so a dispatched call's
     * dispatchingThreadIds_ cleanup still runs even if that call throws
     * (previously a plain post-call statement, skipped entirely if
     * ProcessSensorUpdateEvent() — or transitively a user's
     * CurrentValueChanged/ReadingChanged handler — threw, permanently
     * corrupting dispatchingThreadIds_ and deadlocking any current or
     * future Dispose() call on that instance).
     *
     * Task P7-5: the destructor is `noexcept` and swallows any exception
     * the cleanup callable throws — a scope guard's destructor runs during
     * stack unwinding as often as not (that is the whole point of this
     * class), and a second exception escaping a destructor while one is
     * already propagating calls `std::terminate()`. Every current use of
     * this class (SdlSensorSubsystem's own dispatch cleanup) is a plain
     * lock+erase+notify sequence that does not throw in practice, but this
     * class is a general-purpose utility, not tied to that one call site —
     * it must be safe on its own terms.
     */
    template <typename F>
    class ScopeExit
    {
    public:
        explicit ScopeExit(F onExit)
            : onExit_(std::move(onExit))
        {
        }

        ~ScopeExit() noexcept
        {
            try
            {
                onExit_();
            }
            catch (...)
            {
                // Swallowed deliberately — see this class's doc comment:
                // throwing out of a destructor during unwinding would call
                // std::terminate().
            }
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

    private:
        F onExit_;
    };

    /** @brief Deduces F so callers can write `auto guard = MakeScopeExit([]{ ... });`. */
    template <typename F>
    ScopeExit<F> MakeScopeExit(F onExit)
    {
        return ScopeExit<F>(std::move(onExit));
    }

    /**
     * @brief Process-wide mutex serializing every real SDL_INIT_SENSOR-related
     * SDL API call across *every* sensor class (Task P7-1).
     *
     * `Accelerometer` and `Gyroscope` each own their own
     * `Detail::SdlSensorSubsystem<TSensor>` instance (Task P5-4) and therefore
     * their own, distinct `mutex_` — correct for serializing each class's own
     * per-class shared state (instanceCount_, sensor_, startedInstances_,
     * ...), but insufficient for the actual real SDL sensor-subsystem calls
     * (SDL_InitSubSystem/SDL_QuitSubSystem/SDL_GetSensors/SDL_OpenSensor/
     * SDL_CloseSensor/SDL_GetSensorType), which touch SDL's *one* global
     * SDL_INIT_SENSOR subsystem — a class-specific lock does nothing to stop
     * Accelerometer's and Gyroscope's real SDL calls from running fully
     * concurrently with each other. SDL3's own documentation states
     * SDL_InitSubSystem() "should only be called on the main thread" and
     * SDL_QuitSubSystem() "is not thread safe"; SDL_GetSensors()/
     * SDL_OpenSensor()/SDL_GetSensorType()/SDL_CloseSensor() carry no
     * `\threadsafety` annotation at all, so none of them can be assumed safe
     * for concurrent access either. This single mutex serializes all of them
     * across both sensor classes.
     *
     * Task SDLCORE-001 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
     * this is now a thin forwarding call to
     * `Detail::GetGlobalSdlSubsystemMutex()` (`include/Microsoft/Devices/
     * Detail/SdlSubsystemMutex.hpp`) — the *same* process-wide mutex
     * `Detail::SdlHapticVibrateBackend` (`SDL_INIT_HAPTIC`, used by
     * `VibrateController`) now also uses, instead of each having its own,
     * entirely independent lock. Kept under this existing name (rather than
     * updating every call site in `Accelerometer.cpp`/`Gyroscope.cpp` to a
     * new name) purely to minimize diff — see that header's own doc comment
     * for the full rationale, including why this remains a plain mutex
     * rather than the audit's originally-suggested `SDL_RunOnMainThread()`-
     * based main-thread dispatch (a naive version of that redesign would
     * deadlock this project's own existing, legitimate multi-threaded
     * `Start()`/`Stop()` usage, since nothing in this project's Devices
     * usage guarantees an active SDL event pump to drain it).
     *
     * Lock order (Task P7-1): whenever a caller already holds a
     * `SdlSensorSubsystem<TSensor>::mutex_`, it must acquire *this* mutex
     * only after that lock (nest this mutex inside the per-class one), never
     * the reverse — see `Accelerometer::getIsSupportedProperty()`/`Start()`/
     * `Dispose(bool)` for the exact acquisition sites. `getIsSupportedProperty()`
     * acquires only this mutex (no per-class lock at all — `ProbeIsSupported()`
     * touches no per-class subsystem state), so it never participates in the
     * nesting order at all; this is safe precisely because it never also
     * holds a per-class `mutex_` while doing so.
     */
    inline std::mutex& GetGlobalSdlSensorMutex()
    {
        return Microsoft::Devices::Detail::GetGlobalSdlSubsystemMutex();
    }

    /**
     * Internal implementation detail (Task P5-4) — never included by a
     * public CNA header, not part of any XNA/WP7-facing API surface. Owns
     * the SDL_INIT_SENSOR subsystem lifetime, default-sensor discovery,
     * and event-watch registration/dispatch that Accelerometer and
     * Gyroscope used to each hand-write as near-identical copies.
     *
     * One instantiation per concrete sensor type: each of Accelerometer
     * and Gyroscope owns its own subsystem instance, reached via its own
     * `static Detail::SdlSensorSubsystem<TSensor>& GetSubsystem();`
     * accessor (defined in that class's own .cpp as a function-local
     * static — same "keep SDL types out of the public header" discipline
     * this project already used for `void* g_sensor_`) — never a single
     * instance shared between the two classes. SDL's own internal
     * SDL_INIT_SENSOR ref-counting (see EnsureSubsystemInitialized()) is
     * what correctly aggregates the two classes' independent holds on the
     * one real subsystem, exactly as it did before this refactor
     * (Task P4-8).
     *
     * TSensor must:
     *   - declare `friend class Detail::SdlSensorSubsystem<TSensor>;` so
     *     this class can reach its private members;
     *   - provide `static SDL_SensorType GetSdlSensorType();` (defined in
     *     TSensor's own .cpp, where SDL headers are already included —
     *     keeps SDL_SensorType out of TSensor's public header);
     *   - provide `static SdlSensorSubsystem<TSensor>& GetSubsystem();`
     *     (same reasoning);
     *   - provide a private `void ProcessSensorUpdateEvent(std::int64_t
     *     sensorId, float x, float y, float z)` method;
     *   - provide private `bool subsystemHeld_` and
     *     `std::shared_ptr<std::vector<std::thread::id>> dispatchToken_`
     *     members (Tasks P4-8/P5-3, refactored to a shared_ptr in Task P8-1)
     *     — these remain genuinely per-*instance* data on TSensor itself,
     *     not here; `dispatchToken_` must be created once (e.g.
     *     `std::make_shared<std::vector<std::thread::id>>()`) in TSensor's
     *     constructor and never replaced afterward.
     */
    template <typename TSensor>
    class SdlSensorSubsystem
    {
    public:
        /**
         * @brief Stable, ABA-safe dispatch registration node (Task SDLCORE-004).
         *
         * Previously `startedInstances_` held raw `TSensor*` values directly,
         * and `DispatchToInstances()` revalidated a snapshotted pointer by
         * checking whether that exact bit pattern was still present in the
         * *live* `startedInstances_` list. That check cannot distinguish "the
         * original instance this snapshot was taken for is still started"
         * from "a completely different, later TSensor instance happens to
         * have been allocated at the exact same address and is itself now
         * started" — the classic ABA hazard. Concretely: instance X is
         * snapshotted, then disposed and destroyed (freeing its memory)
         * *before* the dispatch loop reaches it; a brand-new instance Y is
         * then constructed at X's freed address and started, registering
         * that same address in `startedInstances_` again; the old
         * pointer-value revalidation would wrongly find "the address" still
         * present and dispatch a stale event — meant for whatever happened
         * while X was alive — into Y instead.
         *
         * The fix: a fresh `DispatchRegistration` is heap-allocated (via
         * `shared_ptr`) every time `RegisterStartedInstanceLocked()` runs,
         * and is never reused for a later, logically distinct registration —
         * not even a later `Start()` by the very same `TSensor` object after
         * an intervening `Stop()`. `startedInstances_` holds (and
         * `DispatchToInstances()` snapshots/iterates) these `shared_ptr`s,
         * never raw `TSensor*` values. `owner` is nulled by
         * `UnregisterStartedInstanceLocked()` — under `mutex_`, the same lock
         * every read of `owner` requires — strictly before that instance's
         * `Dispose(bool)` can finish waiting for in-flight dispatches and let
         * the object be destroyed (see `Accelerometer`/`Gyroscope`'s
         * `Dispose(bool)`). A dispatcher holding an old snapshot's
         * `shared_ptr<DispatchRegistration>` therefore always sees `owner ==
         * nullptr` once that specific registration is torn down, regardless
         * of whether some later, unrelated instance is later constructed at
         * the same address and separately registers its own, distinct
         * `DispatchRegistration` node — the two are never the same object, so
         * they can never be confused with each other.
         *
         * `owner` is guarded by the *subsystem's* `mutex_` (not a mutex of
         * its own) — every access to it already happens only while `mutex_`
         * is held (`RegisterStartedInstanceLocked()`,
         * `UnregisterStartedInstanceLocked()`, `DispatchToInstances()`),
         * exactly like every other field this class documents as
         * "Caller must already hold mutex_"; a dedicated per-registration
         * mutex would add nothing here.
         */
        struct DispatchRegistration
        {
            /**
             * @brief The instance this registration was created for, or
             * `nullptr` once invalidated. Guarded by the owning
             * `SdlSensorSubsystem::mutex_` — never read or written without
             * it held.
             */
            TSensor* owner = nullptr;
        };

        /**
         * RAII probe guard (Task P5-1): balances a probe-only
         * SDL_InitSubSystem()/SDL_QuitSubSystem() pair, independent of
         * whatever any live TSensor instance separately holds via its own
         * subsystemHeld_ — SDL's own ref-counting aggregates both
         * correctly, so no shared state needs to be tracked here.
         */
        class ProbeGuard
        {
        public:
            ProbeGuard()
                : initialized_(SDL_InitSubSystem(SDL_INIT_SENSOR))
            {
            }

            ~ProbeGuard()
            {
                if (initialized_)
                {
                    SDL_QuitSubSystem(SDL_INIT_SENSOR);
                }
            }

            ProbeGuard(const ProbeGuard&) = delete;
            ProbeGuard& operator=(const ProbeGuard&) = delete;

            [[nodiscard]] bool IsInitialized() const
            {
                return initialized_;
            }

        private:
            bool initialized_;
        };

        /**
         * @brief Always calls through to a real SDL_InitSubSystem(SDL_INIT_SENSOR).
         *
         * Callers are responsible for pairing each successful call with
         * exactly one SDL_QuitSubSystem() (see TSensor::subsystemHeld_
         * for the per-instance pairing, or ProbeGuard for the probe-only
         * pairing) — see Task P4-8's original rationale for why this must
         * not bypass SDL's own ref-counting via SDL_WasInit().
         *
         * Task P8-3: takes a `const std::lock_guard<std::mutex>&` referencing
         * `GetGlobalSdlSensorMutex()` as a required parameter — not used for
         * anything except as a compile-time "you must already hold this
         * lock" proof. A plain doc-comment precondition (what this method
         * relied on before this task) is easy to miss when adding a new call
         * site; a required parameter makes it structurally awkward to call
         * this without a lock visibly in hand, closing off the exact class
         * of mistake Task P6-1's addendum and Task P7-1 both found and
         * fixed the hard way. This does not prove the passed lock_guard
         * actually references `GetGlobalSdlSensorMutex()` specifically (C++
         * has no such "prove this exact mutex" mechanism without a bespoke
         * tag type, which would be more machinery than this internal-only
         * class warrants) — see `GetGlobalSdlSensorMutex()`'s own doc
         * comment for the lock-order rule this participates in.
         */
        static bool EnsureSubsystemInitialized(const std::lock_guard<std::mutex>& /*globalSdlSensorMutexHeld*/)
        {
            return SDL_InitSubSystem(SDL_INIT_SENSOR);
        }

        /**
         * Opens (if not already open) the first SDL sensor matching
         * TSensor::GetSdlSensorType() and caches its id. Returns the
         * shared handle. Caller must already hold mutex_ (documented, not
         * enforced — this method's own per-instance data, not a real SDL
         * call misuse risk the way the global SDL sensor mutex is; see
         * Task P8-3's rationale on EnsureSubsystemInitialized() above for
         * why *that* lock specifically is enforced via a required
         * parameter here too).
         */
        SDL_Sensor* OpenDefaultSensorLocked(const std::lock_guard<std::mutex>& /*globalSdlSensorMutexHeld*/)
        {
            if (sensor_ != nullptr)
            {
                return sensor_;
            }

            int sensorCount = 0;
            SDL_SensorID* sensors = SDL_GetSensors(&sensorCount);

            if (sensors == nullptr || sensorCount <= 0)
            {
                if (sensors != nullptr)
                {
                    SDL_free(sensors);
                }
                return nullptr;
            }

            SDL_Sensor* openedSensor = nullptr;
            SDL_SensorID openedSensorId = 0;

            for (int i = 0; i < sensorCount; ++i)
            {
                const SDL_SensorID sensorId = sensors[i];

                SDL_Sensor* sensor = SDL_OpenSensor(sensorId);
                if (!sensor)
                {
                    continue;
                }

                if (SDL_GetSensorType(sensor) == TSensor::GetSdlSensorType())
                {
                    openedSensor = sensor;
                    openedSensorId = sensorId;
                    break;
                }

                SDL_CloseSensor(sensor);
            }

            SDL_free(sensors);

            if (openedSensor != nullptr)
            {
                sensor_ = openedSensor;
                sensorId_ = static_cast<std::int64_t>(openedSensorId);
            }

            return openedSensor;
        }

        /**
         * @brief Probes whether a sensor of TSensor::GetSdlSensorType()
         * exists, without holding the subsystem or any device open
         * afterward (ProbeGuard handles the subsystem side, Task P5-1;
         * this closes every device it opens to check).
         *
         * Task P8-3: requires proof the global SDL sensor mutex is held —
         * see EnsureSubsystemInitialized()'s doc comment for the full
         * rationale. Unlike that method, this one needs *only* the global
         * lock (no per-class `mutex_` at all — this touches no per-class
         * subsystem state), which is exactly why it was easy to forget in
         * the first place (Task P6-1's addendum).
         */
        static bool ProbeIsSupported(const std::lock_guard<std::mutex>& /*globalSdlSensorMutexHeld*/)
        {
            const ProbeGuard subsystemGuard;
            if (!subsystemGuard.IsInitialized())
            {
                return false;
            }

            int sensorCount = 0;
            SDL_SensorID* sensors = SDL_GetSensors(&sensorCount);

            if (sensors == nullptr || sensorCount <= 0)
            {
                if (sensors != nullptr)
                {
                    SDL_free(sensors);
                }
                return false;
            }

            bool supported = false;

            for (int i = 0; i < sensorCount; ++i)
            {
                SDL_Sensor* sensor = SDL_OpenSensor(sensors[i]);
                if (!sensor)
                {
                    continue;
                }

                if (SDL_GetSensorType(sensor) == TSensor::GetSdlSensorType())
                {
                    supported = true;
                    SDL_CloseSensor(sensor);
                    break;
                }

                SDL_CloseSensor(sensor);
            }

            SDL_free(sensors);
            return supported;
        }

        /**
         * @brief Registers the SDL event watch if not already registered. Caller must already hold mutex_.
         *
         * Task SDLCORE-003 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
         * `SDL_AddEventWatch()` returns `bool` and its result was previously
         * ignored unconditionally, marking the watch registered even on
         * failure — a later real sensor event would then never reach
         * `SensorEventWatch()` (it was never actually installed), while
         * every instance that had already committed to `Ready`/`started_`
         * stayed in that state indefinitely, and a later
         * `UnregisterEventWatchIfNeededLocked()` would call
         * `SDL_RemoveEventWatch()` on a watch that was never installed.
         * Now checks the return value and reports failure to the caller so
         * `Start()` can roll back to a documented failure instead of a
         * silent, permanently-broken "Ready" state.
         *
         * @return true if the watch is registered (already was, or just
         * succeeded); false if registration was attempted and failed (see
         * lastEventWatchError_ for SDL's own error string in that case).
         */
        [[nodiscard]] bool RegisterEventWatchIfNeededLocked()
        {
            if (eventWatchRegistered_)
            {
                return true;
            }

            // Task SDLCORE-003/TEST2-001: the real SDL_AddEventWatch() call
            // below offers no way to force it to fail on demand, so a host
            // test cannot otherwise deterministically exercise Start()'s
            // rollback path. Caller must already hold mutex_ (same as this
            // whole method).
            if (forceEventWatchRegistrationFailureForTesting_)
            {
                lastEventWatchError_ = "forced failure (SetEventWatchRegistrationFailureForTesting)";
                return false;
            }

            if (!SDL_AddEventWatch(&SdlSensorSubsystem::SensorEventWatch, nullptr))
            {
                // Task SDLCORE-003: capture SDL's own error string
                // immediately -- SDL_GetError() reports the *last* SDL
                // error on the calling thread, which a later, unrelated SDL
                // call could otherwise overwrite before Start() gets a
                // chance to read it.
                const char* sdlError = SDL_GetError();
                lastEventWatchError_ = (sdlError != nullptr) ? sdlError : "unknown SDL error";
                return false;
            }

            eventWatchRegistered_ = true;
            return true;
        }

        /** @brief SDL's own error string from the most recent failed RegisterEventWatchIfNeededLocked() call (Task SDLCORE-003). Guarded by mutex_. */
        std::string lastEventWatchError_;

        /**
         * @brief Test-only hook (Task SDLCORE-003): forces the next
         * RegisterEventWatchIfNeededLocked() call to report failure without
         * attempting the real SDL_AddEventWatch() call at all. Guarded by
         * mutex_, same as every other field on this class. A process-wide
         * flag (this subsystem instance is itself process-wide, one per
         * TSensor) — tests that set this must reset it to false afterward,
         * even on failure, so it cannot leak into a later, unrelated test.
         */
        bool forceEventWatchRegistrationFailureForTesting_ = false;

        /**
         * @brief Test-only hook (Task SDLCORE-009): message from the most recent exception `DispatchToInstances()` swallowed.
         *
         * Empty until the first swallowed exception. `SDL_Log()` (debug
         * builds only) already reports every swallowed exception for
         * interactive/manual observability, but that cannot be asserted on
         * from a test — this field is what
         * `Get*LastDispatchExceptionMessageForTesting()`
         * (`Accelerometer`/`Gyroscope`'s own static wrappers) actually
         * checks. Guarded by mutex_, same as every other field here. A
         * process-wide value (this subsystem instance is itself
         * process-wide) — tests must compare against
         * `dispatchExceptionCountForTesting_`'s value from *before* their
         * own action, not assume this starts empty, since an earlier,
         * unrelated test may have already set it.
         */
        std::string lastDispatchExceptionMessageForTesting_;

        /** @brief Test-only hook (Task SDLCORE-009): total exceptions `DispatchToInstances()` has ever swallowed. Guarded by mutex_. Process-wide, never reset — see `lastDispatchExceptionMessageForTesting_`'s own doc comment. */
        int dispatchExceptionCountForTesting_ = 0;

        /** @brief Unregisters the SDL event watch if no instances remain started. Caller must already hold mutex_. */
        void UnregisterEventWatchIfNeededLocked()
        {
            if (eventWatchRegistered_ && startedInstances_.empty())
            {
                SDL_RemoveEventWatch(&SdlSensorSubsystem::SensorEventWatch, nullptr);
                eventWatchRegistered_ = false;
            }
        }

        /**
         * @brief Creates and adds a fresh DispatchRegistration for instance,
         * unless one is already active. Caller must already hold mutex_.
         *
         * Task SDLCORE-004: always heap-allocates a brand-new
         * `DispatchRegistration` for a genuinely new registration — never
         * reuses or mutates a previous, already-invalidated one, even for
         * the same `instance` restarting after a prior `Stop()`. This
         * per-registration identity (not `instance`'s own address) is what
         * `DispatchToInstances()` revalidates against, closing the ABA
         * hazard described on `DispatchRegistration`'s own doc comment.
         */
        void RegisterStartedInstanceLocked(TSensor* instance)
        {
            const bool alreadyActive = std::any_of(
                startedInstances_.begin(), startedInstances_.end(),
                [instance](const std::shared_ptr<DispatchRegistration>& registration)
                {
                    return registration->owner == instance;
                });

            if (!alreadyActive)
            {
                auto registration = std::make_shared<DispatchRegistration>();
                registration->owner = instance;
                startedInstances_.push_back(std::move(registration));
            }
        }

        /**
         * @brief Invalidates and removes instance's active DispatchRegistration, if any. Caller must already hold mutex_.
         *
         * Task SDLCORE-004: nulls the registration's `owner` *before*
         * erasing it from `startedInstances_` (both under the already-held
         * `mutex_`) — a dispatcher holding its own copy of this exact
         * `shared_ptr<DispatchRegistration>` from an earlier snapshot sees
         * `owner == nullptr` the next time it locks `mutex_` to check,
         * regardless of whether this entry has already been erased from the
         * live list by then.
         */
        void UnregisterStartedInstanceLocked(TSensor* instance)
        {
            const auto it = std::find_if(
                startedInstances_.begin(), startedInstances_.end(),
                [instance](const std::shared_ptr<DispatchRegistration>& registration)
                {
                    return registration->owner == instance;
                });

            if (it != startedInstances_.end())
            {
                (*it)->owner = nullptr;
                startedInstances_.erase(it);
            }
        }

        // Class-level shared state (Task P5-4 — migrated from each of
        // Accelerometer's/Gyroscope's own identical static members).
        // Public: TSensor's own methods (Start()/Stop()/Dispose()/
        // getIsSupportedProperty()) read/write these directly, already
        // holding mutex_ per this class's own locking discipline (mirrors
        // the pre-refactor code exactly).
        SDL_Sensor* sensor_ = nullptr;
        std::int64_t sensorId_ = 0;
        int instanceCount_ = 0;
        bool eventWatchRegistered_ = false;
        // Task SDLCORE-004: holds DispatchRegistration nodes, not raw
        // TSensor* values — see that struct's own doc comment.
        std::vector<std::shared_ptr<DispatchRegistration>> startedInstances_;
        std::mutex mutex_;
        std::condition_variable callbackFinished_;

        /**
         * @brief Dispatches to each registration in `registrationsSnapshot`,
         * in order, via `dispatchOne` (Task P7-3, revised Task SDLCORE-004).
         *
         * Shared by the real SDL_EventFilter path (SensorEventWatch(),
         * below) and the NOXNA test-only dispatch hooks that TSensor
         * exposes — both funnel through this one method so the bookkeeping
         * can never diverge between the real path and a test simulating it.
         *
         * For each snapshotted `DispatchRegistration`, re-validates its
         * `owner` field — under `mutex_`, never dereferencing the instance
         * before this check passes — and only if still non-null, marks it as
         * actively dispatching (pushes this thread's id into its
         * dispatchToken_) and calls `dispatchOne(instance)`.
         *
         * This closes a real use-after-free (Task P7-3's audit finding C):
         * previously, *every* snapshotted instance had this thread's id
         * pushed into dispatchingThreadIds_ up front, before any of them
         * were dispatched to. If one instance's callback (e.g. instance A's
         * ProcessSensorUpdateEvent(), still mid-loop below) disposed a
         * *different*, not-yet-reached instance B from the same snapshot,
         * B's pre-pushed thread-id entry made its concurrent Dispose() call
         * look like a same-thread self-dispose (exempt from waiting) even
         * though nothing was actually executing inside B yet — B was fully
         * disposed (and possibly destroyed) with no wait at all, and this
         * loop would then blindly call `dispatchOne` on the now-dangling
         * `B*` on its next iteration.
         *
         * The fix: mark (and validate) each registration immediately before
         * dispatching to its instance, not in bulk up front. If a prior
         * iteration's callback already disposed a later instance in this
         * snapshot, that instance's own Dispose(bool)/Stop() has already
         * invalidated (and removed) its registration (necessarily before any
         * wait that could let it be destroyed) — so the `owner` re-check
         * here safely detects that and skips it, never touching
         * (dereferencing) the possibly-freed memory.
         *
         * Task SDLCORE-004: this per-registration `owner` re-check replaces
         * the previous pointer-value-only `std::find(startedInstances_...,
         * instance)` revalidation, which could not distinguish "the original
         * snapshotted instance is still started" from "a different, later
         * instance happens to have been allocated at the exact same address
         * and is itself now started" (a classic ABA hazard) — see
         * `DispatchRegistration`'s own doc comment for the full analysis.
         *
         * Task P8-1: the cleanup guard below still captures a *copy* of
         * `instance->dispatchToken_` (a `shared_ptr`), taken while `instance`
         * is confirmed still alive+started under the lock, instead of
         * capturing `instance` itself. This closes a further use-after-free:
         * if a callback *destroys* (not just Dispose()s) the exact instance
         * it was invoked for — `dispatchOne(instance)`, below — the cleanup
         * guard would previously dereference `instance->dispatchingThreadIds_`
         * on freed memory the moment it ran. The token, kept alive by the
         * guard's own captured shared_ptr, survives even if the instance does
         * not. This does not, and cannot, protect a TSensor-specific method
         * that itself keeps touching `this` after raising an event on the
         * same dispatch (e.g. Accelerometer::DispatchSensorReading() checking
         * `getIsDataValidProperty()` again before conditionally raising
         * ReadingChanged) — that is a class-design property, documented as
         * an explicit unsupported boundary, not a dispatch-bookkeeping gap
         * this method can close. See plan_devices_phase8.md Task P8-1.
         */
        template <typename DispatchFn>
        void DispatchToInstances(
            const std::vector<std::shared_ptr<DispatchRegistration>>& registrationsSnapshot,
            DispatchFn&& dispatchOne)
        {
            const std::thread::id thisThreadId = std::this_thread::get_id();

            for (const std::shared_ptr<DispatchRegistration>& registration : registrationsSnapshot)
            {
                TSensor* instance = nullptr;
                std::shared_ptr<std::vector<std::thread::id>> token;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    instance = registration->owner;
                    if (instance != nullptr)
                    {
                        token = instance->dispatchToken_;
                        token->push_back(thisThreadId);
                    }
                }

                if (!token)
                {
                    continue;
                }

                auto cleanupGuard = MakeScopeExit([this, token, thisThreadId]()
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto& ids = *token;
                        const auto it = std::find(ids.begin(), ids.end(), thisThreadId);
                        if (it != ids.end())
                        {
                            ids.erase(it);
                        }
                    }
                    callbackFinished_.notify_all();
                });

                try
                {
                    dispatchOne(instance);
                }
                catch (const std::exception& ex)
                {
                    // Task SDLCORE-009 (2026-07-17, external audit
                    // `audit_devices_2026-07-17.md`): still swallowed, for
                    // the same reason the plain `catch (...)` below always
                    // has (see that block's own comment), but no longer
                    // silent -- previously a throwing callback left zero
                    // trace anywhere. Chosen policy: log-and-continue. The
                    // other policies this task's required work names were
                    // considered and rejected: propagate-to-owner-error
                    // would need a new error-reporting surface on every
                    // TSensor class -- a much larger XNA-compatibility-surface
                    // change than this task's own scope; unsubscribe-failing-handler
                    // would silently stop delivering readings to an instance
                    // after a single bad callback, changing observable
                    // behavior for what may be a one-off, transient bug in
                    // caller code; terminate would crash the whole process
                    // over one user callback's exception, strictly worse
                    // than the existing safe swallow. Full routing through a
                    // structured diagnostic channel (this task's own
                    // "report through the diagnostic channel" bullet) is
                    // deferred to `DEVPERF-005` (not yet built) -- until
                    // then, `SDL_Log()` (interactive/manual observability)
                    // plus this test-only counter/message pair (automated
                    // observability) are what this task delivers.
                    LogAndRecordDispatchException(ex.what());
                }
                catch (...)
                {
                    // Swallowed deliberately — see SensorEventWatch()'s doc
                    // comment: the real path is an SDL_EventFilter callback
                    // invoked directly by SDL_PushEvent(), a C API that does
                    // not expect a C++ exception to unwind through its own
                    // call frames, and swallowing it here also lets the
                    // remaining snapshotted instances still get dispatched
                    // to and cleaned up.
                    LogAndRecordDispatchException("non-std::exception value");
                }
            }
        }

    private:
        /**
         * @brief Reports a swallowed dispatch-callback exception (Task SDLCORE-009).
         *
         * `SDL_Log()` for interactive/manual observability (debug builds
         * only, matching this codebase's established convention elsewhere)
         * plus `lastDispatchExceptionMessageForTesting_`/
         * `dispatchExceptionCountForTesting_` for automated test
         * observability — see those fields' own doc comments.
         *
         * @param message Human-readable description of what was thrown.
         */
        void LogAndRecordDispatchException(const std::string& message)
        {
#ifndef NDEBUG
            SDL_Log("SdlSensorSubsystem: sensor callback threw: %s", message.c_str());
#endif
            std::lock_guard<std::mutex> lock(mutex_);
            lastDispatchExceptionMessageForTesting_ = message;
            ++dispatchExceptionCountForTesting_;
        }

        /**
         * SDL_EventFilter trampoline for TSensor's event watch. Takes a
         * stable snapshot of startedInstances_ (pointer values only) under
         * mutex_, then hands it to DispatchToInstances() above, which does
         * the actual per-instance re-validation, marking, dispatch, and
         * cleanup — see that method's doc comment for the full Task P7-3
         * rationale. Dispatch itself always runs outside the lock (so a
         * handler re-entering Start()/Stop()/Dispose() doesn't
         * self-deadlock on mutex_).
         *
         * Task SDLCORE-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
         * declared with the *exact* `SDL_EventFilter` signature and calling
         * convention — `bool (SDLCALL *)(void*, SDL_Event*)`
         * (`third_party/SDL/include/SDL3/SDL_events.h`) — including the
         * `SDLCALL` tag SDL's own documentation explicitly asks every
         * callback to carry ("very important... mismatched calling
         * conventions can cause strange behaviors"). Previously declared
         * as `bool(void*, void*)` and forced through `reinterpret_cast` at
         * both `SDL_AddEventWatch()`/`SDL_RemoveEventWatch()` call sites —
         * calling a function through an incompatible function-pointer type
         * is undefined behavior in C++ even when both pointer types are
         * pointer-sized, and the cast silently hid any calling-convention
         * mismatch on a platform where `SDLCALL` expands to something
         * other than the default (`__cdecl` on 32-bit Windows/x86, a no-op
         * everywhere this project currently builds and tests). Matching
         * the type exactly lets `&SdlSensorSubsystem::SensorEventWatch` be
         * passed directly to `SDL_AddEventWatch()`/`SDL_RemoveEventWatch()`
         * with no cast at all — if SDL's own `SDL_EventFilter` signature
         * ever changes, this call site fails to compile instead of
         * silently reinterpreting an incompatible type.
         */
        static bool SDLCALL SensorEventWatch(void* userdata, SDL_Event* event)
        {
            (void)userdata;

            if (event == nullptr)
            {
                return true;
            }

            if (event->type != SDL_EVENT_SENSOR_UPDATE)
            {
                return true;
            }

            SdlSensorSubsystem& subsystem = TSensor::GetSubsystem();
            const std::int64_t sensorId = static_cast<std::int64_t>(event->sensor.which);
            const float x = event->sensor.data[0];
            const float y = event->sensor.data[1];
            const float z = event->sensor.data[2];

            std::vector<std::shared_ptr<DispatchRegistration>> registrationsSnapshot;
            {
                std::lock_guard<std::mutex> lock(subsystem.mutex_);
                registrationsSnapshot = subsystem.startedInstances_;
            }

            subsystem.DispatchToInstances(registrationsSnapshot, [sensorId, x, y, z](TSensor* instance)
            {
                instance->ProcessSensorUpdateEvent(sensorId, x, y, z);
            });

            return true;
        }

        // Task SDLCORE-002: a standalone, explicit compile-time proof (in
        // addition to the direct, cast-free assignment at both
        // SDL_AddEventWatch()/SDL_RemoveEventWatch() call sites above,
        // which already fails to compile on any signature mismatch) that
        // SensorEventWatch's type is *exactly* SDL_EventFilter, including
        // calling convention -- gives a clearer diagnostic pointing
        // directly at this line if a future SDL header update ever changes
        // SDL_EventFilter's signature, rather than an overload-resolution
        // error at a call site far from the actual mismatch.
        static_assert(
            std::is_same_v<decltype(&SdlSensorSubsystem::SensorEventWatch), SDL_EventFilter>,
            "SdlSensorSubsystem::SensorEventWatch must exactly match SDL_EventFilter's signature and calling convention");
    };
} // namespace Microsoft::Devices::Sensors::Detail
