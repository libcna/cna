// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The view frustum, cut into a grid of clusters that lights are sorted into.
     *
     * This is the half of clustered forward shading that has nothing to do with shading: it decides
     * *where* the clusters are. A fragment shader that wants to know which lights reach it needs a
     * cell it can find from `gl_FragCoord` and its own depth, and this is the definition of that
     * cell -- in view space, so the answer does not change when the camera moves.
     *
     * **The grid is a frustum, not a box.** A cluster is bounded by two screen-space tile edges and
     * two view distances, so its cross-section grows with distance exactly as the frustum does.
     * @ref clusterBounds returns the axis-aligned box around that shape, which is what a
     * sphere-versus-cluster test needs and is deliberately a little conservative: a light may be
     * assigned to a cluster it only *nearly* touches, which costs a wasted light-loop iteration,
     * never a missing light.
     *
     * **Slices are spaced exponentially**, not evenly. Even spacing puts almost every slice past
     * the middle distance, where a cluster is enormous and holds every light in the scene; the
     * exponential spacing gives each slice a constant ratio of far to near distance, so the near
     * clusters -- where geometry is dense and lights are close -- are the thin ones.
     *
     * The grid holds no lights and no device resources. It is the geometry; `MOD-2042` onwards put
     * lights into it.
     */
    class ClusteredLightGrid
    {
    public:
        /** @brief Tiles across the screen in the default grid. */
        static constexpr int kDefaultTilesX = 16;
        /** @brief Tiles down the screen in the default grid. */
        static constexpr int kDefaultTilesY = 8;
        /** @brief Depth slices in the default grid. */
        static constexpr int kDefaultSliceCount = 24;
        /** @brief The largest tile count either screen axis may have. */
        static constexpr int kMaxTilesPerAxis = 128;
        /** @brief The largest number of depth slices. */
        static constexpr int kMaxSliceCount = 256;

        /** @brief Creates the default 16 by 8 by 24 grid. */
        ClusteredLightGrid();

        /**
         * @brief Creates a grid of the given shape.
         *
         * @param tilesX     Tiles across the screen; 1 to @ref kMaxTilesPerAxis.
         * @param tilesY     Tiles down the screen; 1 to @ref kMaxTilesPerAxis.
         * @param sliceCount Depth slices; 1 to @ref kMaxSliceCount.
         * @throws std::invalid_argument When any dimension is outside its range.
         */
        ClusteredLightGrid(int tilesX, int tilesY, int sliceCount);

        /** @brief Returns the tile count across the screen. */
        [[nodiscard]] int getTilesX() const;
        /** @brief Returns the tile count down the screen. */
        [[nodiscard]] int getTilesY() const;
        /** @brief Returns the number of depth slices. */
        [[nodiscard]] int getSliceCount() const;
        /** @brief Returns the total number of clusters, which is the product of the three. */
        [[nodiscard]] int getClusterCount() const;

        /**
         * @brief Returns the linear index of one cluster.
         *
         * The order is x fastest, then y, then slice, so a whole depth slice is contiguous. That is
         * the order the light-list texture is laid out in, and it is stated here because a shader
         * computing the index by hand has to agree with it.
         *
         * @param x     Tile column, 0 to `getTilesX() - 1`.
         * @param y     Tile row, 0 to `getTilesY() - 1`.
         * @param slice Depth slice, 0 to `getSliceCount() - 1`.
         * @return The linear cluster index.
         * @throws std::out_of_range When any coordinate is outside the grid.
         */
        [[nodiscard]] int clusterIndex(int x, int y, int slice) const;

        /**
         * @brief Sets the camera the grid describes.
         *
         * @param projection The projection matrix; the tile shapes come from its inverse, so an
         *                   orthographic projection produces a box grid and works too.
         * @param nearPlane  The nearest view distance the grid covers; must be positive.
         * @param farPlane   The furthest view distance; must exceed @p nearPlane.
         * @throws std::invalid_argument When the distances are not a positive increasing pair, or
         *                               the projection cannot be inverted.
         */
        void setProjection(const Microsoft::Xna::Framework::Matrix& projection, float nearPlane,
                           float farPlane);

        /** @brief Returns the near distance the grid starts at. */
        [[nodiscard]] float getNearPlane() const;
        /** @brief Returns the far distance the grid ends at. */
        [[nodiscard]] float getFarPlane() const;
        /** @brief Returns whether @ref setProjection has been called with a usable camera. */
        [[nodiscard]] bool hasProjection() const;

        /**
         * @brief Returns the view distance where a slice begins.
         *
         * @param slice Depth slice, 0 to `getSliceCount()` -- the count itself is accepted and
         *              answers the far plane, so a caller can ask for a slice's two ends as
         *              `sliceDistance(k)` and `sliceDistance(k + 1)`.
         * @return The positive view distance.
         * @throws std::out_of_range When the slice is outside that range.
         */
        [[nodiscard]] float sliceDistance(int slice) const;

        /**
         * @brief Returns the slice a view distance falls in.
         *
         * Distances nearer than the near plane answer 0 and distances past the far plane answer the
         * last slice, because a shader will hand this both and clamping is the only useful answer:
         * geometry in front of the grid is still lit by the nearest cluster's lights.
         *
         * @param viewDistance Distance along the view direction, positive.
         * @return The slice index, 0 to `getSliceCount() - 1`.
         */
        [[nodiscard]] int sliceForViewDistance(float viewDistance) const;

        /**
         * @brief Returns the axis-aligned bounds of one cluster, in view space.
         *
         * View space here is XNA's: the camera at the origin looking down −Z, so the box's Z range
         * is negative and `Min.Z` is the *far* end.
         *
         * @param x     Tile column.
         * @param y     Tile row.
         * @param slice Depth slice.
         * @return The bounds in view space.
         * @throws std::out_of_range   When any coordinate is outside the grid.
         * @throws std::runtime_error  When no projection has been set.
         */
        [[nodiscard]] Microsoft::Xna::Framework::BoundingBox clusterBounds(int x, int y,
                                                                          int slice) const;

    private:
        int tilesX_;
        int tilesY_;
        int sliceCount_;

        Microsoft::Xna::Framework::Matrix inverseProjection_{};
        float nearPlane_ = 0.0f;
        float farPlane_  = 0.0f;
        bool  hasProjection_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
