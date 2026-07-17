// SPDX-License-Identifier: MS-PL

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp"
#include "Microsoft/Devices/Sensors/Detail/SensorOwnerControlBlock.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Provides access to the device compass sensor.
     *
     * @note SDL3 exposes no magnetometer/compass API on any supported
     * platform, so this class has no SDL-backed implementation. On
     * Android, a native backend (Detail::AndroidCompassBackend, Task
     * DEVICES-0086-0100) provides real heading/magnetometer data using the
     * NDK's rotation-vector and magnetic-field sensors directly — no SDL
     * involvement. On every other platform, getIsSupportedProperty()
     * always returns false, and Start() always fails with
     * SensorFailedException, exactly as before.
     *
     * See `docs/devices-thread-safety.md` for this class's full,
     * consolidated thread-safety contract.
     */
    class Compass final : public SensorBase<CompassReading>
    {
    private:
        static int instanceCount_;

        /** @brief Guards instanceCount_'s check+increment/decrement (Task P6-1) against concurrent construct/destroy. */
        static std::mutex instanceCountMutex_;

        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        /**
         * @brief Shared-ownership control block (Tasks LIFE-001/LIFE-002/LIFE-003/LIFE-005).
         *
         * `control_->mutex` is *the* lock guarding `state_`/`started_`/
         * `transitioning_`/`stopClaimed_`/`backendCallsInFlight_`/`backend_`
         * — replaces this class's previous plain `mutex_` member (Task
         * SENSORBASE-004's original fix, confirmed missing by a real
         * ThreadSanitizer run). Living in a separately heap-allocated,
         * `shared_ptr`-held block (not a direct member) is what lets a
         * native backend callback safely detect "the owner is gone" without
         * ever dereferencing a possibly-destroyed `Compass` — see
         * `Detail::SensorOwnerControlBlock`'s own doc comment for the full
         * rationale, and `Start()`/`Stop()`/`Dispose(bool)`'s own comments
         * for exactly how each field is used.
         *
         * Unlike the prior single-critical-section design, `control_->mutex`
         * is now **never** held across a call into `backend_` (Task
         * LIFE-001) — `Detail::AndroidCompassBackend::Start()`/`Stop()` can
         * block (spawn/join worker threads) and, per Task LIFE-002, may
         * synchronously invoke a reading/calibration callback before
         * returning; holding this lock across that call previously risked a
         * lock/join stall or deadlock if a user handler reentered a method
         * needing the same lock.
         *
         * See `docs/devices-thread-safety.md` for the full, consolidated
         * thread-safety contract this member is part of.
         */
        std::shared_ptr<Detail::SensorOwnerControlBlock<Compass>> control_ =
            std::make_shared<Detail::SensorOwnerControlBlock<Compass>>();

        SensorState state_;
        bool started_;

        /**
         * @brief True from the moment Start()/Stop() reserves a lifecycle
         * transition (under control_->mutex) until it commits, i.e. for the
         * entire window a call into backend_ is in flight without the lock
         * held (Task LIFE-001). A concurrent Start() while this is true (or
         * while started_ is true) throws the existing "already started"
         * exception immediately, without waiting — a strict improvement
         * over the prior design, which let a second caller block on
         * mutex_ for the *entire* backend_->Start() call before reaching
         * the same exception.
         */
        bool transitioning_ = false;

        /**
         * @brief True once some caller has claimed the right to actually
         * call backend_->Stop() for the current stop attempt (Task LIFE-001)
         * — mirrors Detail::AndroidSensorBridge's own reclaimClaimed_
         * pattern (Task ANDROID-BRIDGE-005) one layer up. Every other
         * concurrent Stop() caller waits on transitionFinished_ instead of
         * also calling backend_->Stop().
         */
        bool stopClaimed_ = false;

        /**
         * @brief Count of threads currently inside a call to `backend_`
         * (Start/Stop), guarded by control_->mutex (Task LIFE-003).
         *
         * A Start() attempt superseded by a concurrent Stop() while its own
         * backend_->Start() call was still in flight must still call
         * backend_->Stop() on whatever it just (uselessly) started, so it
         * doesn't leak — but by the time that cleanup call runs,
         * transitioning_ may already have been reset to false by the
         * superseding Stop() (which owns that flag once it wins the race).
         * SetBackendForTesting() must not swap/destroy backend_ while that
         * cleanup call (or any other in-flight call) is still executing on
         * it, so it waits on backendQuiescent_ for this count to reach zero
         * — not just for transitioning_ to be false.
         */
        int backendCallsInFlight_ = 0;

        /** @brief Notified when transitioning_ becomes false (Task LIFE-001). */
        std::condition_variable transitionFinished_;

        /** @brief Notified when backendCallsInFlight_ reaches zero (Task LIFE-003). */
        std::condition_variable backendQuiescent_;

        /**
         * @brief Native backend, selected at construction time by a
         * compile-time platform switch (docs/devices-native-backend-design.md's
         * migration plan). Null on any platform without one — Start()/Stop()
         * fall back to the permanent NotSupported stub in that case,
         * unchanged from before this backend existed. Guarded by
         * control_->mutex; never read/called without it (Task LIFE-001)
         * except for the specific, captured raw pointer a single
         * Start()/Stop() call carries across its own unlocked backend call.
         */
        std::unique_ptr<Detail::ICompassBackend> backend_;

    public:
        /**
         * @brief Gets whether the current platform supports the compass sensor.
         *
         * @return true if supported; otherwise false.
         */
        static bool getIsSupportedProperty();

        /**
         * @brief Gets the current state of the compass.
         *
         * CNA extension beyond the documented WP7 API: the real
         * Microsoft.Devices.Sensors.Compass class has no State property
         * (confirmed against its authoritative member list). Exposed here
         * for symmetry with Accelerometer, the one sensor class that does
         * have a real State property.
         *
         * @return Current sensor state.
         */
        NOXNA [[nodiscard]] SensorState getStateProperty() const;

    public:
        /**
         * @brief Creates a new instance of the Compass object.
         *
         * @throws SensorFailedException If the maximum number of simultaneous instances is exceeded.
         */
        Compass();

        /**
         * @brief Destroys the compass object.
         */
        ~Compass() override;

        /**
         * @brief Starts data acquisition from the compass.
         *
         * Real on Android (Detail::AndroidCompassBackend); throws on every
         * other platform, since no other supported platform currently
         * exposes a compass sensor to this codebase (SDL3 has no
         * magnetometer API anywhere).
         *
         * @throws ObjectDisposedException If the object was already disposed.
         * @throws SensorFailedException If data acquisition is already
         * started (call Stop() first to restart), or if the compass sensor
         * is not supported on this platform/device.
         */
        void Start() override;

        /**
         * @brief Stops data acquisition from the compass.
         *
         * @throws ObjectDisposedException If the object was already disposed.
         */
        void Stop() override;

        /**
         * @brief Disposes the compass resources.
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
        using SensorBase<CompassReading>::Dispose;

        GetTypeNameHPP()

        /**
         * @brief Event raised when the compass detects that it requires calibration.
         */
        System::EventHandler<CalibrationEventArgs> Calibrate;

        /**
         * @brief Test-only hook (Task DEVICES-0096): replaces the real,
         * platform-selected backend with a caller-supplied one (typically a
         * test fake), so Start()/CurrentValueChanged/Calibrate delegation
         * can be exercised on any host without needing real Android
         * hardware or Detail::AndroidCompassBackend directly.
         *
         * Must be called before Start() — enforced, not just documented:
         * throws if this instance is currently started, so a caller can
         * never silently swap out a running backend out from under an
         * active Start()/Stop() session (which would leave the old
         * backend's own worker state running unmanaged).
         *
         * @param backend Replacement backend; pass nullptr to restore the
         * platform-default (no-backend/stub) behavior.
         * @throws SensorFailedException If this instance is currently started.
         */
        NOXNA void SetBackendForTesting(std::unique_ptr<Detail::ICompassBackend> backend);
    };
} // namespace Microsoft::Devices::Sensors
