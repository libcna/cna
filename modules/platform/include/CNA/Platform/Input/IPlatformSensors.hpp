// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

    /**
     * @brief Which sensor a reading came from.
     *
     * The sided variants exist because dual-sensor devices are real — a controller with a motion
     * unit in each half reports two accelerometers, and collapsing them would make one of the two
     * unreachable. `Unknown` covers a sensor the platform enumerates but cannot classify, which
     * is a normal answer rather than an error: the device is there, and a caller listing sensors
     * should see it.
     */
    enum class SensorKind
    {
        /** @brief The platform enumerated a sensor but could not classify it. */
        Unknown,
        /** @brief Linear acceleration, including gravity, in m/s². */
        Accelerometer,
        /** @brief Angular velocity in radians per second. */
        Gyroscope,
        /** @brief Left-side accelerometer on a dual-sensor device. */
        AccelerometerLeft,
        /** @brief Left-side gyroscope on a dual-sensor device. */
        GyroscopeLeft,
        /** @brief Right-side accelerometer on a dual-sensor device. */
        AccelerometerRight,
        /** @brief Right-side gyroscope on a dual-sensor device. */
        GyroscopeRight
    };

    /** @brief Current clockwise display rotation relative to the device's natural orientation. */
    enum class SensorDisplayRotation
    {
        /** @brief The platform cannot report the rotation. */
        Unknown,
        /** @brief Natural orientation. */
        Degrees0,
        /** @brief Rotated clockwise by 90 degrees. */
        Degrees90,
        /** @brief Rotated clockwise by 180 degrees. */
        Degrees180,
        /** @brief Rotated clockwise by 270 degrees. */
        Degrees270
    };

    /** @brief One enumerated sensor. */
    struct SensorInfo
    {
        /** @brief Identifies the sensor while it stays connected; matches `DeviceEvent::device`. */
        std::uint64_t id = 0;
        /** @brief What the sensor measures. */
        SensorKind kind = SensorKind::Unknown;
        /** @brief The sensor's human-readable name, or empty when the platform reports none. */
        std::string name;
    };

    /** @brief Callback invoked when an opened sensor publishes a new reading. */
    using SensorReadingCallback = std::function<void(const SensorReading&)>;

    /** @brief One independently owned sensor stream. */
    class IPlatformSensorSession
    {
    public:
        /** @brief Stops delivery and closes the native sensor. */
        virtual ~IPlatformSensorSession() = default;

        /**
         * @brief Gets the most recent reading without blocking.
         * @param reading Receives the reading; untouched when unavailable.
         * @return True when the session has a current reading.
         */
        [[nodiscard]] virtual bool TryGetReading(SensorReading& reading) const = 0;
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
         * @brief Lists every sensor the device exposes.
         *
         * Separate from `IPlatformInputDevices::GetDevices(Sensor)`, which reports id and name
         * only: what a caller enumerating sensors actually needs is *what each one measures*, and
         * that vocabulary lives here.
         *
         * @return The attached sensors, in no guaranteed order; empty when none are present.
         */
        [[nodiscard]] virtual std::vector<SensorInfo> GetSensors() const = 0;

        /**
         * @brief Gets whether a sensor is present.
         *
         * @param kind Which sensor.
         * @return True if the device exposes it.
         */
        [[nodiscard]] virtual bool IsAvailable(SensorKind kind) const = 0;

        /**
         * @brief Gets current display rotation for consumers that expose display-relative axes.
         * @return The rotation, or Unknown when it cannot be queried.
         */
        [[nodiscard]] virtual SensorDisplayRotation GetDisplayRotation() const = 0;

        /**
         * @brief Opens an independent stream for the first sensor of a kind.
         *
         * The optional callback may run on a platform-owned thread. Destroying the returned
         * session is a barrier: after its destructor returns, callbacks on other threads are no
         * longer running and no new callback will be invoked. Reentrant destruction by the
         * callback itself cannot wait for its own stack frame; that frame may finish normally but
         * must not invoke the callback again. The caller must keep the sensor subsystem acquired
         * until after the session is destroyed.
         *
         * @param kind Which sensor.
         * @param callback Callback for new readings, or empty for polling only.
         * @return A stream, or null when no matching device can be opened.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformSensorSession> OpenSensor(
            SensorKind kind, SensorReadingCallback callback) = 0;

        /**
         * @brief Begins delivering readings for a sensor.
         *
         * Starting an already-started sensor is a **no-op, not an error**. It is deliberately not
         * promised to be *free*, though: an implementation may legitimately re-open the device,
         * and some sensors take a noticeable moment to begin streaming. A caller that reads every
         * frame should therefore track its own started state rather than starting each time.
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
