// SPDX-License-Identifier: MS-PL

#pragma once

#include <functional>

#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Platform-native compass backend interface, per `docs/devices-native-backend-design.md`.
     *
     * `Compass` selects a concrete implementation (e.g. `AndroidCompassBackend`)
     * at construction time via a compile-time platform switch; on any
     * platform without one, `Compass` holds no backend at all and keeps its
     * permanent `NotSupported`/throws-`SensorFailedException` stub behavior
     * unchanged (`docs/devices-native-backend-design.md`'s migration plan,
     * step 3). This interface itself has no platform-specific `#include` —
     * it compiles and is mockable on every platform.
     */
    class ICompassBackend
    {
    public:
        using ReadingCallback = std::function<void(const CompassReading&)>;
        using CalibrationCallback = std::function<void()>;

        virtual ~ICompassBackend() = default;

        /**
         * @brief Cheap probe: true if a real compass is available on this device.
         *
         * Does not start delivery, and does not hold any resource open as a
         * side effect of probing.
         */
        [[nodiscard]] virtual bool IsSupported() = 0;

        /**
         * @brief Starts delivering readings.
         *
         * @param timeBetweenUpdates Requested sample interval.
         * @param onReading Invoked once per fused reading; implementations
         * may call this from a background thread — callers must treat it as
         * running on an unknown thread, same as
         * Accelerometer/Gyroscope's existing CurrentValueChanged contract.
         * @param onCalibrationNeeded Invoked when the backend detects the
         * compass needs calibration (e.g. an accuracy drop).
         * @return true if delivery actually started; false if unsupported
         * or if delivery could not actually be started (e.g. the platform
         * sensor queue failed to initialize) — implementations must not
         * report success optimistically before delivery has genuinely
         * begun. Calling Start() while already started is implementation-
         * defined but must never crash or corrupt state; `Compass` itself
         * guards against calling this twice (see `Compass::Start()`).
         */
        virtual bool Start(
            const System::TimeSpan& timeBetweenUpdates,
            ReadingCallback onReading,
            CalibrationCallback onCalibrationNeeded) = 0;

        /** @brief Stops delivery. Safe to call even if never started. */
        virtual void Stop() = 0;
    };
} // namespace Microsoft::Devices::Sensors::Detail
