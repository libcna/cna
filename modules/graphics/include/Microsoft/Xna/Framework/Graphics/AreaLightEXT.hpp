// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    /** @brief Which shape an `AreaLightEXT` emits from. */
    CNAEXT enum class AreaLightShapeEXT
    {
        /** @brief A rectangle, described by its centre and two half-axes. */
        Rectangle,
        /** @brief A disc in the plane of the same two half-axes; approximated as a polygon. */
        Disc,
        /** @brief A capsule-like tube along the first half-axis, with the second as its radius. */
        Tube,
    };

    /**
     * @brief A light with a shape, as a lit effect receives one.
     *
     * CNAEXT: not part of the XNA 4.0 API. XNA has directional lights and nothing else, and CNA
     * added `PunctualLightEXT` for lights that are a point in space. This is the third kind: a
     * light that is a *surface*, which is what almost every real light is.
     *
     * The difference is not brightness, it is the shape of what the light does. A punctual light
     * puts a point highlight on a smooth surface and casts a shadow with a hard edge, because it
     * has no extent for either to soften. An area light's highlight is the shape of the light --
     * a window is a bright rectangle in a polished floor -- and that is not something a punctual
     * light can be tuned into producing.
     *
     * **In the XNA namespace, deliberately**, following `ImageBasedLightEXT` and `PunctualLightEXT`
     * (plans/plan_modern.md `MOD-1222`): an always-compiled XNA header must not include one that exists
     * only under `CNA_CNAEXT`, and an effect's public surface must not depend on a build flag.
     *
     * **The shape is described by a centre and two half-axes**, rather than by four corners, so
     * that all three shapes share one description and none of them can be given a non-planar or
     * self-intersecting outline. `RightAxis` and `UpAxis` are half-extents: their lengths are half
     * the rectangle's width and height, and the emitting side is the one they cross towards.
     *
     * **There are no area-light shadows.** A shadow with a soft edge needs either many samples of
     * the light's surface or a ray query, and neither exists in this layer; an area light lights
     * what faces it whether or not anything stands in the way. Stated here rather than left to be
     * discovered, because the failure looks like a bug in the shadow system.
     */
    CNAEXT struct AreaLightEXT
    {
        /** @brief Which shape the light emits from. */
        AreaLightShapeEXT Shape = AreaLightShapeEXT::Rectangle;

        /** @brief World-space centre of the emitting surface. */
        Vector3 Position{0.0f, 0.0f, 0.0f};

        /** @brief Half-axis across the surface; its length is half the width. */
        Vector3 RightAxis{0.5f, 0.0f, 0.0f};

        /** @brief Half-axis up the surface; its length is half the height, or the tube's radius. */
        Vector3 UpAxis{0.0f, 0.5f, 0.0f};

        /** @brief Emitted colour, linear, unbounded (values above 1 are meaningful in HDR). */
        Vector3 Color{1.0f, 1.0f, 1.0f};

        /** @brief Multiplier applied to @ref Color. */
        float Intensity = 1.0f;

        /**
         * @brief Distance past which the light contributes nothing.
         *
         * The radius of the volume the light is culled and clustered by, measured from
         * @ref Position, so it has to cover the light's own extent as well as its reach.
         */
        float Range = 20.0f;

        /**
         * @brief Whether the light emits from both faces of its surface.
         *
         * False is the usual case and the one that matches a window or a panel: a surface behind
         * the light receives nothing. True describes a light with no backing, such as a bare tube.
         */
        bool TwoSided = false;

        /**
         * @brief Returns whether the light describes a surface that can emit.
         *
         * A degenerate light -- zero range, zero-length axes, or axes that are parallel and so
         * enclose no area -- is refused by an effect rather than producing a division by zero in
         * the form factor.
         *
         * @return True when the light is usable.
         */
        CNAEXT [[nodiscard]] bool IsValidEXT() const;
    };

} // namespace Microsoft::Xna::Framework::Graphics
