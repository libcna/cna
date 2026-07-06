// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * Abstract counterpart to SDL's SDL_ORIENTATION_LANDSCAPE /
     * SDL_ORIENTATION_LANDSCAPE_FLIPPED (Task P5-7), letting the axis-remap
     * math below be unit-tested on any platform without needing a real
     * Android display or an SDL_GetCurrentDisplayOrientation() call. Only
     * the two rotations this demo's window is actually expected to reach
     * are represented — see Accelerometer.cpp's/Gyroscope.cpp's own
     * `#ifdef __ANDROID__` blocks for how SDL's own orientation enum maps
     * to this one.
     *
     * @note Corrected 2026-07-06 (Task ACCEL-004): the orientation lock is
     * **not** an `android:screenOrientation` manifest attribute — the demo's
     * manifest sets none, confirmed by grepping it directly. The actual
     * mechanism is SDL's own runtime `SDLActivity.setOrientationBis()`
     * (`org/libsdl/app/SDLActivity.java`): with no `SDL_HINT_ORIENTATIONS`
     * hint set (confirmed — this codebase never calls
     * `SDL_SetHint(SDL_HINT_ORIENTATIONS, ...)`) and a non-resizable window
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
        /** @brief Device rotated 90° CCW from portrait (SDL_ORIENTATION_LANDSCAPE) — portrait-top points landscape-LEFT. */
        Rotation90,
        /** @brief Device rotated 270° CCW from portrait (SDL_ORIENTATION_LANDSCAPE_FLIPPED) — portrait-top points landscape-RIGHT. */
        Rotation270,
    };

    /**
     * Pure function (Task P5-7): converts raw SDL accelerometer/gyroscope
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
     * querying SDL itself, so it is testable on any platform, unlike the
     * `#ifdef __ANDROID__`-only code that used to inline this same math —
     * the real Android call site queries
     * SDL_GetCurrentDisplayOrientation() and maps the result to this enum
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
} // namespace Microsoft::Devices::Sensors::Detail
