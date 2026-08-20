// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    class CascadedShadowMap;
    class ClusteredLightGrid;
    class DebugDraw;
    class LightProbeVolumeEXT;
    struct DirectionalLightEXT;
    struct PointLightEXT;
    struct SpotLightEXT;

    /**
     * @brief Wireframe gizmos for the engine layer's own invisible structures.
     *
     * plan_modern.md `MOD-2161`. This is the tool Phase 20 wanted and did without: a light's reach, a
     * probe grid's spacing, a cluster's depth slices and a cascade's fitted volume are all decisions
     * the layer makes silently, and every one of them was checked by arithmetic because there was no
     * way to look at one.
     *
     * Free functions rather than methods on the structures, and deliberately so — nothing here is
     * part of what a `PointLightEXT` *is*, and a light that knew how to draw itself would carry a
     * debug helper into every build that never asks for one.
     *
     * Every function submits into a @ref DebugDraw batch the caller has already opened, so gizmos
     * mix freely with a game's own shapes and share the same one or two draw calls.
     */

    /**
     * @brief Draws a point light's reach: a sphere at its range, and a cross at its position.
     *
     * The sphere is the whole claim a point light makes — beyond `Range` it contributes nothing —
     * and it is the number most often set to something that turns out not to cover the room.
     *
     * @param debug  The open batch to submit into.
     * @param light  The light to draw.
     * @param colour The colour to draw it in.
     */
    void addPointLightGizmo(DebugDraw& debug, const PointLightEXT& light,
                            const Microsoft::Xna::Framework::Color& colour);

    /**
     * @brief Draws a spot light's cone: its outer edge, its inner edge, and the axis.
     *
     * Both cones, because the gap between them is the falloff and a spot whose inner angle has crept
     * up to the outer one has no soft edge left — which looks like a hard-edged light rather than
     * like a wrong number.
     *
     * @param debug    The open batch to submit into.
     * @param light    The light to draw.
     * @param colour   The colour to draw it in.
     * @param segments Lines in each base ring; clamped to 4..128.
     */
    void addSpotLightGizmo(DebugDraw& debug, const SpotLightEXT& light,
                           const Microsoft::Xna::Framework::Color& colour, int segments = 24);

    /**
     * @brief Draws a directional light as an arrow through a point.
     *
     * A directional light has no position, so the caller supplies one — normally the point of
     * interest in the scene. The arrow points the way the light *travels*, matching
     * @ref DirectionalLightEXT::Direction rather than the shading convention.
     *
     * @param debug  The open batch to submit into.
     * @param light  The light to draw.
     * @param at     Where to draw the arrow.
     * @param length The arrow's length in world units.
     * @param colour The colour to draw it in.
     */
    void addDirectionalLightGizmo(DebugDraw& debug, const DirectionalLightEXT& light,
                                  const Microsoft::Xna::Framework::Vector3& at, float length,
                                  const Microsoft::Xna::Framework::Color& colour);

    /**
     * @brief Draws a probe volume's bounds and a cross at each probe.
     *
     * The spacing is the point: irradiance is interpolated between probes, so a volume whose grid is
     * coarser than the geometry inside it leaks light through walls, and the grid is the only way to
     * see that before it happens.
     *
     * @param debug     The open batch to submit into.
     * @param volume    The volume to draw.
     * @param colour    The colour to draw it in.
     * @param crossSize Half the length of each probe marker's arms.
     */
    void addProbeVolumeGizmo(DebugDraw& debug, const LightProbeVolumeEXT& volume,
                             const Microsoft::Xna::Framework::Color& colour,
                             float crossSize = 0.1f);

    /**
     * @brief Draws a clustered grid's depth slices as boxes in world space.
     *
     * The slices and not the tiles: a grid of 16 by 8 tiles over 24 slices is 3072 boxes, which is
     * an unreadable thicket, while the 24 slice boundaries show the thing worth seeing — that the
     * slicing is exponential, so the near slices are thin and the far ones enormous.
     *
     * The grid's bounds are in view space, so a view matrix is needed to place them. A view-space box
     * is **not** axis-aligned once it reaches world space, so each is drawn from its eight
     * transformed corners rather than as a `BoundingBox`.
     *
     * @param debug       The open batch to submit into.
     * @param grid        The grid to draw; it must have a projection set.
     * @param inverseView The inverse of the camera's view matrix.
     * @param colour      The colour to draw it in.
     */
    void addClusterSliceGizmo(DebugDraw& debug, const ClusteredLightGrid& grid,
                              const Microsoft::Xna::Framework::Matrix& inverseView,
                              const Microsoft::Xna::Framework::Color& colour);

    /**
     * @brief Draws each cascade's fitted light volume as a frustum.
     *
     * What a cascade set actually decided, which is otherwise invisible: how much world each level
     * covers and how much they overlap. A cascade fitted far larger than its split needs is
     * resolution thrown away, and it looks like nothing at all in the rendered frame.
     *
     * @param debug    The open batch to submit into.
     * @param cascades The cascade set, after `update` has fitted it.
     * @param colour   The colour to draw it in.
     */
    void addCascadeGizmo(DebugDraw& debug, const CascadedShadowMap& cascades,
                         const Microsoft::Xna::Framework::Color& colour);

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
