// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp"

#ifdef __ANDROID__

#include <algorithm>

#include <android/sensor.h>

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp"
#include "Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector3;

    // Task MOTION-007: 500ms is generous relative to any of this project's
    // actual TimeBetweenUpdates values (default 2ms), deliberately -- this
    // exists to catch a source that has stopped delivering samples
    // entirely, not to enforce tight synchronization between four
    // independently-rated physical sensors.
    const System::TimeSpan AndroidMotionBackend::MaxFusionAgeWindow = System::TimeSpan::FromMilliseconds(500);

    AndroidMotionBackend::AndroidMotionBackend()
        : rotationVectorBridge_(ASENSOR_TYPE_ROTATION_VECTOR),
          gameRotationVectorBridge_(ASENSOR_TYPE_GAME_ROTATION_VECTOR),
          gravityBridge_(ASENSOR_TYPE_GRAVITY),
          linearAccelerationBridge_(ASENSOR_TYPE_LINEAR_ACCELERATION),
          gyroscopeBridge_(ASENSOR_TYPE_GYROSCOPE),
          magneticFieldBridge_(ASENSOR_TYPE_MAGNETIC_FIELD)
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

    bool AndroidMotionBackend::Start(
        const System::TimeSpan& timeBetweenUpdates,
        ReadingCallback onReading,
        CalibrationCallback onCalibrationNeeded)
    {
        if (!IsSupported())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            onReading_ = std::move(onReading);
            onCalibrationNeeded_ = std::move(onCalibrationNeeded);
            hasAttitudeSample_ = false;
            hasGravitySample_ = false;
            hasLinearAccelerationSample_ = false;
            hasGyroscopeSample_ = false;
        }

        // Prefer the plain rotation vector (true-north reference); fall
        // back to the game rotation vector only if the plain one isn't
        // available on this device (Task DEVICES-0104).
        bool attitudeStarted;
        bool usingGameRotationVector;
        if (rotationVectorBridge_.IsAvailable())
        {
            usingGameRotationVector = false;
            attitudeStarted = rotationVectorBridge_.Start(
                timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleAttitudeSample(sample); });
        }
        else
        {
            usingGameRotationVector = true;
            attitudeStarted = gameRotationVectorBridge_.Start(
                timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleAttitudeSample(sample); });
        }

        // Task MOT2-005: stored under stateMutex_ -- see usingGameRotationVector_'s
        // own doc comment for why this is no longer a bare, unsynchronized write.
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            usingGameRotationVector_ = usingGameRotationVector;
        }

        const bool gravityStarted = gravityBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleGravitySample(sample); });
        const bool linearAccelerationStarted = linearAccelerationBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleLinearAccelerationSample(sample); });
        const bool gyroscopeStarted = gyroscopeBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleGyroscopeSample(sample); });

        // Task MOTION-011: best-effort, optional -- unlike the four bridges
        // above, a failure (or plain unavailability) here does not fail
        // this Start() call at all. MotionReading has no magnetometer field
        // to expose; this bridge exists purely to drive Calibrate, an
        // additional signal, not part of Motion's core reading data. Its
        // own return value is deliberately ignored.
        (void)magneticFieldBridge_.Start(
            timeBetweenUpdates, [this](const AndroidSensorSample& sample) { HandleMagneticFieldSample(sample); });

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
        magneticFieldBridge_.Stop();
    }

    void AndroidMotionBackend::SetSampleInterval(const System::TimeSpan& timeBetweenUpdates)
    {
        // All six bridges — AndroidSensorBridge::SetSampleInterval() itself
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
        magneticFieldBridge_.SetSampleInterval(timeBetweenUpdates);
    }

    int AndroidMotionBackend::GetDroppedFusionFrameCountForTesting()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return droppedFusionFrameCountForTesting_;
    }

    bool AndroidMotionBackend::IsUsingNorthReferencedAttitudeSource()
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return !usingGameRotationVector_;
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

        // Task MOTION-012 (2026-07-16): applies the exact same landscape
        // remap Accelerometer.cpp/Gyroscope.cpp already use to
        // Gravity/DeviceAcceleration/DeviceRotationRate, respecting the
        // shared ACCEL-008 opt-out. This was left unresolved by ACCEL-008
        // itself pending confirmation that Android's TYPE_GRAVITY/
        // TYPE_LINEAR_ACCELERATION/TYPE_GYROSCOPE report in the same raw,
        // device-fixed frame as TYPE_ACCELEROMETER -- now confirmed via
        // Android's own public developer documentation (not requiring
        // real hardware, since this is a documented OS API contract, not
        // an implementation detail that could vary by device):
        // developer.android.com/guide/topics/sensors/sensors_motion states,
        // verbatim, for both the gravity and linear-acceleration sensors,
        // "The sensor coordinate system is the same as the one used by the
        // acceleration sensor", and identically for the gyroscope, "The
        // sensor's coordinate system is the same as the one used for the
        // acceleration sensor." developer.android.com/guide/topics/sensors/
        // sensors_overview further confirms that shared coordinate system
        // "never changes as the device moves" -- i.e. it is fixed to the
        // device's natural orientation, never pre-corrected by the OS for
        // the current display rotation, so there is no risk of
        // double-applying an orientation correction the platform already
        // performed. `Motion.Attitude` (the quaternion) is explicitly
        // out of scope for this remap -- a quaternion is not a plain
        // vector, and any fix there is `MOTION-002`'s own open question.
        Vector3 ApplyLandscapeRemapIfEnabled(const Vector3& raw)
        {
            if (!IsAndroidLandscapeRemapEnabled())
            {
                return raw;
            }

            CNA::Platform::IPlatformSensors* sensors =
                CNA::Platform::GetCurrentPlatform().GetSensors();
            const CNA::Platform::SensorDisplayRotation rotation = sensors != nullptr
                ? sensors->GetDisplayRotation()
                : CNA::Platform::SensorDisplayRotation::Unknown;
            const AndroidSensorLandscapeOrientation mappedOrientation =
                (rotation == CNA::Platform::SensorDisplayRotation::Degrees270)
                    ? AndroidSensorLandscapeOrientation::Rotation270
                    : AndroidSensorLandscapeOrientation::Rotation90;

            return ConvertAndroidPortraitToXnaLandscape(raw.X, raw.Y, raw.Z, mappedOrientation);
        }
    }

    void AndroidMotionBackend::HandleGravitySample(const AndroidSensorSample& sample)
    {
        const Vector3 converted = ApplyLandscapeRemapIfEnabled(Vector3(
            sample.Values[0] / StandardGravity,
            sample.Values[1] / StandardGravity,
            sample.Values[2] / StandardGravity));

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            gravity_ = converted;
            gravityTimestamp_ = sample.Timestamp;
            hasGravitySample_ = true;
        }
        PublishReading();
    }

    void AndroidMotionBackend::HandleLinearAccelerationSample(const AndroidSensorSample& sample)
    {
        const Vector3 converted = ApplyLandscapeRemapIfEnabled(Vector3(
            sample.Values[0] / StandardGravity,
            sample.Values[1] / StandardGravity,
            sample.Values[2] / StandardGravity));

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            deviceAcceleration_ = converted;
            linearAccelerationTimestamp_ = sample.Timestamp;
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
    // already agree, so a straight pass-through is correct. Task MOTION-012:
    // the landscape remap is still applied (see ApplyLandscapeRemapIfEnabled()'s
    // own doc comment) -- units and axis convention are independent concerns.
    void AndroidMotionBackend::HandleGyroscopeSample(const AndroidSensorSample& sample)
    {
        const Vector3 converted =
            ApplyLandscapeRemapIfEnabled(Vector3(sample.Values[0], sample.Values[1], sample.Values[2]));

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            deviceRotationRate_ = converted;
            gyroscopeTimestamp_ = sample.Timestamp;
            hasGyroscopeSample_ = true;
        }
        PublishReading();
    }

    // Task MOTION-011 (2026-07-16): calibration-only -- reuses
    // AndroidCompassBackend's own accuracy-status policy
    // (ShouldRaiseCalibrateForAccuracyStatus()) so Motion.Calibrate fires
    // under the exact same condition Compass.Calibrate already does, rather
    // than inventing an independent threshold. Deliberately does not store
    // the magnetic-field vector or its accuracy anywhere -- MotionReading
    // has no field for either, and none is added here (out of scope for
    // this task, see AndroidMotionBackend.hpp's own doc comment).
    void AndroidMotionBackend::HandleMagneticFieldSample(const AndroidSensorSample& sample)
    {
        const auto status = static_cast<AndroidSensorAccuracyStatus>(sample.Status);

        CalibrationCallback calibrationCallback;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (ShouldRaiseCalibrateForAccuracyStatus(status))
            {
                calibrationCallback = onCalibrationNeeded_;
            }
        }

        if (calibrationCallback)
        {
            calibrationCallback();
        }
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

            // Task MOTION-007: also require the four sources' most recent
            // samples to be within MaxFusionAgeWindow of each other --
            // "each source has delivered at least one sample, ever" isn't
            // enough on its own; a source that stopped updating minutes ago
            // would otherwise keep getting fused with fresh data from the
            // other three forever. Drop-and-wait (return without
            // publishing) rather than publish-with-a-caveat: this backend
            // has no existing field to carry a "some data is stale" flag,
            // and inventing one would be a MotionReading shape change with
            // no real-API precedent, out of proportion to this specific
            // fix. The next sample from *any* source re-triggers this
            // check, so a fused reading still publishes as soon as all
            // four are recent again.
            const System::DateTimeOffset& attitudeTimestamp = attitude_.getTimestampProperty();
            const System::DateTimeOffset oldest = std::min(
                {attitudeTimestamp, gravityTimestamp_, linearAccelerationTimestamp_, gyroscopeTimestamp_});
            const System::DateTimeOffset newest = std::max(
                {attitudeTimestamp, gravityTimestamp_, linearAccelerationTimestamp_, gyroscopeTimestamp_});
            if ((newest - oldest) > MaxFusionAgeWindow)
            {
                // Task MOT2-003 (2026-07-17, external audit
                // `audit_devices_2026-07-17.md`): "drop incomplete frames
                // and expose counters" -- the drop itself already existed
                // (MOTION-007); this only adds the counter, so a caller/test
                // can observe how often it actually fires rather than
                // trusting it silently works. Does not implement this
                // task's larger remaining ask (bounded per-source sample
                // queues, nearest/interpolated selection within a tight,
                // hardware-measured skew, replacing this fixed 500ms
                // latest-value bound) -- see this method's own class-level
                // doc comment and plans/plan_devices.md's MOT2-003 resolution
                // note for why that is deliberately deferred as its own,
                // larger design task rather than rushed here.
                ++droppedFusionFrameCountForTesting_;
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
