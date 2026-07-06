// SPDX-License-Identifier: MS-PL

#pragma once

#include <memory>
#include <mutex>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp"
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
         * @brief Guards state_/started_ (Task SENSORBASE-004).
         *
         * Confirmed missing by a real ThreadSanitizer run (not just a
         * theoretical audit finding): `Start()`/`Stop()` write `state_`/
         * `started_` and `getStateProperty()` reads `state_`, all previously
         * completely unguarded — unlike `Accelerometer`/`Gyroscope`, whose
         * equivalent fields are guarded by their shared
         * `Detail::SdlSensorSubsystem<TSensor>::mutex_`. Held for each of
         * `Start()`/`Stop()`/`getStateProperty()`'s entire body, including
         * the actual `backend_->Start()`/`Stop()` call — safe to do so
         * because neither ever synchronously re-enters `Compass` (the real
         * `Detail::AndroidCompassBackend::Start()` only spawns worker
         * threads and waits for a startup handshake; sample/calibration
         * callbacks are only ever invoked later, asynchronously, from those
         * threads — never during the `Start()`/`Stop()` call itself).
         *
         * See `docs/devices-thread-safety.md` for the full, consolidated
         * thread-safety contract this member is part of.
         */
        mutable std::mutex mutex_;

        SensorState state_;
        bool started_;

        /**
         * @brief Native backend, selected at construction time by a
         * compile-time platform switch (docs/devices-native-backend-design.md's
         * migration plan). Null on any platform without one — Start()/Stop()
         * fall back to the permanent NotSupported stub in that case,
         * unchanged from before this backend existed.
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
