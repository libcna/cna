// SPDX-License-Identifier: MS-PL

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp"
#include "Microsoft/Devices/Sensors/Detail/SensorOwnerControlBlock.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Provides access to the device's fused motion sensor.
     *
     * @note SDL3 exposes no fused-orientation/motion API on any supported
     * platform, so this class has no SDL-backed implementation. On
     * Android, a native backend (Detail::AndroidMotionBackend, Task
     * DEVICES-0101-0119) provides real Attitude/Gravity/DeviceAcceleration/
     * DeviceRotationRate data using the NDK's rotation-vector (or
     * game-rotation-vector fallback), gravity, linear-acceleration, and
     * gyroscope sensors directly — no SDL involvement, and no dependency
     * on constructing a live Accelerometer/Compass/Gyroscope C++ instance
     * (Task DEVICES-0114 — an earlier comment here suggested otherwise;
     * Android's own sensor fusion happens entirely inside the OS, not by
     * combining this codebase's own sensor objects). On every other
     * platform, getIsSupportedProperty() always returns false, and Start()
     * always fails with SensorFailedException, exactly as before.
     *
     * See `docs/devices-thread-safety.md` for this class's full,
     * consolidated thread-safety contract.
     */
    class Motion final : public SensorBase<MotionReading>
    {
    private:
        static int instanceCount_;

        /** @brief Guards instanceCount_'s check+increment/decrement (Task P6-1) against concurrent construct/destroy. */
        static std::mutex instanceCountMutex_;

        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        /**
         * @brief Shared-ownership control block (Tasks LIFE-001/LIFE-002/LIFE-003/LIFE-005).
         *
         * Motion is structurally identical to Compass, which a real
         * ThreadSanitizer run confirmed had an unguarded data race on
         * `state_`/`started_` (Task SENSORBASE-004) — unlike Accelerometer/
         * Gyroscope, whose equivalent fields are guarded by their shared
         * `Detail::PlatformSensorSubsystem<TSensor>::mutex_`. `control_->mutex`
         * is *the* lock guarding `state_`/`started_`/`transitioning_`/
         * `stopClaimed_`/`backendCallsInFlight_`/`backend_` — replaces this
         * class's previous plain `mutex_` member. Living in a separately
         * heap-allocated, `shared_ptr`-held block (not a direct member) is
         * what lets a native backend callback safely detect "the owner is
         * gone" without ever dereferencing a possibly-destroyed `Motion` —
         * see `Detail::SensorOwnerControlBlock`'s own doc comment for the
         * full rationale, and `Compass::Start()`/`Stop()`/`Dispose(bool)`'s
         * own comments (mirrored exactly here) for how each field is used.
         *
         * Unlike the prior single-critical-section design, `control_->mutex`
         * is now **never** held across a call into `backend_` (Task
         * LIFE-001) — `Detail::AndroidMotionBackend::Start()`/`Stop()` can
         * block (spawn/join worker threads) and, per Task LIFE-002, may
         * synchronously invoke a reading/calibration callback before
         * returning.
         *
         * See `docs/devices-thread-safety.md` for the full, consolidated
         * thread-safety contract this member is part of.
         */
        std::shared_ptr<Detail::SensorOwnerControlBlock<Motion>> control_ =
            std::make_shared<Detail::SensorOwnerControlBlock<Motion>>();

        SensorState state_;
        bool started_;

        /** @brief See Compass::transitioning_'s identical doc comment (Task LIFE-001). */
        bool transitioning_ = false;

        /** @brief See Compass::stopClaimed_'s identical doc comment (Task LIFE-001). */
        bool stopClaimed_ = false;

        /** @brief See Compass::backendCallsInFlight_'s identical doc comment (Task LIFE-003). */
        int backendCallsInFlight_ = 0;

        /** @brief Notified when transitioning_ becomes false (Task LIFE-001). */
        std::condition_variable transitionFinished_;

        /** @brief Notified when backendCallsInFlight_ reaches zero (Task LIFE-003). */
        std::condition_variable backendQuiescent_;

        /**
         * @brief Native backend, selected at construction time by a
         * compile-time platform switch. Null on any platform without one —
         * Start()/Stop() fall back to the permanent NotSupported stub in
         * that case, unchanged from before this backend existed. Guarded by
         * control_->mutex; never read/called without it (Task LIFE-001)
         * except for the specific, captured raw pointer a single
         * Start()/Stop() call carries across its own unlocked backend call.
         */
        std::unique_ptr<Detail::IMotionBackend> backend_;

    public:
        /**
         * @brief Gets whether the current platform supports fused motion sensing.
         *
         * @return true if supported; otherwise false.
         */
        static bool getIsSupportedProperty();

        /**
         * @brief Gets the current state of the motion sensor.
         *
         * CNA extension beyond the documented WP7 API: the real
         * Microsoft.Devices.Sensors.Motion class has no State property
         * (confirmed against its authoritative member list). Exposed here
         * for symmetry with Accelerometer, the one sensor class that does
         * have a real State property.
         *
         * @return Current sensor state.
         */
        CNAEXT [[nodiscard]] SensorState getStateProperty() const;

        /**
         * @brief Gets whether `CurrentValue.Attitude`'s yaw is currently referenced to true/magnetic north.
         *
         * CNA extension beyond the documented WP7 API (Task MOT2-005,
         * 2026-07-17, external audit `audit_devices_2026-07-17.md`): a real
         * Android backend prefers the magnetometer-fused, north-referenced
         * rotation vector, but falls back to the gyroscope/accelerometer-only
         * game rotation vector (free to drift in yaw over time, with no
         * absolute reference) if the former is unavailable on this device —
         * see `Detail::AndroidMotionBackend::Start()`'s own doc comment.
         * There was previously no way for a caller to discover which of the
         * two is actually in effect; this closes that gap ("expose fallback
         * diagnostics", this task's own required work) so an application can
         * choose to warn the player, disable a north-dependent feature, or
         * otherwise react, rather than silently trusting a drifting yaw as
         * if it were absolute.
         *
         * @return true if the active attitude source is north-referenced
         * (including when unsupported/not yet started, or on any platform
         * with no native backend at all — nothing to warn about in either
         * case); false if it is the drift-prone fallback.
         */
        CNAEXT [[nodiscard]] bool getIsAttitudeNorthReferencedProperty() const;

    public:
        /**
         * @brief Creates a new instance of the Motion object.
         *
         * @throws SensorFailedException If the maximum number of simultaneous instances is exceeded.
         */
        Motion();

        /**
         * @brief Destroys the motion object.
         */
        ~Motion() override;

        /**
         * @brief Starts data acquisition from the fused motion sensor.
         *
         * Real on Android (Detail::AndroidMotionBackend); throws on every
         * other platform, since no other supported platform currently
         * exposes fused motion sensing to this codebase (SDL3 has no
         * fused-orientation API anywhere).
         *
         * @throws ObjectDisposedException If the object was already disposed.
         * @throws SensorFailedException If data acquisition is already
         * started (call Stop() first to restart), or if fused motion
         * sensing is not supported on this platform/device.
         */
        void Start() override;

        /**
         * @brief Stops data acquisition from the fused motion sensor.
         *
         * @throws ObjectDisposedException If the object was already disposed.
         */
        void Stop() override;

        /**
         * @brief Disposes the motion sensor resources.
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
        using SensorBase<MotionReading>::Dispose;

        GetTypeNameHPP()

        /**
         * @brief Event raised when the motion sensor's underlying magnetometer detects that it requires calibration.
         */
        System::EventHandler<CalibrationEventArgs> Calibrate;

        /**
         * @brief Test-only hook (Task DEVICES-0113): replaces the real,
         * platform-selected backend with a caller-supplied one (typically a
         * test fake), so Start()/CurrentValueChanged delegation can be
         * exercised on any host without needing real Android hardware or
         * Detail::AndroidMotionBackend directly.
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
        CNAEXT void SetBackendForTesting(std::unique_ptr<Detail::IMotionBackend> backend);
    };
} // namespace Microsoft::Devices::Sensors
