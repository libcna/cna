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
        // plan_devices.md's DEVICES-0100 gate task.
        const double heading = ConvertRotationVectorToMagneticHeadingDegrees(
            sample.Values[0], sample.Values[1], sample.Values[2], sample.Values[3]);

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            magneticHeadingDegrees_ = heading;
            hasRotationVectorSample_ = true;
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

            if (ShouldRaiseCalibrateForAccuracyStatus(status))
            {
                calibrationCallback = onCalibrationNeeded_;
            }
        }

        if (calibrationCallback)
        {
            calibrationCallback();
        }

        PublishReading();
    }

    void AndroidCompassBackend::PublishReading()
    {
        ReadingCallback callback;
        CompassReading reading;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            // Only publish once both sensors have delivered at least one
            // sample -- a reading built from only one of the two would be
            // half-default data, not a real fused reading.
            if (!hasRotationVectorSample_ || !hasMagneticFieldSample_)
            {
                return;
            }

            callback = onReading_;

            // TrueHeading is deliberately left equal to MagneticHeading,
            // never fabricated from an assumed declination -- true heading
            // requires geomagnetic declination data from a location source,
            // which does not exist in this codebase (System.Device.Location
            // is out of scope; see docs/location-future-plan.md). This is
            // the same honest limitation
            // docs/devices-native-backend-design.md's Android Compass
            // section documents.
            reading = CompassReading(
                headingAccuracyDegrees_,
                magneticHeadingDegrees_,
                magnetometerReading_,
                System::DateTimeOffset::getUtcNowProperty(),
                magneticHeadingDegrees_);
        }

        if (callback)
        {
            callback(reading);
        }
    }
} // namespace Microsoft::Devices::Sensors::Detail

#endif // __ANDROID__
