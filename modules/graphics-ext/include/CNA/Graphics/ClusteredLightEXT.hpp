// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightType.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief One point or spot light, in the single shape a clustered light list needs.
     *
     * `PointLightEXT` and `SpotLightEXT` stay as they are and remain what a single shadow-casting
     * light is described with. This is the *uniform* record: clustered shading gives every light
     * one index in one list, so the list cannot hold two different structures, and a light's type
     * has to be a field rather than a C++ type. Converting is the job of
     * `ClusteredLightSetEXT::add`, so a game keeps writing the specific type it means.
     *
     * **Not to be confused with `Microsoft::Xna::Framework::Graphics::PunctualLightEXT`**, which is
     * the *one* punctual light a lit effect receives for a draw, shadow resources included. That
     * one is a budget of one by deliberate design; this one is a member of a list of hundreds and
     * carries no shadow at all. Two names for two different jobs, rather than one name meaning
     * both -- the first draft of this file called itself `PunctualLightEXT` too, which would have
     * left two types with the same name in two namespaces describing different things.
     *
     * A point light leaves @ref Direction, @ref InnerAngle and @ref OuterAngle unread.
     */
    struct ClusteredLightEXT
    {
        /** @brief Which kind of light this is; decides whether the cone fields are read. */
        ClusteredLightType Type = ClusteredLightType::Point;

        /** @brief World-space position of the light. */
        Microsoft::Xna::Framework::Vector3 Position{0.0f, 0.0f, 0.0f};

        /** @brief The direction a spot light's cone points, in world space; unread for a point light. */
        Microsoft::Xna::Framework::Vector3 Direction{0.0f, -1.0f, 0.0f};

        /** @brief Light colour, linear, unbounded (values above 1 are meaningful in an HDR scene). */
        Microsoft::Xna::Framework::Vector3 Color{1.0f, 1.0f, 1.0f};

        /** @brief Multiplier applied to @ref Color. */
        float Intensity = 1.0f;

        /**
         * @brief Distance past which the light contributes nothing.
         *
         * It is the radius of the volume the light is sorted into clusters by, so a range far
         * larger than the light actually reaches costs light-loop iterations in every cluster it
         * falsely claims.
         */
        float Range = 20.0f;

        /** @brief Half-angle of a spot's fully lit centre, in radians; unread for a point light. */
        float InnerAngle = 0.35f;

        /** @brief Half-angle at which a spot's cone ends, in radians; unread for a point light. */
        float OuterAngle = 0.5f;

        /**
         * @brief Whether this light should have a shadow map.
         *
         * A request rather than a guarantee: a clustered scene has far more lights than there are
         * shadow maps to give them, and `MOD-2047` states which requests are honoured.
         */
        bool CastsShadows = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
