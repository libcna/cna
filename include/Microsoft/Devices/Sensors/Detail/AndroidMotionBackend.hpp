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
     */
    class AndroidMotionBackend final : public IMotionBackend
    {
    public:
        AndroidMotionBackend();
        ~AndroidMotionBackend() override;

        AndroidMotionBackend(const AndroidMotionBackend&) = delete;
        AndroidMotionBackend& operator=(const AndroidMotionBackend&) = delete;

        [[nodiscard]] bool IsSupported() override;

        bool Start(const System::TimeSpan& timeBetweenUpdates, ReadingCallback onReading) override;

        void Stop() override;

        void SetSampleInterval(const System::TimeSpan& timeBetweenUpdates) override;

    private:
        void HandleAttitudeSample(const AndroidSensorSample& sample);
        void HandleGravitySample(const AndroidSensorSample& sample);
        void HandleLinearAccelerationSample(const AndroidSensorSample& sample);
        void HandleGyroscopeSample(const AndroidSensorSample& sample);
        void PublishReading();

        AndroidSensorBridge rotationVectorBridge_;
        AndroidSensorBridge gameRotationVectorBridge_;
        AndroidSensorBridge gravityBridge_;
        AndroidSensorBridge linearAccelerationBridge_;
        AndroidSensorBridge gyroscopeBridge_;

        /** @brief Set once Start() picks which of rotationVectorBridge_/gameRotationVectorBridge_ is actually in use. */
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

        ReadingCallback onReading_;
    };
} // namespace Microsoft::Devices::Sensors::Detail

#endif // __ANDROID__
