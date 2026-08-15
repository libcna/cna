// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * Platform-neutral landscape orientation (Task P5-7), letting the axis-remap
     * math below be unit-tested on any platform without needing a real
     * Android display or native orientation query. Only
     * the two rotations this demo's window is actually expected to reach
     * are represented — see Accelerometer.cpp's/Gyroscope.cpp's own
     * `#ifdef __ANDROID__` blocks for how the platform orientation enum maps
     * to this one.
     *
     * @note Corrected 2026-07-06 (Task ACCEL-004): the orientation lock is
     * **not** an `android:screenOrientation` manifest attribute — the demo's
     * manifest sets none, confirmed by grepping it directly. The actual
     * mechanism is the Android activity's runtime orientation policy: with no explicit
     * orientation hint and a non-resizable window
     * wider than tall, it requests `SCREEN_ORIENTATION_SENSOR_LANDSCAPE`,
     * which Android itself then rotates between only the two landscape
     * states (never portrait) based on the accelerometer — this is *why*
     * only two rotations are ever reached, not a manifest declaration.
     * Not independently re-verified end-to-end on a real running app this
     * session (would require tracing the demo's actual window-creation
     * flags at runtime, plus a real device) — corrected because the old
     * claim named a manifest attribute that provably does not exist in the
     * current manifest, not because the two-rotation assumption itself was
     * found wrong.
     */
    enum class AndroidSensorLandscapeOrientation
    {
        /** @brief Device rotated 90° CCW from portrait — portrait-top points landscape-LEFT. */
        Rotation90,
        /** @brief Device rotated 270° CCW from portrait — portrait-top points landscape-RIGHT. */
        Rotation270,
    };

    /**
     * Pure function (Task P5-7): converts raw platform accelerometer/gyroscope
     * data (portrait device frame) to the XNA Windows Phone landscape
     * coordinate convention, for both allowed landscape rotations. Shared
     * identically by Accelerometer.cpp and Gyroscope.cpp — the sign
     * remapping is the same regardless of which physical quantity (linear
     * acceleration vs. angular rate) the raw values represent. See either
     * file's own `#ifdef __ANDROID__` block for the full coordinate-system
     * rationale (portrait raw axes, sensorLandscape rotations, WP7's
     * expected right-tilt-positive convention).
     *
     * Takes an explicit AndroidSensorLandscapeOrientation rather than
     * querying a native backend itself, so it is testable on any platform, unlike the
     * `#ifdef __ANDROID__`-only code that used to inline this same math —
     * the real Android call site queries
     * the current platform display orientation and maps the result to this enum
     * before calling here.
     *
     * @param rawX X-axis raw sensor value (already unit-converted by the caller if needed, e.g. accelerometer g-normalization).
     * @param rawY Y-axis raw sensor value.
     * @param rawZ Z-axis raw sensor value.
     * @param orientation Current landscape rotation.
     * @return Vector in XNA landscape coordinate convention.
     */
    inline Microsoft::Xna::Framework::Vector3 ConvertAndroidPortraitToXnaLandscape(
        float rawX, float rawY, float rawZ, AndroidSensorLandscapeOrientation orientation)
    {
        if (orientation == AndroidSensorLandscapeOrientation::Rotation270)
        {
            // ROTATION_270: portrait-top → landscape-RIGHT.
            // Tilt right → rawY positive; negate X to keep forward/back consistent.
            return {-rawX, rawY, rawZ};
        }

        // ROTATION_90 (default/fallback): portrait-top → landscape-LEFT.
        // Tilt right → rawY negative; negate Y to match WP7 convention.
        return {rawX, -rawY, rawZ};
    }

    /**
     * Task ACCEL-008: process-wide, CNAEXT opt-out for the landscape remap above.
     * Defaults to `true`, preserving this codebase's existing Accelerometer/Gyroscope
     * behavior unchanged.
     *
     * An archived MSDN Magazine article ("Touch and Go - Getting Oriented with the
     * Windows Phone Compass", Charles Petzold, June 2012) states the real WP7
     * `Accelerometer`/`Gyroscope` **never** remap axes based on display orientation —
     * they always report the same fixed, device-relative frame, and any orientation
     * handling is the game's own responsibility. Native sensor APIs likewise expose a fixed
     * device coordinate frame. `ConvertAndroidPortraitToXnaLandscape()`
     * above is therefore a deliberate **CNA convenience deviation** from real WP7
     * behavior, not part of the XNA 4.0/WP7 contract — kept enabled by default because
     * existing CNA games/demos may already depend on receiving landscape-corrected
     * axes, but a game that wants strict WP7-compatible raw axes (e.g. to implement its
     * own remap, or to match hardware-verified reference behavior) can disable it here.
     *
     * @param enabled false to make `Accelerometer`/`Gyroscope` report the platform's raw,
     * unremapped, device-fixed portrait-frame axes instead.
     */
    void SetAndroidLandscapeRemapEnabled(bool enabled);

    /**
     * @brief CNAEXT: see `SetAndroidLandscapeRemapEnabled()`. Defaults to `true`.
     * @return Whether the landscape remap is currently applied.
     */
    bool IsAndroidLandscapeRemapEnabled();
} // namespace Microsoft::Devices::Sensors::Detail
