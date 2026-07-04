// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_sensor.h>

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
     * for concurrent access either. This single mutex, function-local static
     * inside an `inline` function (one instance for the whole process, per
     * ordinary C++ inline-function singleton rules), serializes all of them
     * across both sensor classes.
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
        static std::mutex mutex;
        return mutex;
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
         */
        static bool EnsureSubsystemInitialized()
        {
            return SDL_InitSubSystem(SDL_INIT_SENSOR);
        }

        /**
         * Opens (if not already open) the first SDL sensor matching
         * TSensor::GetSdlSensorType() and caches its id. Returns the
         * shared handle. Caller must already hold mutex_.
         */
        SDL_Sensor* OpenDefaultSensorLocked()
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
         */
        static bool ProbeIsSupported()
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

        /** @brief Registers the SDL event watch if not already registered. Caller must already hold mutex_. */
        void RegisterEventWatchIfNeededLocked()
        {
            if (!eventWatchRegistered_)
            {
                const SDL_EventFilter eventFilter =
                    reinterpret_cast<SDL_EventFilter>(&SdlSensorSubsystem::SensorEventWatch);
                SDL_AddEventWatch(eventFilter, nullptr);
                eventWatchRegistered_ = true;
            }
        }

        /** @brief Unregisters the SDL event watch if no instances remain started. Caller must already hold mutex_. */
        void UnregisterEventWatchIfNeededLocked()
        {
            if (eventWatchRegistered_ && startedInstances_.empty())
            {
                const SDL_EventFilter eventFilter =
                    reinterpret_cast<SDL_EventFilter>(&SdlSensorSubsystem::SensorEventWatch);
                SDL_RemoveEventWatch(eventFilter, nullptr);
                eventWatchRegistered_ = false;
            }
        }

        /** @brief Adds instance to startedInstances_ if not already present. Caller must already hold mutex_. */
        void RegisterStartedInstanceLocked(TSensor* instance)
        {
            if (std::find(startedInstances_.begin(), startedInstances_.end(), instance) == startedInstances_.end())
            {
                startedInstances_.push_back(instance);
            }
        }

        /** @brief Removes instance from startedInstances_ if present. Caller must already hold mutex_. */
        void UnregisterStartedInstanceLocked(TSensor* instance)
        {
            const auto it = std::find(startedInstances_.begin(), startedInstances_.end(), instance);
            if (it != startedInstances_.end())
            {
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
        std::vector<TSensor*> startedInstances_;
        std::mutex mutex_;
        std::condition_variable callbackFinished_;

        /**
         * @brief Dispatches to each instance in `instancesSnapshot`, in
         * order, via `dispatchOne` (Task P7-3).
         *
         * Shared by the real SDL_EventFilter path (SensorEventWatch(),
         * below) and the NOXNA test-only dispatch hooks that TSensor
         * exposes — both funnel through this one method so the bookkeeping
         * can never diverge between the real path and a test simulating it.
         *
         * For each snapshotted pointer, re-validates it against the *live*
         * startedInstances_ list — by pointer value only, never
         * dereferencing the pointer before this check passes — and only if
         * still present, marks it as actively dispatching (pushes this
         * thread's id into its dispatchingThreadIds_) and calls
         * `dispatchOne(instance)`.
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
         * The fix: mark (and validate) each instance immediately before
         * dispatching to *that* instance, not in bulk up front. If a prior
         * iteration's callback already disposed a later instance in this
         * snapshot, that instance's own Dispose(bool)/Stop() has already
         * removed it from startedInstances_ (necessarily before any wait
         * that could let it be destroyed) — so the pointer-value-only
         * re-check here safely detects that and skips it, never touching
         * (dereferencing) the possibly-freed memory.
         *
         * Task P8-1: the cleanup guard below now captures a *copy* of
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
        void DispatchToInstances(const std::vector<TSensor*>& instancesSnapshot, DispatchFn&& dispatchOne)
        {
            const std::thread::id thisThreadId = std::this_thread::get_id();

            for (TSensor* instance : instancesSnapshot)
            {
                std::shared_ptr<std::vector<std::thread::id>> token;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (std::find(startedInstances_.begin(), startedInstances_.end(), instance)
                        != startedInstances_.end())
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
                catch (...)
                {
                    // Swallowed deliberately — see SensorEventWatch()'s doc
                    // comment: the real path is an SDL_EventFilter callback
                    // invoked directly by SDL_PushEvent(), a C API that does
                    // not expect a C++ exception to unwind through its own
                    // call frames, and swallowing it here also lets the
                    // remaining snapshotted instances still get dispatched
                    // to and cleaned up.
                }
            }
        }

    private:
        /**
         * SDL_EventFilter trampoline for TSensor's event watch. Takes a
         * stable snapshot of startedInstances_ (pointer values only) under
         * mutex_, then hands it to DispatchToInstances() above, which does
         * the actual per-instance re-validation, marking, dispatch, and
         * cleanup — see that method's doc comment for the full Task P7-3
         * rationale. Dispatch itself always runs outside the lock (so a
         * handler re-entering Start()/Stop()/Dispose() doesn't
         * self-deadlock on mutex_).
         */
        static bool SensorEventWatch(void* userdata, void* eventData)
        {
            (void)userdata;

            SDL_Event* event = static_cast<SDL_Event*>(eventData);

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

            std::vector<TSensor*> instancesSnapshot;
            {
                std::lock_guard<std::mutex> lock(subsystem.mutex_);
                instancesSnapshot = subsystem.startedInstances_;
            }

            subsystem.DispatchToInstances(instancesSnapshot, [sensorId, x, y, z](TSensor* instance)
            {
                instance->ProcessSensorUpdateEvent(sensorId, x, y, z);
            });

            return true;
        }
    };
} // namespace Microsoft::Devices::Sensors::Detail
