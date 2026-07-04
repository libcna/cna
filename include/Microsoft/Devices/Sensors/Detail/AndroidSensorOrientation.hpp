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
     * the two rotations AndroidManifest.xml's
     * android:screenOrientation="sensorLandscape" allows are represented
     * — see Accelerometer.cpp's/Gyroscope.cpp's own `#ifdef __ANDROID__`
     * blocks for how SDL's own orientation enum maps to this one.
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
