// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief A directional light as the engine layer needs to describe one.
     *
     * Deliberately not an XNA type. XNA's own `DirectionalLight` belongs to `BasicEffect` and
     * describes a shading contribution; this describes a light in the scene, which is what a
     * shadow map is generated from. Keeping them separate avoids implying that setting one
     * changes the other.
     */
    struct DirectionalLightEXT
    {
        /**
         * @brief The direction the light travels, in world space.
         *
         * Normalized on use rather than on assignment, so a caller may write a convenient
         * `(-1, -1, -1)` without it silently becoming something else.
         */
        Microsoft::Xna::Framework::Vector3 Direction{0.0f, -1.0f, 0.0f};

        /** @brief Light colour, linear, unbounded (values above 1 are meaningful in an HDR scene). */
        Microsoft::Xna::Framework::Vector3 Color{1.0f, 1.0f, 1.0f};

        /** @brief Multiplier applied to @ref Color. */
        float Intensity = 1.0f;

        /** @brief Whether a shadow map should be generated for this light. */
        bool CastsShadows = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
