// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <cstdint>
#include <thread>
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

namespace Microsoft::Devices::Sensors::Detail
{
    template <typename TSensor>
    class SdlSensorSubsystem;
} // namespace Microsoft::Devices::Sensors::Detail

namespace Microsoft::Devices::Sensors
{
    /** @brief Provides access to the device accelerometer sensor. */
    class Accelerometer final : public SensorBase<AccelerometerReading>
    {
        friend class Detail::SdlSensorSubsystem<Accelerometer>;

    private:
        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        SensorState state_;
        bool started_;

        /**
         * True once this instance has made its own successful
         * SDL_InitSubSystem(SDL_INIT_SENSOR) call (on its first successful
         * Start()). Paired with exactly one SDL_QuitSubSystem() call from
         * this same instance's Dispose(), regardless of how many other
         * instances (of this class or Gyroscope) exist — SDL's own
         * internal ref-counting aggregates all instances' balanced
         * init/quit pairs correctly (Task P4-8). Never re-set once true; a
         * later Start() after Stop() does not call SDL_InitSubSystem()
         * again.
         */
        bool subsystemHeld_ = false;

        /**
         * Thread IDs of calls currently mid-dispatch into this instance's
         * ProcessSensorUpdateEvent()/InjectSyntheticSensorUpdate(), one
         * entry per in-flight call (its size is the in-flight count).
         * Guarded by the shared subsystem's mutex_ (Task P5-4). Dispose()
         * waits until no *other* thread's entry remains (after first
         * removing the instance from the subsystem's startedInstances_,
         * so no *new* callback can start) before letting the object's
         * lifetime end, closing the use-after-free window left open by
         * Task P3-4.
         *
         * Task P5-2: was a single bool (inFlightCallback_) until this
         * task — SDL's own SDL_AddEventWatch() documentation warns the
         * callback "may run in a different thread", and SDL_PushEvent()
         * (which synchronously invokes it) is documented safe to call
         * from any thread — nothing rules out the event watch being
         * re-entered concurrently for this same instance from a second
         * thread while the first invocation is still mid-dispatch.
         *
         * Task P5-3: changed from a plain int counter to a vector of the
         * dispatching thread id(s), so Dispose() can tell "some other
         * thread is still dispatching to me, I must wait" apart from "the
         * only still-in-flight dispatch(es) are on my own thread" (a
         * handler calling Dispose() on its own sender reentrantly) — the
         * latter must not wait, since this thread's own dispatch frame
         * can only finish unwinding after Dispose() itself returns. See
         * Dispose(bool)'s wait predicate.
         */
        std::vector<std::thread::id> dispatchingThreadIds_;

    private:
        /**
         * @brief Returns this class's shared SDL sensor subsystem manager (Task P5-4).
         *
         * Defined in Accelerometer.cpp as a function-local static, so SDL
         * types never need to appear in this header — same discipline
         * this class already used for its previous `void* g_sensor_`.
         *
         * @return Reference to the single, process-lifetime subsystem instance.
         */
        static Detail::SdlSensorSubsystem<Accelerometer>& GetSubsystem();

        /**
         * @brief Returns SDL_SENSOR_ACCEL, as a plain int (Task P5-4).
         *
         * Kept as an `int`-returning function rather than an
         * SDL_SensorType-typed constant so this header never needs to
         * include any SDL header.
         *
         * @return SDL_SENSOR_ACCEL, cast to int.
         */
        static int GetSdlSensorType();

        /**
         * Validates the event belongs to this instance's open device
         * (started_, the shared subsystem's device/sensorId match), then
         * delegates to DispatchSensorReading() to do the actual
         * conversion+dispatch. Split out (Task P4-2) so
         * DispatchSensorReading() can be exercised directly by
         * InjectSyntheticSensorUpdate() below without requiring a real,
         * opened SDL sensor — which never exists in a headless test
         * environment.
         */
        void ProcessSensorUpdateEvent(
            std::int64_t sensorId,
            float x,
            float y,
            float z
        );

        /**
         * Converts raw sensor floats into an AccelerometerReading and
         * raises CurrentValueChanged/ReadingChanged. No hardware-presence
         * guard — callers (ProcessSensorUpdateEvent() and the NOXNA
         * synthetic-injection hook below) are responsible for deciding
         * whether this call is legitimate. Timestamp is always the real
         * wall-clock time of the call (Task P4-7), not derived from any
         * caller-supplied value.
         */
        void DispatchSensorReading(float x, float y, float z);

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
         * update, bypassing the real-hardware-presence checks (shared
         * subsystem device/sensorId matching) that the real SDL event
         * path enforces, so CurrentValueChanged/ReadingChanged's dispatch
         * logic can be exercised without a real, opened SDL accelerometer.
         *
         * Still respects the started/disposed state exactly as the real
         * event path does: a no-op if the instance isn't "started" (see
         * SetStartedForTesting()) or has already been disposed. The
         * resulting reading's Timestamp is always the real wall-clock time
         * of the call (Task P4-7), not a synthetic value.
         *
         * @param x Raw X-axis sensor value, in m/s^2 (same units SDL reports).
         * @param y Raw Y-axis sensor value, in m/s^2.
         * @param z Raw Z-axis sensor value, in m/s^2.
         */
        NOXNA void InjectSyntheticSensorUpdate(float x, float y, float z);

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
