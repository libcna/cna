// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief A spot light as the engine layer needs to describe one.
     *
     * Engine-layer only, like `PointLightEXT` and `DirectionalLightEXT`. XNA 4.0 has no spot light,
     * so nothing here is a reinterpretation of an existing API.
     */
    struct SpotLightEXT
    {
        /** @brief World-space position of the light. */
        Microsoft::Xna::Framework::Vector3 Position{0.0f, 0.0f, 0.0f};

        /**
         * @brief The direction the cone points, in world space.
         *
         * Normalized on use rather than on assignment, so a caller may write a convenient
         * `(0, -1, 0)` without it silently becoming something else.
         */
        Microsoft::Xna::Framework::Vector3 Direction{0.0f, -1.0f, 0.0f};

        /** @brief Light colour, linear, unbounded. */
        Microsoft::Xna::Framework::Vector3 Color{1.0f, 1.0f, 1.0f};

        /** @brief Multiplier applied to @ref Color. */
        float Intensity = 1.0f;

        /** @brief Distance past which the light contributes nothing; the shadow map's far plane. */
        float Range = 20.0f;

        /**
         * @brief Half-angle of the cone's bright centre, in radians.
         *
         * Everything inside this angle is fully lit; between it and @ref OuterAngle the
         * contribution falls off. Kept as a half-angle because that is what a projection matrix
         * needs, and converting in one place is one fewer factor of two to get wrong.
         */
        float InnerAngle = 0.35f;

        /**
         * @brief Half-angle at which the cone ends, in radians.
         *
         * The shadow map's field of view is twice this, so a wide cone spreads the same texels over
         * more of the world -- the spot equivalent of a shadow map fitted too loosely.
         */
        float OuterAngle = 0.5f;

        /** @brief Whether a shadow map should be generated for this light. */
        bool CastsShadows = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
