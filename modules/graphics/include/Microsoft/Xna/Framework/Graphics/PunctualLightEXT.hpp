// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class Texture2D;
    class TextureCube;

    /** @brief Which kind of punctual light a `PunctualLightEXT` describes. */
    CNAEXT enum class PunctualLightKindEXT
    {
        /** @brief No punctual light; every other field is ignored. */
        None,
        /** @brief A point light: shadowed by a cube of six distance faces. */
        Point,
        /** @brief A spot light: shadowed by one perspective distance map. */
        Spot,
    };

    /**
     * @brief One punctual light, with its shadow, as a lit effect receives it.
     *
     * CNAEXT: not part of the XNA 4.0 API. XNA's lit effects carry three *directional* lights and
     * nothing else, so a point or spot light is an addition rather than a reinterpretation.
     *
     * **One per draw** (plans/plan_modern.md `MOD-1007`). Three directional slots plus one shadowed
     * punctual light is the budget, and it is a deliberate ceiling rather than an implementation
     * limit that happened: each additional shadowed light is another full generation pass -- six of
     * them for a point light -- so a second one doubles the frame's shadow cost before it draws a
     * pixel. A game that needs more should light its scene with the directional slots and reserve
     * this for the one light that carries the shot.
     *
     * One struct rather than separate setters, for the same reason `ShadowCascadeStateEXT` is one:
     * the position, range and matrix describe the same light, and a matrix from one light beside a
     * position from another produces a shadow that is merely in the wrong place -- which looks like
     * a bias problem rather than a torn update.
     */
    CNAEXT struct PunctualLightEXT
    {
        /** @brief What this describes; `None` leaves every field below inert. */
        PunctualLightKindEXT Kind = PunctualLightKindEXT::None;

        /** @brief World-space position of the light. */
        Vector3 Position{0.0f, 0.0f, 0.0f};

        /** @brief The direction the cone points; ignored for a point light. */
        Vector3 Direction{0.0f, -1.0f, 0.0f};

        /** @brief Diffuse colour, linear. */
        Vector3 DiffuseColor{1.0f, 1.0f, 1.0f};

        /**
         * @brief Distance past which the light contributes nothing.
         *
         * Also the divisor the shadow's stored distance was normalized by, so the value here has
         * to be the one the map was generated with -- a different range reads every occluder at
         * the wrong depth.
         */
        float Range = 20.0f;

        /** @brief Half-angle of the cone's fully lit centre, in radians. Spot only. */
        float InnerAngle = 0.35f;

        /** @brief Half-angle at which the cone ends, in radians. Spot only. */
        float OuterAngle = 0.5f;

        /**
         * @brief The cube of distance faces, for a point light.
         *
         * Null means the light is lit but casts no shadow, which is a legitimate configuration --
         * a fill light rarely needs one.
         */
        TextureCube* ShadowCube = nullptr;

        /** @brief The distance map, for a spot light. Null means the light casts no shadow. */
        Texture2D* ShadowMap = nullptr;

        /** @brief World space to the spot map's clip space. Ignored for a point light. */
        Matrix ShadowViewProjection{};

        /** @brief Depth bias, as a fraction of @ref Range -- the units the map stores. */
        float ShadowDepthBias = 0.004f;
    };

} // namespace Microsoft::Xna::Framework::Graphics
