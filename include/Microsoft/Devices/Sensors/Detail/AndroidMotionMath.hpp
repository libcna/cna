// SPDX-License-Identifier: MS-PL

#pragma once

#include <cmath>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Converts a raw Android rotation-vector quaternion to an XNA Quaternion.
     *
     * Pure function, testable on any platform. This is a direct
     * component-for-component mapping (Android's `x,y,z,w` straight into
     * `Quaternion(x,y,z,w)`) — deliberately the simplest possible choice,
     * not a rigorously-derived change-of-basis between Android's world
     * frame and XNA's. **Never checked against real hardware** (no Android
     * device/emulator in this environment); whether this needs the same
     * kind of axis remap `Detail::ConvertAndroidPortraitToXnaLandscape()`
     * applies to `Accelerometer`/`Gyroscope`'s vector readings is an open
     * question — see `docs/devices-hardware-checklist.md`'s Motion section.
     * `RotationMatrix`/`Yaw`/`Pitch`/`Roll` are always derived FROM this
     * same `Quaternion` (never independently), so they stay internally
     * consistent with each other regardless of whether the absolute
     * mapping to real-world Android axes is eventually found to need
     * correction.
     *
     * @param x Quaternion X component (`ASensorEvent::data[0]`).
     * @param y Quaternion Y component (`data[1]`).
     * @param z Quaternion Z component (`data[2]`).
     * @param w Quaternion scalar component (`data[3]`).
     * @return Equivalent XNA Quaternion.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Quaternion ConvertRotationVectorToXnaQuaternion(
        float x, float y, float z, float w)
    {
        return {x, y, z, w};
    }

    /**
     * @brief Extracts yaw/pitch/roll (radians) from a Quaternion, exactly matching `Quaternion::CreateFromYawPitchRoll()`'s own convention.
     *
     * Pure function, derived from — and numerically verified round-trip
     * against — `Quaternion::CreateFromYawPitchRoll()`/
     * `Matrix::CreateFromQuaternion()`'s own existing, already-tested
     * formulas (not an independent guess at XNA's Euler convention): builds
     * the rotation matrix via the same element formulas
     * `Matrix::CreateFromQuaternion()` uses, then extracts
     * `pitch = asin(-M32)`, `yaw = atan2(M31, M33)`, `roll = atan2(M12, M22)`
     * (1-indexed row/column, matching `Matrix`'s own `M11..M44` naming).
     * Verified numerically (Python prototype, this session) to round-trip
     * exactly through `CreateFromYawPitchRoll()` for several angle
     * combinations before being written here — see `AndroidMotionMathTests`
     * for the equivalent C++ round-trip tests.
     *
     * @param quaternion Orientation to decompose.
     * @param yaw Output: rotation around the Y axis, in radians.
     * @param pitch Output: rotation around the X axis, in radians.
     * @param roll Output: rotation around the Z axis, in radians.
     */
    inline void ExtractYawPitchRollFromQuaternion(
        const Microsoft::Xna::Framework::Quaternion& quaternion, float& yaw, float& pitch, float& roll)
    {
        const Microsoft::Xna::Framework::Matrix m = Microsoft::Xna::Framework::Matrix::CreateFromQuaternion(quaternion);

        pitch = std::asin(-m.M32);
        yaw = std::atan2(m.M31, m.M33);
        roll = std::atan2(m.M12, m.M22);
    }
} // namespace Microsoft::Devices::Sensors::Detail
