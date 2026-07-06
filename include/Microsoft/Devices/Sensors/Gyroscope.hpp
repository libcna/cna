// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    template <typename TSensor>
    class SdlSensorSubsystem;
} // namespace Microsoft::Devices::Sensors::Detail

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Provides access to the device gyroscope sensor.
     *
     * See `docs/devices-thread-safety.md` for this class's full,
     * consolidated thread-safety contract.
     */
    class Gyroscope final : public SensorBase<GyroscopeReading>
    {
        friend class Detail::SdlSensorSubsystem<Gyroscope>;

    private:
        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        SensorState state_;
        bool started_;

        /**
         * True once this instance has made its own successful
         * SDL_InitSubSystem(SDL_INIT_SENSOR) call (on its first successful
         * Start()). Paired with exactly one SDL_QuitSubSystem() call from
         * this same instance's Dispose(), regardless of how many other
         * instances (of this class or Accelerometer) exist — SDL's own
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
         * task — see Accelerometer.hpp's identical member for the full
         * rationale (SDL's own documented "may run in a different thread"
         * event-watch warning).
         *
         * Task P5-3: changed from a plain int counter to a vector of the
         * dispatching thread id(s), so Dispose() can tell "some other
         * thread is still dispatching to me, I must wait" apart from "the
         * only still-in-flight dispatch(es) are on my own thread" (a
         * handler calling Dispose() on its own sender reentrantly) — the
         * latter must not wait, since this thread's own dispatch frame
         * can only finish unwinding after Dispose() itself returns. See
         * Dispose(bool)'s wait predicate.
         *
         * Task P8-1: changed from a plain member vector to a `shared_ptr` to
         * a heap-allocated one — see Accelerometer.hpp's identical member
         * for the full rationale. Unlike Accelerometer, Gyroscope's
         * DispatchSensorReading() raises CurrentValueChanged as its last
         * statement and touches `this` for nothing afterward, so — with
         * this token fix — destroying this same instance from within its
         * own CurrentValueChanged handler is fully supported for this
         * class (see plan_devices_phase8.md Task P8-1).
         */
        std::shared_ptr<std::vector<std::thread::id>> dispatchToken_;

        /**
         * Test-only hook (Task P7-2): see Accelerometer.hpp's identical
         * member for the full rationale and single-threaded-setup safety
         * argument.
         */
        std::function<void()> disposalTestHook_;

    private:
        /**
         * @brief Returns this class's shared SDL sensor subsystem manager (Task P5-4).
         *
         * Defined in Gyroscope.cpp as a function-local static, so SDL
         * types never need to appear in this header — same discipline
         * this class already used for its previous `void* g_sensor_`.
         *
         * @return Reference to the single, process-lifetime subsystem instance.
         */
        static Detail::SdlSensorSubsystem<Gyroscope>& GetSubsystem();

        /**
         * @brief Returns SDL_SENSOR_GYRO, as a plain int (Task P5-4).
         *
         * Kept as an `int`-returning function rather than an
         * SDL_SensorType-typed constant so this header never needs to
         * include any SDL header.
         *
         * @return SDL_SENSOR_GYRO, cast to int.
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
         * update, bypassing the real-hardware-presence checks (shared
         * subsystem device/sensorId matching) that the real SDL event
         * path enforces, so CurrentValueChanged's dispatch logic can be
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

        /**
         * @brief Test-only hook (Task P5-6): directly sets the base
         * class's isSupported_ flag, without requiring real gyroscope
         * hardware to be present.
         *
         * Deliberately separate from SetStartedForTesting() — see
         * Accelerometer.hpp's identical hook for the full rationale.
         * Without calling this, getCurrentValueProperty() still throws
         * System::InvalidOperationException on unsupported hardware even
         * after SetStartedForTesting(true) + InjectSyntheticSensorUpdate()
         * — that is the correct, intentional contract (Task P3-1).
         *
         * @param supported New value for the base class's isSupported_ flag.
         */
        NOXNA void SetSupportedForTesting(bool supported);

        /**
         * @brief Test-only hook (Task P6-2): exposes whether this instance
         * currently holds its own successful
         * SDL_InitSubSystem(SDL_INIT_SENSOR) call — see
         * Accelerometer.hpp's identical hook for the full rationale.
         *
         * @return True if this instance currently holds the subsystem open.
         */
        NOXNA [[nodiscard]] bool GetSubsystemHeldForTesting() const;

        /**
         * @brief Test-only hook (Task P7-2): see Accelerometer.hpp's
         * identical hook for the full rationale.
         *
         * @param hook Callback to invoke; pass an empty std::function to clear it.
         */
        NOXNA void SetDisposalCleanupHookForTesting(std::function<void()> hook);

        /**
         * @brief Test-only hook (Task P7-3): see Accelerometer.hpp's
         * identical hook for the full rationale.
         *
         * @param instance Instance to register.
         */
        NOXNA static void RegisterStartedInstanceForTesting(Gyroscope& instance);

        /**
         * @brief Test-only hook (Task P7-3): see Accelerometer.hpp's
         * identical hook for the full rationale.
         *
         * @param instance Instance to unregister.
         */
        NOXNA static void UnregisterStartedInstanceForTesting(Gyroscope& instance);

        /**
         * @brief Test-only hook (Task P7-3): see Accelerometer.hpp's
         * identical hook for the full rationale.
         *
         * @param instances Instances to dispatch to, in order.
         * @param x Raw X-axis rotation rate to pass to each instance's DispatchSensorReading().
         * @param y Raw Y-axis rotation rate.
         * @param z Raw Z-axis rotation rate.
         */
        NOXNA static void DispatchToInstancesForTesting(
            const std::vector<Gyroscope*>& instances, float x, float y, float z);
    };
} // namespace Microsoft::Devices::Sensors
