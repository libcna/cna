// SPDX-License-Identifier: MS-PL

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

namespace Microsoft::Devices::Sensors
{
    /** @brief Provides access to the device gyroscope sensor. */
    class Gyroscope final : public SensorBase<GyroscopeReading>
    {
    private:
        static void* g_sensor_;
        static std::int64_t g_sensorId_;
        static int instanceCount_;
        static bool eventWatchRegistered_;
        static std::vector<Gyroscope*> startedInstances_;

        /**
         * Guards g_sensor_, g_sensorId_, eventWatchRegistered_,
         * startedInstances_, and every instance's inFlightCallback_ against
         * the SDL event-watch callback (SensorEventWatch) potentially
         * running on a different thread than Start()/Stop()/Dispose(). See
         * SDL_AddEventWatch()'s own doc comment. Not held across
         * ProcessSensorUpdateEvent() itself, to avoid holding a lock across
         * an event-handler callout that might re-enter Start()/Stop().
         */
        static std::mutex mutex_;

        /**
         * Signaled whenever an instance's inFlightCallback_ clears, so
         * Dispose() can wait for a concurrently-running callback on this
         * instance to finish before the object's lifetime ends. Shared
         * across all instances (a broadcast-and-recheck pattern) rather
         * than per-instance, since at the ≤10-instance cap the extra
         * spurious wakeups are negligible.
         */
        static std::condition_variable callbackFinished_;

        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        SensorState state_;
        bool started_;

        /**
         * True while SensorEventWatch() is (possibly on another thread)
         * mid-call into this instance's ProcessSensorUpdateEvent(). Guarded
         * by mutex_. Dispose() waits for this to clear (after first
         * removing the instance from startedInstances_, so no *new*
         * callback can start) before letting the object's lifetime end,
         * closing the use-after-free window left open by Task P3-4.
         *
         * @note Known accepted limitation: if a CurrentValueChanged
         * subscriber calls Dispose() on *this same instance* from within
         * its own handler (i.e. reentrantly, on the same thread already
         * executing ProcessSensorUpdateEvent() for this instance),
         * Dispose() would deadlock waiting on a flag only that same,
         * currently-blocked call could clear. Not solved here — see
         * Accelerometer.hpp's identical note for the full rationale.
         */
        bool inFlightCallback_ = false;

    private:
        static bool EnsureSensorSubsystemInitialized();
        static void* OpenDefaultGyroscope();

        static void RegisterEventWatchIfNeeded();
        static void UnregisterEventWatchIfNeeded();

        static bool SensorEventWatch(void* userdata, void* eventData);

        /**
         * Validates the event belongs to this instance's open device
         * (started_, g_sensor_, sensorId match), then delegates to
         * DispatchSensorReading() to do the actual conversion+dispatch.
         * Split out (Task P4-2) so DispatchSensorReading() can be exercised
         * directly by InjectSyntheticSensorUpdate() below without requiring
         * a real, opened SDL sensor — which never exists in a headless
         * test environment.
         */
        void ProcessSensorUpdateEvent(
            std::int64_t sensorId,
            float x,
            float y,
            float z
        );

        /**
         * Converts raw sensor floats into a GyroscopeReading and raises
         * CurrentValueChanged. No hardware-presence guard — callers
         * (ProcessSensorUpdateEvent() and the NOXNA synthetic-injection
         * hook below) are responsible for deciding whether this call is
         * legitimate. Timestamp is always the real wall-clock time of the
         * call (Task P4-7), not derived from any caller-supplied value.
         */
        void DispatchSensorReading(float x, float y, float z);

    public:
        /**
         * @brief Gets whether the current platform supports the gyroscope sensor.
         *
         * @return true if supported; otherwise false.
         */
        static bool getIsSupportedProperty();

        /**
         * @brief Gets the current state of the gyroscope.
         *
         * CNA extension beyond the documented WP7 API: the real
         * Microsoft.Devices.Sensors.Gyroscope class has no State property
         * (confirmed against its authoritative member list). Exposed here
         * for symmetry with Accelerometer, the one sensor class that does
         * have a real State property.
         *
         * @return Current sensor state.
         */
        NOXNA [[nodiscard]] SensorState getStateProperty() const;

    public:
        /**
         * @brief Creates a new instance of the Gyroscope object.
         *
         * @throws SensorFailedException If the maximum number of simultaneous instances is exceeded.
         */
        Gyroscope();

        /**
         * @brief Destroys the gyroscope object.
         */
        ~Gyroscope() override;

        /**
         * @brief Starts data acquisition from the gyroscope.
         *
         * @throws ObjectDisposedException If the object was already disposed.
         * @throws SensorFailedException If acquisition cannot be started.
         */
        void Start() override;

        /**
         * @brief Stops data acquisition from the gyroscope.
         *
         * @throws ObjectDisposedException If the object was already disposed.
         */
        void Stop() override;

        /**
         * @brief Disposes the gyroscope resources.
         *
         * @param disposing True when called from Dispose(); false when called from destructor path.
         */
        void Dispose(bool disposing) override;

        /**
         * @brief Brings the base class's no-argument Dispose() into scope.
         *
         * Without this, declaring Dispose(bool) here would hide the
         * inherited public Dispose() override of System::IDisposable.
         */
        using SensorBase<GyroscopeReading>::Dispose;

        GetTypeNameHPP()

        /**
         * @brief Test-only hook (Task P4-2): injects a synthetic sensor
         * update, bypassing the real-hardware-presence checks
         * (g_sensor_/sensorId matching) that the real SDL event path
         * enforces, so CurrentValueChanged's dispatch logic can be
         * exercised without a real, opened SDL gyroscope.
         *
         * Still respects the started/disposed state exactly as the real
         * event path does: a no-op if the instance isn't "started" (see
         * SetStartedForTesting()) or has already been disposed. The
         * resulting reading's Timestamp is always the real wall-clock time
         * of the call (Task P4-7), not a synthetic value.
         *
         * @param x Raw X-axis rotation rate, in radians/second.
         * @param y Raw Y-axis rotation rate, in radians/second.
         * @param z Raw Z-axis rotation rate, in radians/second.
         */
        NOXNA void InjectSyntheticSensorUpdate(float x, float y, float z);

        /**
         * @brief Test-only hook (Task P4-2): directly sets the internal
         * "started" flag, without requiring a real SDL gyroscope to be
         * opened. Lets tests exercise InjectSyntheticSensorUpdate()'s
         * started-state gating — and confirm Stop() correctly disables it,
         * since Stop() always clears this flag regardless of how it was
         * set — in headless environments where the real Start() always
         * throws.
         *
         * @param started New value for the internal started flag.
         */
        NOXNA void SetStartedForTesting(bool started);
    };
} // namespace Microsoft::Devices::Sensors
