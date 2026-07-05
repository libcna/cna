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
     */
    class Compass final : public SensorBase<CompassReading>
    {
    private:
        static int instanceCount_;

        /** @brief Guards instanceCount_'s check+increment/decrement (Task P6-1) against concurrent construct/destroy. */
        static std::mutex instanceCountMutex_;

        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

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
         * @throws ObjectDisposedException If the object was already disposed.
         * @throws SensorFailedException Always, since no platform currently exposes a compass sensor.
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
         * Must be called before Start(); has no effect on an already-started
         * instance.
         *
         * @param backend Replacement backend; pass nullptr to restore the
         * platform-default (no-backend/stub) behavior.
         */
        NOXNA void SetBackendForTesting(std::unique_ptr<Detail::ICompassBackend> backend);
    };
} // namespace Microsoft::Devices::Sensors
