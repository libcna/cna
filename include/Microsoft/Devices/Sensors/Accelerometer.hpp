//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <cstdint>
#include <vector>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

namespace Microsoft::Devices::Sensors {

    /**
     * @brief Provides access to the device accelerometer sensor.
     *
     * This is a partial C++ counterpart of the MonoGame
     * Microsoft.Devices.Sensors.Accelerometer class.
     *
     * @note Status: Partial.
     * @note Public API intentionally follows the original C# type.
     * SDL3 event processing is kept internal.
     * @note Runtime behavior is expected to be validated primarily on Android devices.
     */
    class Accelerometer final : public SensorBase<AccelerometerReading> {
    private:
        static void* g_sensor_;
        static std::int64_t g_sensorId_;
        static int instanceCount_;
        static bool eventWatchRegistered_;
        static std::vector<Accelerometer*> startedInstances_;

        static constexpr SharpRuntime::bytecs MaxSensorCount = 10;

        SensorState state_;
        bool started_;

    private:
        static bool EnsureSensorSubsystemInitialized();
        static void* OpenDefaultAccelerometer();

        static void RegisterEventWatchIfNeeded();
        static void UnregisterEventWatchIfNeeded();

        static bool SensorEventWatch(void* userdata, void* eventData);

        void ProcessSensorUpdateEvent(
            std::int64_t sensorId,
            float x,
            float y,
            float z,
            std::uint64_t timestampNs
        );

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

        GetTypeNameHPP()
    };

} // namespace Microsoft::Devices::Sensors