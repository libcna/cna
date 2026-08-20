// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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

namespace CNA::Platform { enum class SensorKind; }
namespace CNA::Platform { class IPlatform; }

namespace Microsoft::Devices::Sensors::Detail
{
    template <typename TSensor>
    class PlatformSensorSubsystem;
} // namespace Microsoft::Devices::Sensors::Detail

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Provides access to the device accelerometer sensor.
     *
     * See `docs/devices-thread-safety.md` for this class's full,
     * consolidated thread-safety contract.
     */
    class Accelerometer final : public SensorBase<AccelerometerReading>
    {
        friend class Detail::PlatformSensorSubsystem<Accelerometer>;

    private:
        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        SensorState state_;
        bool started_;

        /**
         * True once this instance acquired the selected platform's sensor subsystem. The hold is
         * paired with exactly one release from Dispose(), regardless of Stop()/Start() cycles.
         */
        bool subsystemHeld_ = false;
        CNA::Platform::IPlatform* acquiredPlatform_ = nullptr;

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
         * Task P5-2: was a single bool (inFlightCallback_) until this task. Platform callbacks may
         * arrive concurrently, so a counter-equivalent collection is required.
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
         * a heap-allocated one, created once in the constructor and never
         * replaced. `Detail::PlatformSensorSubsystem<Accelerometer>::DispatchToInstances()`
         * and `InjectSyntheticSensorUpdate()` now copy this `shared_ptr`
         * into their dispatch-cleanup guard *before* invoking the user
         * callback, so that cleanup step (which runs after the callback
         * returns) touches the shared token, never `this` again — closing a
         * real use-after-free if a callback destroys (not just Dispose()s)
         * this same instance during its own dispatch. See
         * plans/plan_devices_phase8.md Task P8-1 for the full analysis, including
         * the remaining, deliberately-undefended boundary: destroying this
         * instance from within its own `CurrentValueChanged` handler is
         * still unsupported, because `DispatchSensorReading()` itself
         * unconditionally touches `this` again afterward (to decide whether
         * to also raise the legacy `ReadingChanged` event) — a class-design
         * property this token cannot fix.
         */
        std::shared_ptr<std::vector<std::thread::id>> dispatchToken_;

        /**
         * Test-only hook (Task P7-2): if set, invoked once by Dispose(bool)
         * immediately after this instance wins ClaimDisposalOnce(), before
         * any cleanup runs. Lets a test force a controlled pause at that
         * exact point (e.g. block until a second thread's concurrent
         * Dispose() call has had a chance to run and confirm it correctly
         * waits, rather than racing ahead) to deterministically reproduce a
         * specific interleaving that real thread scheduling cannot be
         * relied on to reproduce. Only ever set before any Dispose() call
         * begins (in a test's single-threaded setup phase) and never
         * written again afterward, so reading it here without a lock is
         * safe: thread creation already establishes the necessary
         * happens-before relationship between the test's setup and any
         * thread that goes on to call Dispose().
         */
        std::function<void()> disposalTestHook_;

    private:
        /**
         * @brief Returns this class's shared platform sensor-session manager (Task P5-4).
         *
         * @return Reference to the single, process-lifetime subsystem instance.
         */
        static Detail::PlatformSensorSubsystem<Accelerometer>& GetSubsystem();

        /**
         * @brief Returns the platform-neutral accelerometer kind.
         * @return SensorKind::Accelerometer.
         */
        static CNA::Platform::SensorKind GetPlatformSensorKind();

        /**
         * Validates the event belongs to this instance's open device
         * (started_, the shared subsystem's device/sensorId match), then
         * delegates to DispatchSensorReading() to do the actual
         * conversion+dispatch. Split out (Task P4-2) so
         * DispatchSensorReading() can be exercised directly by
         * InjectSyntheticSensorUpdate() below without requiring a real,
         * opened hardware sensor, which need not exist in a headless test environment.
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
         * guard — callers (ProcessSensorUpdateEvent() and the CNAEXT
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
         * subsystem device/sensorId matching) that the real platform event
         * path enforces, so CurrentValueChanged/ReadingChanged's dispatch
         * logic can be exercised without a real, opened accelerometer.
         *
         * Still respects the started/disposed state exactly as the real
         * event path does: a no-op if the instance isn't "started" (see
         * SetStartedForTesting()) or has already been disposed. The
         * resulting reading's Timestamp is always the real wall-clock time
         * of the call (Task P4-7), not a synthetic value.
         *
         * @param x Raw X-axis sensor value, in m/s^2 (the platform contract's units).
         * @param y Raw Y-axis sensor value, in m/s^2.
         * @param z Raw Z-axis sensor value, in m/s^2.
         */
        CNAEXT void InjectSyntheticSensorUpdate(float x, float y, float z);

        /**
         * @brief Test-only hook (Task P4-2): directly sets the internal
         * "started" flag, without requiring a real accelerometer to be
         * opened. Lets tests exercise InjectSyntheticSensorUpdate()'s
         * started-state gating — and confirm Stop() correctly disables it,
         * since Stop() always clears this flag regardless of how it was
         * set — in headless environments where the real Start() always
         * throws.
         *
         * @param started New value for the internal started flag.
         */
        CNAEXT void SetStartedForTesting(bool started);

        /**
         * @brief Test-only hook (Task P5-6): directly sets the base
         * class's isSupported_ flag, without requiring real accelerometer
         * hardware to be present.
         *
         * Deliberately separate from SetStartedForTesting(): the two
         * gate different things. SetStartedForTesting() controls whether
         * InjectSyntheticSensorUpdate() dispatches at all;
         * getCurrentValueProperty()/getIsDataValidProperty() are governed
         * solely by isSupported_ (SensorBase<T>), which this method — not
         * SetStartedForTesting() — controls. Without calling this,
         * getCurrentValueProperty() still throws
         * System::InvalidOperationException on unsupported hardware even
         * after SetStartedForTesting(true) + InjectSyntheticSensorUpdate()
         * — that is the correct, intentional contract (matching the real
         * hardware-support check, Task P3-1), not a gap this hook should
         * bypass by default.
         *
         * @param supported New value for the base class's isSupported_ flag.
         */
        CNAEXT void SetSupportedForTesting(bool supported);

        /**
         * @brief Test-only hook (Task P6-2): exposes whether this instance
         * currently holds its own successful platform sensor-subsystem acquisition. Lets tests
         * confirm a failed Start() releases the hold it just acquired.
         *
         * @return True if this instance currently holds the subsystem open.
         */
        CNAEXT [[nodiscard]] bool GetSubsystemHeldForTesting() const;

        /**
         * @brief Test-only hook (Task P7-2): sets a callback invoked once by
         * Dispose(bool), immediately after this instance wins
         * ClaimDisposalOnce(), before any cleanup runs. See
         * disposalTestHook_'s doc comment for the full rationale and its
         * single-threaded-setup safety argument.
         *
         * @param hook Callback to invoke; pass an empty std::function to clear it.
         */
        CNAEXT void SetDisposalCleanupHookForTesting(std::function<void()> hook);

        /**
         * @brief Test-only hook (Task P7-3): registers this instance into
         * the shared subsystem's startedInstances_ list directly, without
         * requiring a real sensor to be opened (the real Start() always
         * throws in a headless environment). Lets tests reproduce
         * multi-instance dispatch-batch scenarios (see
         * DispatchToInstancesForTesting()) that require an instance to be
         * genuinely present in startedInstances_ — SetStartedForTesting()
         * alone only sets this instance's own started_ flag, not the
         * shared subsystem-level registry a real dispatch batch is drawn
         * from.
         *
         * @param instance Instance to register.
         */
        CNAEXT static void RegisterStartedInstanceForTesting(Accelerometer& instance);

        /**
         * @brief Test-only hook (Task P7-3): removes this instance from the
         * shared subsystem's startedInstances_ list directly. See
         * RegisterStartedInstanceForTesting()'s doc comment.
         *
         * @param instance Instance to unregister.
         */
        CNAEXT static void UnregisterStartedInstanceForTesting(Accelerometer& instance);

        /**
         * @brief Test-only hook (Task P7-3): simulates the multi-instance
         * portion of the real platform callback dispatch path for a
         * caller-supplied list of instances, using the exact same
         * bookkeeping method (DispatchToInstances()) the real path uses —
         * so a test can reproduce a specific batch ordering/interleaving
         * (e.g. one instance's callback disposing a different, not-yet-
         * dispatched instance in the same simulated batch) that real sensor
         * event timing cannot be controlled to reproduce deterministically
         * in a headless test environment. Dispatches via
         * DispatchSensorReading() directly (bypassing the real
         * hardware-presence gate ProcessSensorUpdateEvent() enforces, the
         * same way InjectSyntheticSensorUpdate() does), since no real
         * sensor is ever open in this environment.
         *
         * Each instance must already be registered via
         * RegisterStartedInstanceForTesting() for this to dispatch to it —
         * otherwise DispatchToInstances()'s own re-validation against the
         * live startedInstances_ list will (correctly) skip it.
         *
         * @param instances Instances to dispatch to, in order.
         * @param x Raw X-axis sensor value to pass to each instance's DispatchSensorReading().
         * @param y Raw Y-axis sensor value.
         * @param z Raw Z-axis sensor value.
         */
        CNAEXT static void DispatchToInstancesForTesting(
            const std::vector<Accelerometer*>& instances, float x, float y, float z);

        /**
         * @brief Test-only hook: forces the next
         * platform session registration attempt (inside Start()) to report
         * failure, without attempting the real platform call at all. The platform API cannot be
         * forced to fail on
         * demand. Lets a test deterministically exercise Start()'s
         * rollback path (release a freshly-acquired subsystem hold, throw,
         * and leave started_/state_/the shared started-instance registry
         * untouched) rather than merely asserting the failure is "possible
         * in principle."
         *
         * Affects every Accelerometer instance (the underlying flag is
         * process-wide, shared with the underlying subsystem all instances
         * of this class use) -- a test that sets this to true must reset it
         * to false afterward, even on failure, so it cannot leak into a
         * later, unrelated test.
         *
         * @param shouldFail true to force the next registration attempt to fail.
         */
        CNAEXT static void SetEventWatchRegistrationFailureForTesting(bool shouldFail);

        /**
         * @brief Test-only hook: total callback exceptions swallowed so far.
         *
         * Forwards to the shared
         * Detail::PlatformSensorSubsystem<Accelerometer>::dispatchExceptionCountForTesting_
         * -- process-wide and never reset, so a test must compare against
         * this value from *before* its own throwing-callback action, not
         * assume it starts at zero.
         *
         * @return The number of exceptions the shared platform dispatch has ever swallowed.
         */
        CNAEXT static int GetDispatchExceptionCountForTesting();

        /**
         * @brief Test-only hook: message from the most recently swallowed callback exception.
         *
         * @return `ex.what()` for a swallowed `std::exception`, a fixed placeholder for any other thrown value, or empty if none has been swallowed yet.
         */
        CNAEXT static std::string GetLastDispatchExceptionMessageForTesting();

        /**
         * @brief Test-only hook: true if `sensorId` is still enumerated by the platform.
         * @param sensorId The stable platform sensor id to check.
         * @return true if still present in the current platform enumeration.
         */
        CNAEXT static bool IsSensorConnectedForTesting(std::int64_t sensorId);

        /**
         * @brief Legacy WP7 7.0 event raised when the accelerometer reading changes.
         *
         * Deprecated in favor of CurrentValueChanged, which is the WP7 7.1
         * SensorBase pattern used by every other sensor in this namespace
         * (archived MSDN page for this event, `ff707930(v=vs.105)`,
         * re-verified Task ACCEL-001: `[ObsoleteAttribute("use
         * CurrentValueChanged")]`, "Supported in: 7.0. Obsolete (compiler
         * warning) in 8.1, 8.0, 7.1" — real, present, still-raised API, not
         * removed, on every WP7 version). Kept and raised here only for API
         * completeness with the real WP7 Accelerometer, which still exposes
         * both.
         *
         * @note Firing order (Task ACCEL-002, verified against
         * DispatchSensorReading()'s actual implementation): CurrentValueChanged
         * always fires first, ReadingChanged second, for the same reading —
         * never the reverse, never interleaved.
         */
        System::EventHandler<AccelerometerReadingEventArgs> ReadingChanged;
    };
} // namespace Microsoft::Devices::Sensors
