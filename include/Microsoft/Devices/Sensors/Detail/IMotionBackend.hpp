// SPDX-License-Identifier: MS-PL

#pragma once

#include <functional>

#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Platform-native motion backend interface, per `docs/devices-native-backend-design.md`.
     *
     * `Motion` selects a concrete implementation (e.g. `AndroidMotionBackend`)
     * at construction time via a compile-time platform switch; on any
     * platform without one, `Motion` holds no backend at all and keeps its
     * permanent `NotSupported`/throws-`SensorFailedException` stub behavior
     * unchanged. This interface itself has no platform-specific `#include`
     * — it compiles and is mockable on every platform. Mirrors
     * `ICompassBackend`'s shape for architectural symmetry (per the design
     * doc), minus a calibration callback — `Motion` shares `Compass`'s
     * `Calibrate` event conceptually, but this backend interface only ever
     * needs to report readings; `AndroidMotionBackend` does not itself
     * raise calibration (see `docs/devices-native-backend-design.md`).
     */
    class IMotionBackend
    {
    public:
        using ReadingCallback = std::function<void(const MotionReading&)>;

        virtual ~IMotionBackend() = default;

        /**
         * @brief Cheap probe: true if real fused motion sensing is available on this device.
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
         * @return true if delivery actually started; false if unsupported
         * or if delivery could not actually be started (e.g. the platform
         * sensor queue failed to initialize) — implementations must not
         * report success optimistically before delivery has genuinely
         * begun. Calling Start() while already started is implementation-
         * defined but must never crash or corrupt state; `Motion` itself
         * guards against calling this twice (see `Motion::Start()`).
         */
        virtual bool Start(const System::TimeSpan& timeBetweenUpdates, ReadingCallback onReading) = 0;

        /** @brief Stops delivery. Safe to call even if never started. */
        virtual void Stop() = 0;

        /**
         * @brief Changes the sample interval on an already-started backend, without requiring Stop()/Start() (Task ANDROID-BRIDGE-002).
         *
         * A safe no-op if this backend is not currently started.
         *
         * @param timeBetweenUpdates New requested sample interval.
         */
        virtual void SetSampleInterval(const System::TimeSpan& timeBetweenUpdates) = 0;
    };
} // namespace Microsoft::Devices::Sensors::Detail
