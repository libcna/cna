// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Platform {

    /** @brief A three-axis sensor reading. */
    struct SensorReading
    {
        /** @brief X component, in the sensor's own units. */
        float x = 0.0f;
        /** @brief Y component. */
        float y = 0.0f;
        /** @brief Z component. */
        float z = 0.0f;
        /** @brief When the reading was taken, in nanoseconds since an arbitrary epoch. */
        std::uint64_t timestampNanoseconds = 0;
    };

    /** @brief Which sensor a reading came from. */
    enum class SensorKind
    {
        /** @brief Linear acceleration, including gravity, in m/s². */
        Accelerometer,
        /** @brief Angular velocity in radians per second. */
        Gyroscope
    };

    /**
     * @brief Reads device motion sensors.
     *
     * Backs `Microsoft::Devices::Sensors`. Subsystem acquisition here is refcounted and shares
     * the process-wide ordering documented in docs/platform-sdl-lifecycle-audit.md; an
     * implementation must not route these calls through a main-thread dispatch, because CNA
     * supports sensor use with no event loop running at all.
     */
    class IPlatformSensors
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformSensors() = default;

        /**
         * @brief Gets whether a sensor is present.
         *
         * @param kind Which sensor.
         * @return True if the device exposes it.
         */
        [[nodiscard]] virtual bool IsAvailable(SensorKind kind) const = 0;

        /**
         * @brief Begins delivering readings for a sensor.
         *
         * @param kind Which sensor.
         * @throws PlatformNotSupportedException If the platform reports no `Sensors` capability.
         * @throws PlatformException If the sensor exists but could not be opened.
         */
        virtual void Start(SensorKind kind) = 0;

        /**
         * @brief Stops delivering readings for a sensor.
         *
         * @param kind Which sensor.
         */
        virtual void Stop(SensorKind kind) = 0;

        /**
         * @brief Gets the most recent reading.
         *
         * @param kind Which sensor.
         * @param reading Receives the reading; untouched when this returns false.
         * @return True if a reading was available.
         */
        [[nodiscard]] virtual bool TryGetReading(SensorKind kind, SensorReading& reading) const = 0;
    };

} // namespace CNA::Platform
