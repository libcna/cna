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
     * @brief Magnetic heading, in degrees [0, 360), for a device held **upright**
     * ("portrait"/handheld-compass mode, e.g. held vertically facing the user, like
     * a traditional compass) — as opposed to `ConvertRotationVectorToMagneticHeadingDegrees()`
     * above, which is correct for a device lying **flat** (e.g. face-up like a map).
     *
     * Task COMPASS-009: the real WP7 `Compass`'s own archived MSDN Remarks state
     * "the compass uses a different axis to compute the heading, depending on the
     * orientation of the device" — held upright vs. held flat. The companion
     * walkthrough ("How to get data from the compass sensor for Windows Phone 8",
     * archived `hh202974(v=vs.105)`) names the upright case
     * `isCompassUsingNegativeZAxis` and switches to a different axis of its own
     * (WP7-specific) accelerometer/orientation data — but that exact switch cannot
     * be ported verbatim, since it is written against WP7's own coordinate
     * convention, not Android's rotation-vector quaternion.
     *
     * This function derives the Android-specific equivalent from first principles,
     * the same discipline `ConvertRotationVectorToMagneticHeadingDegrees()` above
     * already uses: Android's own `SensorManager.remapCoordinateSystem(inR,
     * AXIS_X, AXIS_Z, outR)` — the standard, documented way Android apps compute a
     * "hold the phone upright like a compass" heading — redefines the device's
     * axes so the new Y axis is the old Z axis and the new Z axis is the old
     * -Y axis (the third axis is always fixed by the right-hand rule once two are
     * chosen), which is equivalent to substituting the rotation matrix's *second*
     * column (index 2, `R02`/`R12`) for what `getOrientation()`'s azimuth formula
     * normally reads from the *first* column (index 1, `R01`/`R11`, used by
     * `ConvertRotationVectorToMagneticHeadingDegrees()` above). Combined with
     * Android's own documented `getOrientation()` azimuth formula
     * (`atan2(R[1], R[4])` in its row-major layout) applied to that remapped
     * matrix, and the same quaternion→rotation-matrix formula already used above,
     * this gives `atan2(R02, R12)` where `R02 = 2(xz+yw)` and `R12 = 2(yz-xw)`.
     *
     * @note Never checked against real hardware — same standing caveat as
     * `ConvertRotationVectorToMagneticHeadingDegrees()` above. Only self-consistency
     * (identity quaternion, monotonic response to a known yaw) has been verified.
     * The *mode-selection* logic (`IsDeviceInUprightCompassMode()` below) and this
     * heading formula are two independently-derived pieces; a hardware check should
     * verify both separately if either looks wrong.
     *
     * @param x Quaternion X component (`ASensorEvent::data[0]`).
     * @param y Quaternion Y component (`data[1]`).
     * @param z Quaternion Z component (`data[2]`).
     * @param w Quaternion scalar component (`data[3]`).
     * @return Magnetic heading, in degrees, in the range [0, 360), for upright mode.
     */
    [[nodiscard]] inline double ConvertRotationVectorToUprightMagneticHeadingDegrees(
        float x, float y, float z, float w)
    {
        constexpr double Pi = 3.141592653589793238462643383279502884;

        const double r02 = 2.0 * (static_cast<double>(x) * z + static_cast<double>(y) * w);
        const double r12 = 2.0 * (static_cast<double>(y) * z - static_cast<double>(x) * w);

        const double azimuthRadians = std::atan2(r02, r12);
        double degrees = azimuthRadians * (180.0 / Pi);
        degrees = std::fmod(degrees + 360.0, 360.0);
        return degrees;
    }

    /**
     * @brief Whether a device (identified only by its rotation-vector quaternion)
     * is being held upright ("portrait" compass mode) rather than lying flat
     * ("flat" compass mode) — Task COMPASS-009.
     *
     * The real WP7 walkthrough's own tilt-mode test (`hh202974(v=vs.105)`) reads
     * the device's *accelerometer*, in WP7's fixed, never-remapped device frame
     * (per `ACCEL-008`, `Accelerometer.Acceleration` is exactly this raw frame):
     * `|Acceleration.Z| < cos(45°) && Acceleration.Y < sin(315°)` (`sin(315°) ==
     * -cos(45°)`). Rather than adding a second, independent accelerometer sensor
     * subscription to `AndroidCompassBackend` purely to reproduce this condition,
     * this function derives the equivalent device-frame gravity components
     * directly from the same rotation-vector quaternion already being read for
     * heading: Android's `getRotationMatrixFromVector()` is documented to produce
     * a matrix `R` such that `R * device_vector = world_vector`; a device at rest
     * reports `device_vector ≈ (0, 0, standard_gravity)` in world coordinates
     * (Android's own accelerometer convention, "Up" = `+Z` world), so the
     * stationary accelerometer reading in *device* coordinates is approximately
     * `R^T * (0, 0, g) = g * (third row of R)` (`R` is orthogonal, so
     * `R^{-1} = R^T`). That third row, from the same quaternion→matrix formula
     * `ConvertRotationVectorToMagneticHeadingDegrees()` above already uses, is
     * `R20 = 2(xz-yw)` (device-frame X), `R21 = 2(yz+xw)` (device-frame Y), and
     * `R22 = 1-2(x²+y²)` (device-frame Z) — this function only needs Y and Z,
     * matching the WP7 condition above exactly (only `Acceleration.Y`/`.Z` are
     * ever tested there).
     *
     * @note This mode-selection condition is a real, cited requirement (the WP7
     * behavior it reproduces); the specific Android rotation-matrix-to-gravity
     * derivation above is grounded in Android's own documented sensor-fusion
     * contract, not an arbitrary choice — but like everything else in this file,
     * unverified against real hardware.
     *
     * @param x Quaternion X component (`ASensorEvent::data[0]`).
     * @param y Quaternion Y component (`data[1]`).
     * @param z Quaternion Z component (`data[2]`).
     * @param w Quaternion scalar component (`data[3]`).
     * @return true if the device should use `ConvertRotationVectorToUprightMagneticHeadingDegrees()`
     * instead of `ConvertRotationVectorToMagneticHeadingDegrees()`.
     */
    [[nodiscard]] inline bool IsDeviceInUprightCompassMode(float x, float y, float z, float w)
    {
        // cos(45 degrees) == sin(315 degrees) in magnitude -- a single named
        // constant covers both sides of the WP7-documented condition.
        constexpr double CosFortyFiveDegrees = 0.70710678118654752440;

        const double deviceFrameGravityY = 2.0 * (static_cast<double>(y) * z + static_cast<double>(x) * w);
        const double deviceFrameGravityZ = 1.0 - 2.0 * (static_cast<double>(x) * x + static_cast<double>(y) * y);

        return std::abs(deviceFrameGravityZ) < CosFortyFiveDegrees
            && deviceFrameGravityY < -CosFortyFiveDegrees;
    }

    /**
     * @brief Magnetic heading, in degrees [0, 360), automatically choosing the
     * flat-mode or upright-mode axis convention based on the device's current
     * tilt (Task COMPASS-009). This is the function real Android backends should
     * call — `ConvertRotationVectorToMagneticHeadingDegrees()`/
     * `ConvertRotationVectorToUprightMagneticHeadingDegrees()` above are the two
     * single-mode building blocks, kept directly testable and unchanged from
     * their own pre-COMPASS-009 behavior.
     *
     * @param x Quaternion X component (`ASensorEvent::data[0]`).
     * @param y Quaternion Y component (`data[1]`).
     * @param z Quaternion Z component (`data[2]`).
     * @param w Quaternion scalar component (`data[3]`).
     * @return Magnetic heading, in degrees, in the range [0, 360).
     */
    [[nodiscard]] inline double ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(
        float x, float y, float z, float w)
    {
        if (IsDeviceInUprightCompassMode(x, y, z, w))
        {
            return ConvertRotationVectorToUprightMagneticHeadingDegrees(x, y, z, w);
        }
        return ConvertRotationVectorToMagneticHeadingDegrees(x, y, z, w);
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
