// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
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
     */
    template <typename F>
    class ScopeExit
    {
    public:
        explicit ScopeExit(F onExit)
            : onExit_(std::move(onExit))
        {
        }

        ~ScopeExit()
        {
            onExit_();
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
     *     `std::vector<std::thread::id> dispatchingThreadIds_` members
     *     (Tasks P4-8/P5-3) — these remain genuinely per-*instance* data
     *     on TSensor itself, not here.
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

    private:
        /**
         * SDL_EventFilter trampoline for TSensor's event watch. Snapshots
         * startedInstances_ under mutex_ (recording each instance's
         * dispatching thread id — Task P5-3), then calls each snapshotted
         * instance's ProcessSensorUpdateEvent() outside the lock (so a
         * handler re-entering Start()/Stop()/Dispose() doesn't self-deadlock
         * on mutex_), clearing its dispatch entry and notifying
         * callbackFinished_ after each one returns — via a ScopeExit guard
         * (Task P6-4) so this cleanup still runs even if
         * ProcessSensorUpdateEvent() throws. Any exception from
         * ProcessSensorUpdateEvent() (or transitively a user's
         * CurrentValueChanged/ReadingChanged handler) is caught and
         * swallowed per-instance (Task P6-4): this is an SDL_EventFilter
         * callback invoked directly by SDL_PushEvent(), a C API that does
         * not expect a C++ exception to unwind through its own call
         * frames, and swallowing it here also lets the remaining
         * snapshotted instances still get dispatched to and cleaned up.
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
            const std::thread::id thisThreadId = std::this_thread::get_id();

            std::vector<TSensor*> instancesSnapshot;
            {
                std::lock_guard<std::mutex> lock(subsystem.mutex_);
                for (TSensor* instance : subsystem.startedInstances_)
                {
                    if (instance != nullptr)
                    {
                        instance->dispatchingThreadIds_.push_back(thisThreadId);
                        instancesSnapshot.push_back(instance);
                    }
                }
            }

            for (TSensor* instance : instancesSnapshot)
            {
                auto cleanupGuard = MakeScopeExit([&subsystem, instance, thisThreadId]()
                {
                    {
                        std::lock_guard<std::mutex> lock(subsystem.mutex_);
                        auto& ids = instance->dispatchingThreadIds_;
                        const auto it = std::find(ids.begin(), ids.end(), thisThreadId);
                        if (it != ids.end())
                        {
                            ids.erase(it);
                        }
                    }
                    subsystem.callbackFinished_.notify_all();
                });

                try
                {
                    instance->ProcessSensorUpdateEvent(
                        sensorId,
                        event->sensor.data[0],
                        event->sensor.data[1],
                        event->sensor.data[2]
                    );
                }
                catch (...)
                {
                    // Swallowed deliberately — see this method's doc comment.
                }
            }

            return true;
        }
    };
} // namespace Microsoft::Devices::Sensors::Detail
