// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

namespace Microsoft::Devices::Sensors
{
    /** @brief Provides access to the device gyroscope sensor. */
    class Gyroscope final : public SensorBase<GyroscopeReading>
    {
    private:
        static void* g_sensor_;
        static std::int64_t g_sensorId_;
        static int instanceCount_;
        static bool eventWatchRegistered_;
        static std::vector<Gyroscope*> startedInstances_;

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
        static void* OpenDefaultGyroscope();

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
    };
} // namespace Microsoft::Devices::Sensors
