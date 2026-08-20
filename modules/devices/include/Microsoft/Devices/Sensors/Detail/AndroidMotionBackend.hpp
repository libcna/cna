// SPDX-License-Identifier: MS-PL

#pragma once

#ifdef __ANDROID__

#include <mutex>

#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp"
#include "Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Android native `Motion` backend: fused rotation vector for `Attitude`, plus `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION`/`TYPE_GYROSCOPE`.
     *
     * Entirely `#ifdef __ANDROID__`-gated (both this header and its `.cpp`),
     * same discipline as `Detail::AndroidCompassBackend` — only ever
     * referenced from `Motion.cpp`'s own `#if defined(__ANDROID__)` block.
     *
     * Prefers `ASENSOR_TYPE_ROTATION_VECTOR` (has a true-north reference);
     * falls back to `ASENSOR_TYPE_GAME_ROTATION_VECTOR` (gyroscope+
     * accelerometer only, no magnetometer coupling, but drifts in yaw over
     * time with no correction — see this class's own `.cpp` doc comment and
     * `docs/devices-native-backend-design.md`'s drift note) if the plain
     * rotation vector sensor is unavailable on this device.
     *
     * `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION` are Android's own already
     * gravity/acceleration-split virtual sensors — no manual high-pass/
     * low-pass filtering is performed in this bridge (matching
     * `docs/devices-native-backend-design.md`'s Android Motion section).
     * `TYPE_GYROSCOPE` here is this backend's own, independent
     * registration — separate from any live `Gyroscope` C++ instance;
     * Android allows multiple listeners on the same physical sensor, and
     * this codebase's own instance-counting (`Gyroscope::MaxSensorCount`)
     * is per-class, so the two do not conflict (Task DEVICES-0110).
     *
     * Never performs manual sensor fusion (integrating raw gyroscope data,
     * combining raw accelerometer+magnetometer) in this bridge — always
     * consumes Android's own OS-fused rotation-vector/game-rotation-vector
     * output directly (Task DEVICES-0119's gate).
     *
     * Task MOTION-011 (2026-07-16): also independently registers
     * `TYPE_MAGNETIC_FIELD`, purely to monitor its accuracy status and
     * drive `IMotionBackend::CalibrationCallback` — reusing
     * `Detail::ShouldRaiseCalibrateForAccuracyStatus()`, the same accuracy
     * policy `AndroidCompassBackend` already uses. This is best-effort and
     * optional: `IsSupported()`/`Start()`'s overall success never depends
     * on the magnetic-field sensor being available, since `MotionReading`
     * has no magnetometer field to expose and calibration is an additional
     * signal, not part of Motion's core reading data.
     */
    class AndroidMotionBackend final : public IMotionBackend
    {
    public:
        AndroidMotionBackend();
        ~AndroidMotionBackend() override;

        AndroidMotionBackend(const AndroidMotionBackend&) = delete;
        AndroidMotionBackend& operator=(const AndroidMotionBackend&) = delete;

        [[nodiscard]] bool IsSupported() override;

        bool Start(
            const System::TimeSpan& timeBetweenUpdates,
            ReadingCallback onReading,
            CalibrationCallback onCalibrationNeeded) override;

        void Stop() override;

        void SetSampleInterval(const System::TimeSpan& timeBetweenUpdates) override;

        [[nodiscard]] bool IsUsingNorthReferencedAttitudeSource() override;

        /**
         * @brief Test-only hook (Task MOT2-003): total fused frames dropped for exceeding `MaxFusionAgeWindow`.
         *
         * @return The number of times `PublishReading()` has skipped
         * publishing because the four fused sources' most recent samples
         * spanned more than `MaxFusionAgeWindow`.
         */
        [[nodiscard]] int GetDroppedFusionFrameCountForTesting();

    private:
        void HandleAttitudeSample(const AndroidSensorSample& sample);
        void HandleGravitySample(const AndroidSensorSample& sample);
        void HandleLinearAccelerationSample(const AndroidSensorSample& sample);
        void HandleGyroscopeSample(const AndroidSensorSample& sample);
        void HandleMagneticFieldSample(const AndroidSensorSample& sample);
        void PublishReading();

        AndroidSensorBridge rotationVectorBridge_;
        AndroidSensorBridge gameRotationVectorBridge_;
        AndroidSensorBridge gravityBridge_;
        AndroidSensorBridge linearAccelerationBridge_;
        AndroidSensorBridge gyroscopeBridge_;

        /** @brief Task MOTION-011: calibration-only -- never exposed through MotionReading, see this class's own doc comment. */
        AndroidSensorBridge magneticFieldBridge_;

        /**
         * @brief Set once Start() picks which of rotationVectorBridge_/gameRotationVectorBridge_ is actually in use.
         *
         * Task MOT2-005 (2026-07-17, external audit
         * `audit_devices_2026-07-17.md`): now read by
         * `IsUsingNorthReferencedAttitudeSource()`, so — unlike before this
         * task, when nothing ever read it and an unsynchronized write from
         * `Start()` was harmless — this is now guarded by `stateMutex_`
         * (`Start()` computes the value locally first, then stores it under
         * the lock, matching this class's own established discipline for
         * every other field a public method can read from another thread).
         */
        bool usingGameRotationVector_ = false;

        std::mutex stateMutex_;
        AttitudeReading attitude_;
        Microsoft::Xna::Framework::Vector3 gravity_;
        Microsoft::Xna::Framework::Vector3 deviceAcceleration_;
        Microsoft::Xna::Framework::Vector3 deviceRotationRate_;
        bool hasAttitudeSample_ = false;
        bool hasGravitySample_ = false;
        bool hasLinearAccelerationSample_ = false;
        bool hasGyroscopeSample_ = false;

        /**
         * @brief Wall-clock arrival time of each source's most recent sample (Task MOTION-007).
         *
         * `attitude_.getTimestampProperty()` already tracks the attitude
         * source's own timestamp (`MOTION-006`) — these three cover the
         * other three sources, so `PublishReading()` can confirm all four
         * are recent relative to each other before fusing them, instead of
         * only checking that each has delivered *some* sample, ever.
         */
        System::DateTimeOffset gravityTimestamp_;
        System::DateTimeOffset linearAccelerationTimestamp_;
        System::DateTimeOffset gyroscopeTimestamp_;

        /**
         * @brief Maximum age gap allowed between the four sources' most recent samples for a fused reading to publish (Task MOTION-007).
         *
         * Deliberately generous (not a tight frame-sync budget): this
         * exists to catch a source that has stopped delivering samples
         * entirely (e.g. a sensor failure, or a permission/registration
         * problem affecting one of the five underlying bridges) while the
         * other three keep going — not to enforce sub-frame synchronization
         * between four independently-rated physical sensors, which
         * legitimately deliver samples at different times even in normal,
         * healthy operation.
         */
        static const System::TimeSpan MaxFusionAgeWindow;

        /**
         * @brief Test-only hook (Task MOT2-003): total fused frames dropped for exceeding `MaxFusionAgeWindow`.
         *
         * Satisfies the required work's "expose counters" bullet for the
         * drop path that already existed (`MOTION-007`) before this task —
         * see this class's own `.cpp` `PublishReading()` and
         * `plans/plan_devices.md`'s `MOT2-003` resolution note for what this task
         * does and, just as importantly, does *not* yet implement (bounded
         * per-source sample queues and nearest/interpolated selection
         * within a tight, hardware-measured skew — deliberately deferred as
         * its own, larger design task, not rushed here). Guarded by
         * `stateMutex_`, same as every other field in this class.
         */
        int droppedFusionFrameCountForTesting_ = 0;

        ReadingCallback onReading_;

        /** @brief Task MOTION-011: invoked from HandleMagneticFieldSample(), guarded by stateMutex_ same as onReading_. */
        CalibrationCallback onCalibrationNeeded_;
    };
} // namespace Microsoft::Devices::Sensors::Detail

#endif // __ANDROID__
