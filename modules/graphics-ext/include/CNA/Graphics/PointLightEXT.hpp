// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Graphics {

    /**
     * @brief A point light as the engine layer needs to describe one.
     *
     * Deliberately not an XNA type, for the same reason `DirectionalLightEXT` is not: XNA's
     * `DirectionalLight` belongs to `BasicEffect` and describes a shading contribution, while this
     * describes a light in the scene -- the thing a shadow map is generated from. XNA 4.0 has no
     * point light at all, so there is no name to preserve here.
     */
    struct PointLightEXT
    {
        /** @brief World-space position of the light. */
        Microsoft::Xna::Framework::Vector3 Position{0.0f, 0.0f, 0.0f};

        /** @brief Light colour, linear, unbounded (values above 1 are meaningful in an HDR scene). */
        Microsoft::Xna::Framework::Vector3 Color{1.0f, 1.0f, 1.0f};

        /** @brief Multiplier applied to @ref Color. */
        float Intensity = 1.0f;

        /**
         * @brief Distance past which the light contributes nothing.
         *
         * Not a soft falloff parameter: it is the far plane of every shadow face, and the divisor
         * that turns a stored distance into the 0..1 value a colour texture can hold. A range far
         * larger than the scene therefore costs precision everywhere, which is the practical
         * reason to set it to what the light actually reaches.
         */
        float Range = 20.0f;

        /** @brief Whether a shadow map should be generated for this light. */
        bool CastsShadows = false;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
