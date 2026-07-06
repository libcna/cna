// SPDX-License-Identifier: MS-PL

#pragma once

#include <cmath>

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Android magnetic-field-sensor accuracy status values.
     *
     * Numerically identical to the NDK's `ASENSOR_STATUS_*` constants
     * (`android/sensor.h`) and the Java `SensorManager.SENSOR_STATUS_*`
     * constants — duplicated here as plain `int` so this header has no
     * Android-only `#include` and is testable on every platform.
     */
    enum class AndroidSensorAccuracyStatus : int
    {
        NoContact = -1,
        Unreliable = 0,
        Low = 1,
        Medium = 2,
        High = 3,
    };

    /**
     * @brief Converts a raw Android rotation-vector quaternion to a magnetic heading, in degrees [0, 360).
     *
     * Pure function (mirrors `Detail::ConvertAndroidPortraitToXnaLandscape()`'s
     * precedent in `AndroidSensorOrientation.hpp`): testable on any platform
     * without a real Android sensor. Builds the standard quaternion-to-
     * rotation-matrix relationship, then extracts azimuth using Android's
     * own world-frame axis convention (East/North/Up when the rotation
     * vector sensor reports orientation) — `atan2(R01, R11)`, matching the
     * well-documented relationship between `SensorManager.getRotationMatrixFromVector()`'s
     * output and `SensorManager.getOrientation()`'s azimuth component. This
     * specific axis mapping is a factual requirement to stay compatible
     * with Android's own sensor semantics (not an arbitrary/creative
     * choice), reproduced here from first-principles quaternion algebra
     * rather than copied from any implementation source.
     *
     * @note Never checked against real hardware (no Android device/emulator
     * available in this environment) — see `docs/devices-hardware-checklist.md`.
     * Only self-consistency (identity quaternion → 0°, monotonic response to
     * a known yaw rotation) has been verified. Treat the exact sign/zero-
     * point convention as unverified until confirmed on a real device,
     * mirroring `Detail::ConvertAndroidPortraitToXnaLandscape()`'s own
     * standing caveat for the accelerometer/gyroscope axis remap.
     *
     * @param x Quaternion X component (`ASensorEvent::data[0]`).
     * @param y Quaternion Y component (`data[1]`).
     * @param z Quaternion Z component (`data[2]`).
     * @param w Quaternion scalar component (`data[3]`).
     * @return Magnetic heading, in degrees, in the range [0, 360).
     */
    [[nodiscard]] inline double ConvertRotationVectorToMagneticHeadingDegrees(float x, float y, float z, float w)
    {
        // M_PI is a POSIX/BSD <cmath> extension, not standard C++ -- not
        // guaranteed to be defined on every toolchain/standard-library
        // combination this project targets, so a local constant is used
        // instead of relying on it.
        constexpr double Pi = 3.141592653589793238462643383279502884;

        const double r01 = 2.0 * (static_cast<double>(x) * y - static_cast<double>(z) * w);
        const double r11 = 1.0 - 2.0 * (static_cast<double>(x) * x + static_cast<double>(z) * z);

        const double azimuthRadians = std::atan2(r01, r11);
        double degrees = azimuthRadians * (180.0 / Pi);
        degrees = std::fmod(degrees + 360.0, 360.0);
        return degrees;
    }

    /**
     * @brief Maps a magnetic-field sensor's accuracy status to a HeadingAccuracy value, in degrees.
     *
     * CNA-chosen mapping (the real WP7 API documents `HeadingAccuracy` as
     * degrees but does not define what value corresponds to a given
     * hardware accuracy tier — there is no XNA-documented value to match):
     * `Unreliable`/`NoContact`/any unrecognized value → 180° (worst case,
     * "could be anything"), `Low` → 20°, `Medium` → 15°, `High` → 5°.
     *
     * @note `Low`'s value is deliberately exactly 20°, not some larger
     * "clearly bad" value like the previous 45° (Task COMPASS-006,
     * 2026-07-06) — the real `Compass.Calibrate` event's own documented
     * contract (archived MSDN `hh203107(v=vs.105)`) is "If the
     * HeadingAccuracy exceeds +/- 20 degrees, this event is raised."
     * `ShouldRaiseCalibrateForAccuracyStatus()` below deliberately does
     * *not* fire `Calibrate` for `Low` (avoids event spam from a common,
     * momentary reading during normal use) — so `Low`'s own reported
     * `HeadingAccuracy` value must not itself exceed 20°, or a game
     * independently checking `HeadingAccuracy > 20` (matching the
     * documented real-API rule exactly, instead of relying on `Calibrate`)
     * would see a contradiction: a "no calibration needed" status reporting
     * an accuracy number that claims calibration *is* needed. The previous
     * 45° value did not have this property. `20.0` itself does not "exceed"
     * 20 (a strict `>` comparison), so it stays consistent with not firing.
     *
     * @param status Accuracy status from the magnetic-field sensor's
     * `ASensorVector::status` field.
     * @return Heading accuracy, in degrees.
     */
    [[nodiscard]] inline double ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(
        AndroidSensorAccuracyStatus status)
    {
        switch (status)
        {
        case AndroidSensorAccuracyStatus::High:
            return 5.0;
        case AndroidSensorAccuracyStatus::Medium:
            return 15.0;
        case AndroidSensorAccuracyStatus::Low:
            return 20.0;
        case AndroidSensorAccuracyStatus::Unreliable:
        case AndroidSensorAccuracyStatus::NoContact:
        default:
            return 180.0;
        }
    }

    /**
     * @brief Whether an accuracy status should raise `Compass::Calibrate`.
     *
     * `Unreliable`/`NoContact` (device needs the classic figure-8 gesture)
     * raise it; `Low`/`Medium`/`High` do not. `Low` is deliberately excluded
     * (matching `docs/devices-native-backend-design.md`'s own "and
     * optionally `_LOW`" phrasing as a choice, not a requirement) — a
     * momentarily "Low" reading during normal use is common and would make
     * `Calibrate` fire too eagerly if included.
     *
     * @param status Accuracy status from the magnetic-field sensor.
     * @return true if this status should raise `Calibrate`.
     */
    [[nodiscard]] inline bool ShouldRaiseCalibrateForAccuracyStatus(AndroidSensorAccuracyStatus status)
    {
        return status == AndroidSensorAccuracyStatus::Unreliable
            || status == AndroidSensorAccuracyStatus::NoContact;
    }
} // namespace Microsoft::Devices::Sensors::Detail
