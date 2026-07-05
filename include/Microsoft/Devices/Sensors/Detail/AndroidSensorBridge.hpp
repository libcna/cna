// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "System/DateTimeOffset.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Converts a requested update interval to ASensorEventQueue_setEventRate()'s microsecond parameter.
     *
     * Pure function (mirrors Detail::ConvertAndroidPortraitToXnaLandscape()'s
     * precedent in AndroidSensorOrientation.hpp): testable on any platform
     * without needing a real Android sensor queue. Floors at 1 microsecond
     * — a non-positive or sub-microsecond requested interval is clamped up
     * rather than passed through as 0 or negative, which the NDK does not
     * document a defined meaning for.
     *
     * @param timeBetweenUpdates Requested update interval.
     * @return Equivalent microsecond value, at least 1.
     */
    [[nodiscard]] inline std::int32_t ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(
        const System::TimeSpan& timeBetweenUpdates)
    {
        const double requestedMicroseconds = timeBetweenUpdates.getTotalMillisecondsProperty() * 1000.0;
        return requestedMicroseconds > 1.0 ? static_cast<std::int32_t>(requestedMicroseconds) : 1;
    }

    /**
     * One raw Android NDK sensor sample. Already stamped with real
     * wall-clock time, not the sensor's own monotonic boot-time timestamp —
     * matches the precedent Accelerometer/Gyroscope already established
     * (Task P4-7): ASensorEvent::timestamp is nanoseconds since boot, not
     * suitable for a WP7-style DateTimeOffset reading timestamp.
     */
    struct AndroidSensorSample
    {
        /** @brief Raw sensor values, in Android's own units/axis convention, unconverted. Mirrors ASensorEvent::data (up to 16 floats). */
        float Values[16] = {};

        /** @brief Number of valid entries in Values (e.g. 3 for a vector sensor, up to 5 for a rotation vector with accuracy). */
        int ValueCount = 0;

        /**
         * @brief Accuracy status, for sensor types that report one via `ASensorVector::status` (e.g. magnetic field, accelerometer, gyroscope).
         *
         * Numerically matches the NDK's `ASENSOR_STATUS_*` constants. This
         * is a distinct field, not part of Values — `ASensorVector::status`
         * occupies the same union memory as `Values[3]` would for a 4-float
         * interpretation (it is a byte-sized field packed after the 3
         * vector floats, not a 4th float), so it cannot be read correctly
         * through `Values` alone. Meaningless (left at its default, 0) for
         * sensor types that don't report a `ASensorVector`-shaped event
         * (e.g. rotation vector, which uses the plain `data[]` float form
         * instead).
         */
        int Status = 0;

        /** @brief Wall-clock time this sample was delivered. */
        System::DateTimeOffset Timestamp;
    };

    /**
     * @brief Shared Android-only bridge to the NDK's ASensorManager/ASensorEventQueue API, for exactly one Android sensor type.
     *
     * Delivers samples via callback on its own dedicated background thread.
     * An ALooper is thread-affine (must be ALooper_prepare()'d on the same
     * thread that later polls it), so this bridge owns and pumps its own
     * looper internally on a private worker thread, rather than requiring
     * CNA's game loop to pump anything — this keeps the eventual
     * Compass/Motion Android backends' threading model consistent with
     * Accelerometer/Gyroscope's existing SDL-callback-thread model (a game
     * subscribing to CurrentValueChanged must already treat the handler as
     * running on an unknown thread; see AUDIT.md's "Event-thread model"
     * note).
     *
     * This is CNA-internal plumbing shared by Compass/Motion's future
     * native Android backends (`plan_devices.md` Phase 7/8) — not itself an
     * XNA-facing sensor class, and not wired into Compass/Motion by this
     * class alone (see AndroidCompassBackend/AndroidMotionBackend).
     *
     * On any non-Android platform, every method is a safe, inert no-op
     * (IsAvailable() always false, Start() always returns false) — this
     * header has no Android-only #include, so it compiles cleanly on every
     * platform, matching the discipline Accelerometer.hpp/Gyroscope.hpp
     * already established for keeping SDL/platform headers out of public
     * headers.
     */
    class AndroidSensorBridge final
    {
    public:
        using SampleCallback = std::function<void(const AndroidSensorSample&)>;

        /**
         * @param androidSensorType One of the NDK's ASENSOR_TYPE_* constants
         * (e.g. ASENSOR_TYPE_MAGNETIC_FIELD), passed as a plain int so this
         * header never needs to include <android/sensor.h> — mirrors
         * Accelerometer::GetSdlSensorType()'s identical discipline for
         * SDL_SensorType.
         */
        explicit AndroidSensorBridge(int androidSensorType);

        /** @brief Stops delivery (if started) and destroys the bridge. */
        ~AndroidSensorBridge();

        AndroidSensorBridge(const AndroidSensorBridge&) = delete;
        AndroidSensorBridge& operator=(const AndroidSensorBridge&) = delete;
        AndroidSensorBridge(AndroidSensorBridge&&) = delete;
        AndroidSensorBridge& operator=(AndroidSensorBridge&&) = delete;

        /**
         * @brief Cheap probe: true if this sensor type exists on this device.
         *
         * Does not start delivery, and does not hold any resource open as a
         * side effect of probing (same discipline as
         * VibrateController::getIsSupportedProperty()).
         *
         * @return true if a sensor of this type is available; otherwise false.
         */
        [[nodiscard]] bool IsAvailable() const;

        /**
         * @brief Starts delivering samples on a dedicated background thread.
         *
         * @param timeBetweenUpdates Requested sample interval, mapped to
         * ASensorEventQueue_setEventRate()'s microsecond parameter. Applied
         * once at Start() time — changing it later has no effect until the
         * next Start() call. Note: Android 12+ (API 31+) restricts
         * high-frequency sensor sampling (rates faster than ~200Hz) unless
         * the app declares the `HIGH_SAMPLING_RATE_SENSORS` permission —
         * requesting a very small `timeBetweenUpdates` on such a device may
         * silently be capped by the OS to a slower effective rate than
         * requested; this bridge does not detect or compensate for that.
         * @param callback Invoked once per sample, on this bridge's own
         * background thread — never the calling thread. Must not throw;
         * an exception escaping this callback is not handled here (unlike
         * Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()'s
         * documented per-instance exception-swallowing policy) — wiring
         * that same policy in is the responsibility of whichever
         * Compass/Motion backend (Phase 7/8) supplies this callback.
         * @return true if sensor delivery actually started; false if this
         * sensor type is unavailable on this device.
         */
        bool Start(const System::TimeSpan& timeBetweenUpdates, SampleCallback callback);

        /**
         * @brief Stops delivery and joins the background thread.
         *
         * Safe to call even if Start() was never called, or was already
         * stopped. Calling this reentrantly from within this bridge's own
         * callback (on its own worker thread) does not deadlock — see this
         * method's .cpp implementation for the accepted boundary this
         * still leaves (detaches rather than joins in that one case,
         * mirroring Accelerometer's own documented "destroying from within
         * your own callback" limitation rather than fully solving it).
         */
        void Stop();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace Microsoft::Devices::Sensors::Detail
