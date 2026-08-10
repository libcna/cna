// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Specifies the camera angle preference for racing games.
     */
    enum class RacingCameraAngle
    {
        /** @brief Behind-the-car (chase) camera. */
        Back,
        /** @brief Front-mounted camera. */
        Front,
        /** @brief Inside-the-cockpit camera. */
        Inside
    };
}
