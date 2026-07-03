// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Devices::Sensors
{
    /** @brief Provides access to the device accelerometer sensor. */
    class Accelerometer final : public SensorBase<AccelerometerReading>
    {
    private:
        static void* g_sensor_;
        static std::int64_t g_sensorId_;
        static int instanceCount_;
        static bool eventWatchRegistered_;
        static std::vector<Accelerometer*> startedInstances_;

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
         * @note Known accepted limitation: if a CurrentValueChanged/
         * ReadingChanged subscriber calls Dispose() on *this same instance*
         * from within its own handler (i.e. reentrantly, on the same thread
         * already executing ProcessSensorUpdateEvent() for this instance),
         * Dispose() would deadlock waiting on a flag only that same,
         * currently-blocked call could clear. Not solved here — judged an
         * unusual enough pattern (a handler disposing the very sensor
         * object mid-dispatch to it) to accept as a documented limitation
         * rather than adding further complexity (e.g. detecting the
         * disposing thread is the callback thread) for this task's scope.
         */
        bool inFlightCallback_ = false;

    private:
        static bool EnsureSensorSubsystemInitialized();
        static void* OpenDefaultAccelerometer();

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
            float z,
            std::uint64_t timestampNs
        );

        /**
         * Converts raw sensor floats into an AccelerometerReading and
         * raises CurrentValueChanged/ReadingChanged. No hardware-presence
         * guard — callers (ProcessSensorUpdateEvent() and the NOXNA
         * synthetic-injection hook below) are responsible for deciding
         * whether this call is legitimate.
         */
        void DispatchSensorReading(float x, float y, float z, std::uint64_t timestampNs);

    public:
        /**
         * @brief Gets whether the current platform supports the accelerometer sensor.
         *
         * @return true if supported; otherwise false.
         */
        static bool getIsSupportedProperty();

        /**
         * @brief Gets the current state of the accelerometer.
         *
         * @return Current sensor state.
         */
        [[nodiscard]] SensorState getStateProperty() const;

    public:
        /**
         * @brief Creates a new instance of the Accelerometer object.
         *
         * @throws SensorFailedException If the maximum number of simultaneous instances is exceeded.
         */
        Accelerometer();

        /**
         * @brief Destroys the accelerometer object.
         */
        ~Accelerometer() override;

        /**
         * @brief Starts data acquisition from the accelerometer.
         *
         * @throws ObjectDisposedException If the object was already disposed.
         * @throws AccelerometerFailedException If acquisition cannot be started.
         */
        void Start() override;

        /**
         * @brief Stops data acquisition from the accelerometer.
         *
         * @throws ObjectDisposedException If the object was already disposed.
         */
        void Stop() override;

        /**
         * @brief Disposes the accelerometer resources.
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
        using SensorBase<AccelerometerReading>::Dispose;

        GetTypeNameHPP()

        /**
         * @brief Test-only hook (Task P4-2): injects a synthetic sensor
         * update, bypassing the real-hardware-presence checks
         * (g_sensor_/sensorId matching) that the real SDL event path
         * enforces, so CurrentValueChanged/ReadingChanged's dispatch logic
         * can be exercised without a real, opened SDL accelerometer.
         *
         * Still respects the started/disposed state exactly as the real
         * event path does: a no-op if the instance isn't "started" (see
         * SetStartedForTesting()) or has already been disposed.
         *
         * @param x Raw X-axis sensor value, in m/s^2 (same units SDL reports).
         * @param y Raw Y-axis sensor value, in m/s^2.
         * @param z Raw Z-axis sensor value, in m/s^2.
         * @param timestampNs Synthetic event timestamp in nanoseconds.
         */
        NOXNA void InjectSyntheticSensorUpdate(float x, float y, float z, std::uint64_t timestampNs);

        /**
         * @brief Test-only hook (Task P4-2): directly sets the internal
         * "started" flag, without requiring a real SDL accelerometer to be
         * opened. Lets tests exercise InjectSyntheticSensorUpdate()'s
         * started-state gating — and confirm Stop() correctly disables it,
         * since Stop() always clears this flag regardless of how it was
         * set — in headless environments where the real Start() always
         * throws.
         *
         * @param started New value for the internal started flag.
         */
        NOXNA void SetStartedForTesting(bool started);

        /**
         * @brief Legacy WP7 7.0 event raised when the accelerometer reading changes.
         *
         * Deprecated in favor of CurrentValueChanged, which is the WP7 7.1
         * SensorBase pattern used by every other sensor in this namespace.
         * Kept and raised here only for API completeness with the real WP7
         * Accelerometer, which still exposes both.
         */
        System::EventHandler<AccelerometerReadingEventArgs> ReadingChanged;
    };
} // namespace Microsoft::Devices::Sensors
