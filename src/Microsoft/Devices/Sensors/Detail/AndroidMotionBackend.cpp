// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp"

#ifdef __ANDROID__

#include <android/sensor.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector3;

    AndroidMotionBackend::AndroidMotionBackend()
        : rotationVectorBridge_(ASENSOR_TYPE_ROTATION_VECTOR),
          gameRotationVectorBridge_(ASENSOR_TYPE_GAME_ROTATION_VECTOR),
          gravityBridge_(ASENSOR_TYPE_GRAVITY),
          linearAccelerationBridge_(ASENSOR_TYPE_LINEAR_ACCELERATION),
          gyroscopeBridge_(ASENSOR_TYPE_GYROSCOPE)
    {
    }

    AndroidMotionBackend::~AndroidMotionBackend()
    {
        Stop();
    }

    bool AndroidMotionBackend::IsSupported()
    {
        const bool hasAttitudeSource = rotationVectorBridge_.IsAvailable() || gameRotationVectorBridge_.IsAvailable();
        return hasAttitudeSource
            && gravityBridge_.IsAvailable()
            && linearAccelerationBridge_.IsAvailable()
            && gyroscopeBridge_.IsAvailable();
    }

    bool AndroidMotionBackend::Start(const System::TimeSpan& timeBetweenUpdates, ReadingCallback onReading)
    {
        if (!IsSupported())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            onReading_ = std::move(onReading);
            hasAttitudeSample_ = false;
            hasGravitySample_ = false;
            hasLinearAccelerationSample_ = false;
            hasGyroscopeSample_ = false;
        }

        // Prefer the plain rotation vector (true-north reference); fall
        // back to the game rotation vector only if the plain one isn't
        // available on this device (Task DEVICES-0104).
        bool attitudeStarted;
        if (rotationVectorBridge_.IsAvailable())
        {
            usingGameRotationVector_ = false;
            attitudeStarted = rotationVectorBridge_.Start(
                timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleAttitudeSample(sample); });
        }
        else
        {
            usingGameRotationVector_ = true;
            attitudeStarted = gameRotationVectorBridge_.Start(
                timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleAttitudeSample(sample); });
        }

        const bool gravityStarted = gravityBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleGravitySample(sample); });
        const bool linearAccelerationStarted = linearAccelerationBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleLinearAccelerationSample(sample); });
        const bool gyroscopeStarted = gyroscopeBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleGyroscopeSample(sample); });

        if (!attitudeStarted || !gravityStarted || !linearAccelerationStarted || !gyroscopeStarted)
        {
            Stop();
            return false;
        }

        return true;
    }

    void AndroidMotionBackend::Stop()
    {
        rotationVectorBridge_.Stop();
        gameRotationVectorBridge_.Stop();
        gravityBridge_.Stop();
        linearAccelerationBridge_.Stop();
        gyroscopeBridge_.Stop();
    }

    void AndroidMotionBackend::SetSampleInterval(const System::TimeSpan& timeBetweenUpdates)
    {
        // All five bridges — AndroidSensorBridge::SetSampleInterval() itself
        // is a safe no-op on whichever ones, if any, are not currently
        // started (Task ANDROID-BRIDGE-002). Simpler and equally correct to
        // call it on both attitude bridges unconditionally rather than
        // checking usingGameRotationVector_ first — only the one Start()
        // actually started will do anything.
        rotationVectorBridge_.SetSampleInterval(timeBetweenUpdates);
        gameRotationVectorBridge_.SetSampleInterval(timeBetweenUpdates);
        gravityBridge_.SetSampleInterval(timeBetweenUpdates);
        linearAccelerationBridge_.SetSampleInterval(timeBetweenUpdates);
        gyroscopeBridge_.SetSampleInterval(timeBetweenUpdates);
    }

    void AndroidMotionBackend::HandleAttitudeSample(const AndroidSensorSample& sample)
    {
        // Always the OS-fused rotation vector / game rotation vector output
        // -- never raw gyroscope integration or accelerometer+magnetometer
        // cross-product math computed in this bridge (Task DEVICES-0119).
        const Quaternion quaternion = ConvertRotationVectorToXnaQuaternion(
            sample.Values[0], sample.Values[1], sample.Values[2], sample.Values[3]);
        const Matrix rotationMatrix = Matrix::CreateFromQuaternion(quaternion);

        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        ExtractYawPitchRollFromQuaternion(quaternion, yaw, pitch, roll);

        const AttitudeReading attitude(pitch, roll, yaw, quaternion, rotationMatrix, sample.Timestamp);

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            attitude_ = attitude;
            hasAttitudeSample_ = true;
        }
        PublishReading();
    }

    namespace
    {
        // Matches Accelerometer.cpp's identical constant/conversion (Task
        // DEVICES-0063, re-verified MOTION-003 2026-07-06): Android's
        // TYPE_GRAVITY/TYPE_LINEAR_ACCELERATION sensors report m/s^2 (same
        // NDK convention as TYPE_ACCELEROMETER, confirmed ACCEL-003), while
        // the real WP7 MotionReading.DeviceAcceleration is documented "in
        // gravitational units" (archived MSDN hh220832(v=vs.105)) and the
        // companion "How to use the combined Motion API" walkthrough
        // (hh202984(v=vs.105)) confirms DeviceAcceleration is gravity-filtered
        // ("Unlike the Accelerometer API, the acceleration of gravity is
        // filtered out of the reading so that when the device is still, the
        // acceleration is zero along all axes" -- matching
        // TYPE_LINEAR_ACCELERATION's own NDK semantics exactly, as opposed to
        // TYPE_ACCELEROMETER's raw, gravity-inclusive reading). Gravity's own
        // dedicated MSDN page (hh203234) does not state its unit as explicitly,
        // but the same g-unit convention is the only one consistent with
        // DeviceAcceleration's documented unit and with a physically sensible
        // "vector of magnitude ~1 at rest" reading.
        constexpr float StandardGravity = 9.80665f;
    }

    void AndroidMotionBackend::HandleGravitySample(const AndroidSensorSample& sample)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            gravity_ = Vector3(
                sample.Values[0] / StandardGravity,
                sample.Values[1] / StandardGravity,
                sample.Values[2] / StandardGravity);
            hasGravitySample_ = true;
        }
        PublishReading();
    }

    void AndroidMotionBackend::HandleLinearAccelerationSample(const AndroidSensorSample& sample)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            deviceAcceleration_ = Vector3(
                sample.Values[0] / StandardGravity,
                sample.Values[1] / StandardGravity,
                sample.Values[2] / StandardGravity);
            hasLinearAccelerationSample_ = true;
        }
        PublishReading();
    }

    // Task MOTION-004 (re-verified 2026-07-06): no unit conversion here,
    // deliberately -- Android's ASENSOR_TYPE_GYROSCOPE reports radians/second
    // (same NDK convention already confirmed for the plain Gyroscope class,
    // GYRO-002), and the real WP7 MotionReading.DeviceRotationRate is
    // documented identically: "Gets the rotational velocity of the device,
    // in radians per second" (archived MSDN hh312728(v=vs.105)). Both sides
    // already agree, so a straight pass-through is correct.
    void AndroidMotionBackend::HandleGyroscopeSample(const AndroidSensorSample& sample)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            deviceRotationRate_ = Vector3(sample.Values[0], sample.Values[1], sample.Values[2]);
            hasGyroscopeSample_ = true;
        }
        PublishReading();
    }

    void AndroidMotionBackend::PublishReading()
    {
        ReadingCallback callback;
        MotionReading reading;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            // Only publish once all four sources have delivered at least
            // one sample -- a reading built from a subset would be
            // half-default data, not a real fused reading.
            if (!hasAttitudeSample_ || !hasGravitySample_ || !hasLinearAccelerationSample_ || !hasGyroscopeSample_)
            {
                return;
            }

            callback = onReading_;

            // Task MOTION-006 (2026-07-06): the fused MotionReading's own
            // Timestamp is deliberately set to the *same* value as its
            // nested Attitude.Timestamp, not a fresh getUtcNowProperty()
            // call -- PublishReading() only runs once all four sources
            // have delivered at least one sample, which can be strictly
            // later than when the attitude sample itself arrived (each
            // source has its own independent sample rate). Calling
            // getUtcNowProperty() here previously produced two different
            // values both claiming to represent "now" for the same fused
            // reading: MotionReading.Timestamp (publish time) vs.
            // MotionReading.Attitude.Timestamp (attitude sample's own
            // arrival time, set in HandleAttitudeSample() from
            // AndroidSensorSample::Timestamp, itself already wall-clock --
            // see that struct's own doc comment). Anchoring both to the
            // attitude sample's timestamp keeps the fused reading
            // internally consistent by construction, and treats Attitude
            // -- the class's own headline value, per Motion's class
            // remarks -- as the reading's canonical "as of" time.
            reading = MotionReading(
                attitude_, deviceAcceleration_, deviceRotationRate_, gravity_,
                attitude_.getTimestampProperty());
        }

        if (callback)
        {
            callback(reading);
        }
    }
} // namespace Microsoft::Devices::Sensors::Detail

#endif // __ANDROID__
