// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Provides access to the device's fused motion sensor (accelerometer + compass + gyroscope).
     *
     * @note Motion requires an Accelerometer, Compass, and Gyroscope. SDL3
     * exposes no magnetometer/compass API on any supported platform, so
     * getIsSupportedProperty() always returns false, and Start() always fails
     * with SensorFailedException until compass support becomes available.
     */
    class Motion final : public SensorBase<MotionReading>
    {
    private:
        static int instanceCount_;

        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        SensorState state_;
        bool started_;

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
        NOXNA [[nodiscard]] SensorState getStateProperty() const;

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
         * @throws ObjectDisposedException If the object was already disposed.
         * @throws SensorFailedException Always, since no platform currently exposes a compass sensor.
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
         * @brief Event raised when the compass component detects that it requires calibration.
         */
        System::EventHandler<CalibrationEventArgs> Calibrate;
    };
} // namespace Microsoft::Devices::Sensors
