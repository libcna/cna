// SPDX-License-Identifier: MS-PL

#pragma once

#ifdef __ANDROID__

#include <mutex>

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp"
#include "Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Android native `Compass` backend: `ASENSOR_TYPE_ROTATION_VECTOR` for heading, `ASENSOR_TYPE_MAGNETIC_FIELD` for the raw reading/accuracy.
     *
     * Entirely `#ifdef __ANDROID__`-gated (both this header and its `.cpp`)
     * — only ever referenced from `Compass.cpp`'s own `#if defined(__ANDROID__)`
     * block (`docs/devices-native-backend-design.md`'s migration plan), so
     * it is never compiled into a non-Android build at all, unlike
     * `Detail::AndroidSensorBridge` (which is deliberately inert-everywhere
     * as a generic building block).
     *
     * Uses the rotation vector sensor (not raw accelerometer+magnetometer
     * cross-product math) for heading: Android's own rotation vector is
     * already sensor-fused by the OS and gives a direct quaternion, which
     * this class converts to azimuth via
     * ConvertRotationVectorToMagneticHeadingDegrees() — avoiding
     * reimplementing manual sensor fusion in this bridge (see
     * `docs/devices-native-backend-design.md`'s Android Compass section).
     * The separate magnetic-field sensor is still needed for
     * `CompassReading.MagnetometerReading` (raw µT vector) and its own
     * accuracy status, which the rotation vector alone does not expose in
     * the same discrete form.
     *
     * Never fakes a heading from the magnetic-field sensor alone without a
     * real orientation reference, and never fakes `TrueHeading` — see
     * `HandleRotationVectorSample()`'s/`PublishReading()`'s own comments.
     */
    class AndroidCompassBackend final : public ICompassBackend
    {
    public:
        AndroidCompassBackend();
        ~AndroidCompassBackend() override;

        AndroidCompassBackend(const AndroidCompassBackend&) = delete;
        AndroidCompassBackend& operator=(const AndroidCompassBackend&) = delete;

        [[nodiscard]] bool IsSupported() override;

        bool Start(
            const System::TimeSpan& timeBetweenUpdates,
            ReadingCallback onReading,
            CalibrationCallback onCalibrationNeeded) override;

        void Stop() override;

    private:
        void HandleRotationVectorSample(const AndroidSensorSample& sample);
        void HandleMagneticFieldSample(const AndroidSensorSample& sample);
        void PublishReading();

        AndroidSensorBridge rotationVectorBridge_;
        AndroidSensorBridge magneticFieldBridge_;

        std::mutex stateMutex_;
        double magneticHeadingDegrees_ = 0.0;
        double headingAccuracyDegrees_ = 180.0;
        Microsoft::Xna::Framework::Vector3 magnetometerReading_;
        bool hasRotationVectorSample_ = false;
        bool hasMagneticFieldSample_ = false;

        ReadingCallback onReading_;
        CalibrationCallback onCalibrationNeeded_;
    };
} // namespace Microsoft::Devices::Sensors::Detail

#endif // __ANDROID__
