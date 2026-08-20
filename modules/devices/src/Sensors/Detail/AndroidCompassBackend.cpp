// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp"

#ifdef __ANDROID__

#include <android/sensor.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    using Microsoft::Xna::Framework::Vector3;

    AndroidCompassBackend::AndroidCompassBackend()
        : rotationVectorBridge_(ASENSOR_TYPE_ROTATION_VECTOR),
          magneticFieldBridge_(ASENSOR_TYPE_MAGNETIC_FIELD)
    {
    }

    AndroidCompassBackend::~AndroidCompassBackend()
    {
        Stop();
    }

    bool AndroidCompassBackend::IsSupported()
    {
        return rotationVectorBridge_.IsAvailable() && magneticFieldBridge_.IsAvailable();
    }

    bool AndroidCompassBackend::Start(
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
            hasRotationVectorSample_ = false;
            hasMagneticFieldSample_ = false;
            // Task COMP2-001: "Reset freshness on Start/resume" -- a fresh
            // Start() must not let a stale timestamp survive from a
            // previous run and immediately make the first new sample of
            // *this* run look stale relative to it.
            rotationVectorTimestamp_ = System::DateTimeOffset::getUtcNowProperty();
            magneticFieldTimestamp_ = rotationVectorTimestamp_;
            maxSampleSkew_ = ComputeCompassMaxSampleSkew(timeBetweenUpdates);
        }

        const bool rotationStarted = rotationVectorBridge_.Start(
            timeBetweenUpdates,
            [this](const AndroidSensorSample& sample) { HandleRotationVectorSample(sample); });
        const bool magneticStarted = magneticFieldBridge_.Start(
            timeBetweenUpdates,
            [this](const AndroidSensorSample& sample) { HandleMagneticFieldSample(sample); });

        if (!rotationStarted || !magneticStarted)
        {
            Stop();
            return false;
        }

        return true;
    }

    void AndroidCompassBackend::Stop()
    {
        rotationVectorBridge_.Stop();
        magneticFieldBridge_.Stop();
    }

    void AndroidCompassBackend::SetSampleInterval(const System::TimeSpan& timeBetweenUpdates)
    {
        // Both bridges — AndroidSensorBridge::SetSampleInterval() itself is
        // a safe no-op on whichever one, if either, is not currently
        // started (Task ANDROID-BRIDGE-002).
        rotationVectorBridge_.SetSampleInterval(timeBetweenUpdates);
        magneticFieldBridge_.SetSampleInterval(timeBetweenUpdates);
    }

    void AndroidCompassBackend::HandleRotationVectorSample(const AndroidSensorSample& sample)
    {
        // Heading comes from the OS-fused rotation vector quaternion, never
        // from raw accelerometer+magnetometer cross-product math computed
        // in this bridge -- see this class's own doc comment and
        // plans/plan_devices.md's DEVICES-0100 gate task.
        //
        // Task COMPASS-009: automatically switches between flat-mode and
        // upright-mode axis conventions based on the device's current tilt,
        // derived entirely from this same quaternion -- see
        // ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()'s own
        // doc comment for the full derivation.
        const double heading = ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(
            sample.Values[0], sample.Values[1], sample.Values[2], sample.Values[3]);

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            magneticHeadingDegrees_ = heading;
            hasRotationVectorSample_ = true;
            // Task COMP2-001: sample.Timestamp (see AndroidSensorSample's
            // own doc comment) is this sample's real delivery time --
            // PublishReading() compares it against the magnetic-field
            // stream's own last-delivery time to decide whether the two are
            // still close enough together to fuse.
            rotationVectorTimestamp_ = sample.Timestamp;
        }
        PublishReading();
    }

    void AndroidCompassBackend::HandleMagneticFieldSample(const AndroidSensorSample& sample)
    {
        const auto status = static_cast<AndroidSensorAccuracyStatus>(sample.Status);
        const double accuracyDegrees = ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(status);

        CalibrationCallback calibrationCallback;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            magnetometerReading_ = Vector3(sample.Values[0], sample.Values[1], sample.Values[2]);
            headingAccuracyDegrees_ = accuracyDegrees;
            hasMagneticFieldSample_ = true;
            // Task COMP2-001: see HandleRotationVectorSample()'s identical comment.
            magneticFieldTimestamp_ = sample.Timestamp;

            if (ShouldRaiseCalibrateForAccuracyStatus(status))
            {
                calibrationCallback = onCalibrationNeeded_;
            }
        }

        // Task COMPASS-008 (2026-07-06): PublishReading() must run *before*
        // calibrationCallback(), not after -- calibrationCallback() invokes
        // user code (Compass::Calibrate's subscribers), which may destroy
        // the owning Compass (and therefore this AndroidCompassBackend)
        // reentrantly, exactly like a CurrentValueChanged handler can
        // (SENSORBASE-003). PublishReading() itself never touches `this`
        // after invoking its own callback (its last statement), matching
        // this codebase's established "last touch of `this` is a user
        // callback invocation" safety pattern (see HandleRotationVectorSample()
        // and Gyroscope::DispatchSensorReading()) -- so calibrationCallback()
        // must be the true last statement in this function, or a
        // Calibrate handler destroying the instance would leave this call
        // site invoking PublishReading() on an already-destroyed `this`.
        // The previous order (calibrationCallback() then PublishReading())
        // had exactly that bug. Not independently testable here beyond
        // compiling (Android-only code, no host test seam reaches this
        // exact multi-callback call chain -- see COMPASS-008's closing
        // note in plans/plan_devices.md).
        PublishReading();

        if (calibrationCallback)
        {
            calibrationCallback();
        }
    }

    void AndroidCompassBackend::PublishReading()
    {
        ReadingCallback callback;
        CompassReading reading;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            const System::DateTimeOffset now = System::DateTimeOffset::getUtcNowProperty();

            // Only publish once both sensors have delivered at least one
            // sample -- a reading built from only one of the two would be
            // half-default data, not a real fused reading.
            //
            // Task COMP2-001 (2026-07-17, external audit
            // `audit_devices_2026-07-17.md`): also requires both streams'
            // last sample to still be fresh relative to `now`. Without
            // this, a stream that silently stopped delivering (its own
            // AndroidSensorBridge worker died, or the underlying device
            // disappeared) would keep its last, now-stale value fused with
            // the other, still-live stream's fresh samples indefinitely --
            // producing readings that *look* fresh (a brand-new
            // CompassReading.Timestamp every time) but are actually built
            // from arbitrarily-stale data for one of the two fused
            // quantities. "Drop rather than fuse indefinitely stale data"
            // (this task's own required-work wording): a stale pairing
            // simply skips this publish, exactly as if the required sample
            // had never arrived at all -- the still-live stream's *next*
            // fresh sample re-attempts the same check, so this recovers on
            // its own the moment the stalled stream (if it ever does)
            // delivers again, with no separate "recovery" logic needed.
            if (!hasRotationVectorSample_ || !hasMagneticFieldSample_
                || !IsCompassSampleFresh(rotationVectorTimestamp_, now, maxSampleSkew_)
                || !IsCompassSampleFresh(magneticFieldTimestamp_, now, maxSampleSkew_))
            {
                return;
            }

            callback = onReading_;

            // TrueHeading is deliberately left equal to MagneticHeading,
            // never fabricated from an assumed declination -- true heading
            // requires geomagnetic declination data from a location source,
            // which does not exist in this codebase (System.Device.Location
            // is out of scope; see docs/location-future-plans/plan.md). This is
            // the same honest limitation
            // docs/devices-native-backend-design.md's Android Compass
            // section documents.
            // Wall-clock time of this reading, this project's one consistent
            // cross-sensor-class timestamp policy (Task READINGS-003) — see
            // docs/devices-api-coverage.md's "Timestamp policy" section.
            reading = CompassReading(
                headingAccuracyDegrees_,
                magneticHeadingDegrees_,
                magnetometerReading_,
                now,
                magneticHeadingDegrees_);
        }

        if (callback)
        {
            callback(reading);
        }
    }
} // namespace Microsoft::Devices::Sensors::Detail

#endif // __ANDROID__
