// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/25/25.
//

#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

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
         * Guards g_sensor_, g_sensorId_, eventWatchRegistered_, and
         * startedInstances_ against the SDL event-watch callback
         * (SensorEventWatch) potentially running on a different thread than
         * Start()/Stop()/Dispose(). See SDL_AddEventWatch()'s own doc
         * comment. Not held across ProcessSensorUpdateEvent() itself, to
         * avoid holding a lock across an event-handler callout that might
         * re-enter Start()/Stop().
         */
        static std::mutex mutex_;

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

        /**
         * @brief Brings the base class's no-argument Dispose() into scope.
         *
         * Without this, declaring Dispose(bool) here would hide the
         * inherited public Dispose() override of System::IDisposable.
         */
        using SensorBase<AccelerometerReading>::Dispose;

        GetTypeNameHPP()

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
