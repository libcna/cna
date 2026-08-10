// SPDX-License-Identifier: MS-PL

#pragma once

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Minimum squared length a quaternion must have before this file
     * will normalize and use it (Task MOT2-002).
     *
     * `Quaternion::Normalize()` (real XNA behavior, faithfully unchanged —
     * see that method's own implementation) divides by `1/sqrt(lengthSquared)`
     * with no zero/near-zero guard of its own — a near-zero-norm quaternion
     * (e.g. `{0,0,0,0}`, or a garbled sample that decays toward it) produces
     * `Inf`/`NaN` throughout, not a clean error. This threshold is deliberately
     * far below any length a real, even barely-valid orientation quaternion
     * could ever have (a unit quaternion has `LengthSquared() == 1`) —
     * it exists purely to reject numerically-unstable division, not to
     * express any real tolerance for a "slightly off" orientation.
     */
    inline constexpr float MinimumValidQuaternionLengthSquared = 1e-12f;

    /**
     * @brief Normalizes a quaternion, falling back to `Quaternion::Identity`
     * for non-finite or near-zero-norm input (Task MOT2-002).
     *
     * Shared by `ConvertRotationVectorToXnaQuaternion()` and
     * `ExtractYawPitchRollFromQuaternion()` so both validate identically —
     * see `MinimumValidQuaternionLengthSquared`'s own doc comment for why a
     * near-zero norm is rejected rather than normalized, and
     * `ConvertRotationVectorToXnaQuaternion()`'s own doc comment for why
     * *this* function, not just the yaw/pitch/roll extraction, is where
     * validation belongs.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Quaternion NormalizeOrIdentity(
        const Microsoft::Xna::Framework::Quaternion& quaternion)
    {
        const float lengthSquared = quaternion.LengthSquared();

        if (!std::isfinite(lengthSquared) || lengthSquared < MinimumValidQuaternionLengthSquared)
        {
            return Microsoft::Xna::Framework::Quaternion::Identity;
        }

        return Microsoft::Xna::Framework::Quaternion::Normalize(quaternion);
    }

    /**
     * @brief Converts a raw Android rotation-vector quaternion to an XNA Quaternion.
     *
     * Pure function, testable on any platform. This is a direct
     * component-for-component mapping (Android's `x,y,z,w` straight into
     * `Quaternion(x,y,z,w)`) — **now backed by an explicit change-of-basis
     * derivation (Task `MOT2-001`, 2026-07-18), not merely "the simplest
     * possible choice" as an earlier version of this comment described it**:
     *
     * - **Handedness/formula match, verified from source, not assumed**:
     *   Android's `TYPE_ROTATION_VECTOR` quaternion uses the standard
     *   Hamilton active-rotation convention in a right-handed device/world
     *   (East/North/Up) frame. `Microsoft::Xna::Framework::Matrix`'s own
     *   header documents itself as "a right-handed 4x4 matrix"
     *   (`Matrix.hpp`), and `Quaternion::CreateFromAxisAngle()`'s actual
     *   implementation (`Quaternion.cpp`: `result.{X,Y,Z} = axis*sin(angle/2),
     *   result.W = cos(angle/2)`, no sign flip anywhere) is the identical,
     *   unmodified Hamilton formula — confirmed by direct computation (not
     *   just formula inspection) that a +90° `CreateFromAxisAngle(Z, ...)`
     *   quaternion run through `Matrix::CreateFromQuaternion()` rotates
     *   `+X` to `+Y` under XNA's own row-vector convention, matching the
     *   standard right-handed, counterclockwise-from-the-positive-axis
     *   sense. Android and XNA quaternions are therefore the *same*
     *   mathematical object under the *same* multiplication convention —
     *   there is no handedness correction to derive or apply.
     * - **No display-orientation remap needed — confirmed, not assumed**:
     *   the same archived MSDN Magazine article already cited for
     *   `Detail::IsAndroidLandscapeRemapEnabled()`/`AndroidCompassMath.hpp`
     *   ("Touch and Go — Getting Oriented with the Windows Phone Compass,"
     *   Petzold, 2012) states real WP7 device-relative sensor readings "are
     *   the same whether... running in portrait or landscape mode." A
     *   direct, unremapped quaternion passthrough is therefore the
     *   *WP7-faithful* choice for `Motion.Attitude`, not a gap — consistent
     *   with `AndroidCompassMath.hpp`'s own identical conclusion for
     *   `Compass` (Task `COMP2-003`).
     * - **A real, separate finding surfaced while investigating this task,
     *   deliberately not acted on here**: `Detail::ConvertAndroidPortraitToXnaLandscape()`
     *   — the landscape remap `Accelerometer`/`Gyroscope`/`Motion`'s own
     *   `Gravity`/`DeviceAcceleration`/`DeviceRotationRate` fields already
     *   apply (Task `MOTION-012`) — is, for both its `Rotation90`/`Rotation270`
     *   cases, a **reflection** (`diag(1,-1,1)`/`diag(-1,1,1)`, determinant
     *   `-1`), not a proper rotation (determinant `+1`), verified by direct
     *   computation. This means (a) it cannot represent an actual 90°/270°
     *   physical device rotation as a coordinate transform, and (b), more
     *   directly relevant here, **no quaternion can represent a reflection at
     *   all** — quaternions only encode proper (determinant `+1`) rotations —
     *   so even a "for consistency with Motion's other three remapped
     *   fields" argument for adding a matching remap to this `Quaternion`
     *   could not actually be implemented as a quaternion multiply. Not
     *   fixed here: `ConvertAndroidPortraitToXnaLandscape()` is
     *   already-shipped, already-tested, deliberate `CNAEXT` behavior with
     *   its own maintainer-made decision (`ACCEL-008`, 2026-07-07) this task
     *   has no mandate to unilaterally revisit — flagged as a genuinely new
     *   finding for whoever next touches `ACCEL-004`/`ACCEL-008`/`MOTION-012`,
     *   not silently absorbed into this function's own scope.
     *
     * `RotationMatrix`/`Yaw`/`Pitch`/`Roll` are always derived FROM this
     * same `Quaternion` (never independently), so they stay internally
     * consistent with each other regardless of whether the axis
     * *correspondence* itself (as opposed to the display-orientation and
     * handedness questions above, both now closed) is eventually found on
     * real hardware to need correction — still genuinely unverified, no
     * Android device/emulator exists in this environment.
     *
     * Task MOT2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
     * now normalizes (via `NormalizeOrIdentity()`) rather than passing the
     * four raw components straight through unchecked. Validation belongs
     * *here*, not only inside `ExtractYawPitchRollFromQuaternion()`: this
     * class's own caller (`AndroidMotionBackend::HandleAttitudeSample()`)
     * separately calls `Matrix::CreateFromQuaternion()` on the `Quaternion`
     * this function returns to build the *published*
     * `AttitudeReading::RotationMatrix` — validating only inside the
     * yaw/pitch/roll extraction would have left that separately-computed,
     * separately-published matrix (and the published `Quaternion` itself)
     * still built from raw, possibly non-finite/non-unit input, silently
     * breaking the "always derived from this same Quaternion" invariant
     * this doc comment itself already promised.
     *
     * @param x Quaternion X component (`ASensorEvent::data[0]`).
     * @param y Quaternion Y component (`data[1]`).
     * @param z Quaternion Z component (`data[2]`).
     * @param w Quaternion scalar component (`data[3]`).
     * @return Equivalent, normalized XNA Quaternion (`Quaternion::Identity`
     * if the input was non-finite or had a near-zero norm).
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Quaternion ConvertRotationVectorToXnaQuaternion(
        float x, float y, float z, float w)
    {
        return NormalizeOrIdentity(Microsoft::Xna::Framework::Quaternion(x, y, z, w));
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
     * Task MOT2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
     * hardened against invalid/non-unit input — this function has no way to
     * guarantee its caller already validated it (`ConvertRotationVectorToXnaQuaternion()`
     * now does, for its own call site, but this is a general-purpose pure
     * function, not exclusively called through that one path):
     * - `Matrix::CreateFromQuaternion()`'s own formula assumes a unit-length
     *   quaternion (a non-unit one produces a scaled, non-rotation matrix,
     *   not a domain error, but a silently-wrong one) — normalizes first via
     *   the same `NormalizeOrIdentity()` helper `ConvertRotationVectorToXnaQuaternion()`
     *   uses, so an already-normalized input costs one cheap, idempotent
     *   re-check, and an un-normalized one (from any *other* caller) is
     *   handled identically to that call site. See
     *   `MinimumValidQuaternionLengthSquared`'s own doc comment for why a
     *   near-zero norm is rejected rather than normalized; a non-finite or
     *   near-zero-norm input yields `yaw`/`pitch`/`roll` all `0` (from
     *   `Quaternion::Identity`) rather than propagating garbage — a
     *   deliberate, documented CNA policy choice (no WP7 reference behavior
     *   exists for "what does an invalid native sensor sample decode to,"
     *   since real WP7 never ran this code path at all).
     * - `std::asin(-M32)`'s argument is mathematically guaranteed to be in
     *   `[-1, 1]` for a properly unit-length, finite rotation matrix, but
     *   floating-point rounding in `Matrix::CreateFromQuaternion()`'s own
     *   arithmetic can still push it fractionally outside that range (most
     *   commonly exactly at a gimbal-lock pole, `pitch == +-π/2`) — `asin()`
     *   of an out-of-domain argument returns `NaN`, not a clamped boundary
     *   value. Clamped to `[-1, 1]` before calling `asin()`. Gimbal-lock
     *   convention: unchanged from the pre-existing formula — `yaw`/`roll`
     *   both remain independently computed via `atan2()` at the pole
     *   exactly as before this task (no explicit `M31`/`M33`/`M12`/`M22`-all-zero
     *   special case is introduced), since `atan2(0, 0)` is well-defined
     *   (`0`) in C++, not a domain error the way `asin()` outside `[-1, 1]`
     *   is — there was no actual gap to fix in the `atan2()` calls.
     *
     * @param quaternion Orientation to decompose.
     * @param yaw Output: rotation around the Y axis, in radians.
     * @param pitch Output: rotation around the X axis, in radians.
     * @param roll Output: rotation around the Z axis, in radians.
     */
    inline void ExtractYawPitchRollFromQuaternion(
        const Microsoft::Xna::Framework::Quaternion& quaternion, float& yaw, float& pitch, float& roll)
    {
        const Microsoft::Xna::Framework::Matrix m =
            Microsoft::Xna::Framework::Matrix::CreateFromQuaternion(NormalizeOrIdentity(quaternion));

        const float clampedAsinArgument = std::clamp(-m.M32, -1.0f, 1.0f);
        pitch = std::asin(clampedAsinArgument);
        yaw = std::atan2(m.M31, m.M33);
        roll = std::atan2(m.M12, m.M22);
    }
} // namespace Microsoft::Devices::Sensors::Detail
