// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Devices::Sensors::Detail
{
    /**
     * @brief Landscape poses supported by CNA's natural-portrait sensor conversion.
     *
     * Android's display angle describes graphics compensation, opposite to the physical turn.
     * The platform adapters map Degrees90/Degrees270 to these explicit poses.
     */
    CNAEXT enum class AndroidSensorLandscapeOrientation
    {
        /** @brief Physical 90-degree CCW turn: the portrait top points landscape left. */
        Rotation90,
        /** @brief Physical 270-degree CCW turn: the portrait top points landscape right. */
        Rotation270,
    };

    /**
     * @brief Rotates a natural-portrait device vector into CNA's landscape screen frame.
     *
     * Output +X points right, +Y points up, and +Z points out of the screen. Rotation90
     * maps portrait +X to +Y and +Y to -X; Rotation270 is its inverse. Both preserve
     * handedness, so the same conversion applies to acceleration and angular velocity.
     * This is an optional CNA convenience, independent of Windows Phone's fixed sensor frame.
     *
     * @param rawX Device-fixed X component, already unit-converted by the caller.
     * @param rawY Device-fixed Y component.
     * @param rawZ Device-fixed Z component.
     * @param orientation Physical landscape pose of a natural-portrait device.
     * @return Vector in the landscape screen frame, with the original units.
     */
    CNAEXT [[nodiscard]] inline Microsoft::Xna::Framework::Vector3 ConvertAndroidPortraitToXnaLandscape(
        float rawX, float rawY, float rawZ, AndroidSensorLandscapeOrientation orientation)
    {
        if (orientation == AndroidSensorLandscapeOrientation::Rotation270)
        {
            return {rawY, -rawX, rawZ};
        }

        return {-rawY, rawX, rawZ};
    }

    /**
     * @brief Enables CNA's optional landscape conversion for Android sensor vectors.
     *
     * Enabled by default, as retained by ACCEL-008. Disabling exposes device-fixed components
     * after unit conversion for Accelerometer, Gyroscope and Motion's three vector fields.
     * Motion.Attitude remains device/world-relative regardless of this switch.
     *
     * @param enabled Whether to rotate sensor vectors into the landscape screen frame.
     */
    CNAEXT void SetAndroidLandscapeRemapEnabled(bool enabled);

    /**
     * @brief Gets whether CNA's optional landscape conversion is enabled.
     * @return True when sensor vectors are converted; the default is true.
     */
    CNAEXT [[nodiscard]] bool IsAndroidLandscapeRemapEnabled();
} // namespace Microsoft::Devices::Sensors::Detail
