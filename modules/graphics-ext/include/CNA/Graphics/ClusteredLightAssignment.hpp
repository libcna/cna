// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <span>
#include <vector>

namespace CNA::Graphics {

    class ClusteredLightGrid;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Which lights reach which cluster, worked out on the CPU.
     *
     * `ClusteredLightGrid` says where the clusters are; this says what is in them. A light is put
     * into every cluster its bounding sphere touches, and the result is stored the way the GPU will
     * eventually want it: one flat array of light indices, plus an offset and a count per cluster.
     * That layout is the point -- a shader cannot follow a pointer, so the per-cluster lists have
     * to be contiguous with a table saying where each one starts.
     *
     * **The test is a sphere against the cluster's axis-aligned bounds**, which is conservative in
     * two stacked ways: the bounds are already a box around a frustum shape, and a sphere is
     * already a box around a light's reach. Both err towards including a light that only nearly
     * touches, which costs an iteration of the shader's light loop. Neither can drop a light, which
     * would be a visible hole in the lighting.
     *
     * Assignment is narrowed by depth before it starts: a sphere spans a known range of view
     * distances, so only the slices covering that range are visited at all. The screen axes are
     * scanned in full within those slices, because a light behind the camera or straddling the
     * near plane has no honest screen-space extent to narrow by.
     */
    class ClusteredLightAssignment
    {
    public:
        /** @brief The largest number of lights one assignment may hold. */
        static constexpr int kMaxLights = 1024;

        /** @brief Creates an empty assignment holding no lights and no clusters. */
        ClusteredLightAssignment();

        /**
         * @brief Sorts a set of world-space light volumes into a grid's clusters.
         *
         * Any previous result is replaced. The grid is read, not retained.
         *
         * @param grid   The grid to sort into; it must already have a projection.
         * @param view   The camera's view matrix, used to bring the spheres into view space.
         * @param lights The lights' bounding spheres, in world space; index @c i in the result
         *               refers to entry @c i here.
         * @throws std::invalid_argument When there are more than @ref kMaxLights lights.
         * @throws std::runtime_error    When the grid has no projection.
         */
        void assign(const ClusteredLightGrid& grid,
                    const Microsoft::Xna::Framework::Matrix& view,
                    const std::vector<Microsoft::Xna::Framework::BoundingSphere>& lights);

        /** @brief Drops every light and every cluster, as if freshly constructed. */
        void clear();

        /**
         * @brief Takes over a result produced elsewhere.
         *
         * The GPU path (`ClusteredLightCompute`) builds the same two arrays and needs somewhere to
         * put them; without this it would either duplicate every accessor or reach into private
         * state. The arrays are validated on the way in -- offsets monotone, one longer than the
         * cluster count, ending at the index count, and every index naming a light that exists --
         * because an assignment that disagrees with itself lights the wrong objects rather than
         * failing.
         *
         * @param lightCount How many lights the indices refer to.
         * @param offsets    One entry per cluster plus one; see @ref getOffsets.
         * @param indices    The flat index array; see @ref getIndices.
         * @throws std::invalid_argument When the two arrays are not a consistent assignment.
         */
        void adopt(int lightCount, std::vector<int> offsets, std::vector<int> indices);

        /** @brief Returns the number of lights the last assignment was given. */
        [[nodiscard]] int getLightCount() const;
        /** @brief Returns the number of clusters the last assignment filled. */
        [[nodiscard]] int getClusterCount() const;

        /**
         * @brief Returns the light indices reaching one cluster, in increasing order.
         *
         * @param clusterIndex The linear index from `ClusteredLightGrid::clusterIndex`.
         * @return The light indices; empty when no light reaches the cluster.
         * @throws std::out_of_range When the index is outside the assigned range.
         */
        [[nodiscard]] std::span<const int> lightsInCluster(int clusterIndex) const;

        /**
         * @brief Returns the flat index array every cluster's list is a window into.
         *
         * This is what gets uploaded; @ref getOffsets says where each cluster's window begins.
         */
        [[nodiscard]] const std::vector<int>& getIndices() const;

        /**
         * @brief Returns the start of each cluster's window into @ref getIndices.
         *
         * It has one more entry than there are clusters, so a cluster's count is the difference
         * between its own offset and the next one and no separate count array is needed.
         */
        [[nodiscard]] const std::vector<int>& getOffsets() const;

        /** @brief Returns the total number of light references stored across all clusters. */
        [[nodiscard]] int getTotalReferenceCount() const;

        /** @brief Returns the largest number of lights any one cluster received. */
        [[nodiscard]] int getMaxLightsPerCluster() const;

    private:
        std::vector<int> indices_;
        std::vector<int> offsets_{0};
        int lightCount_  = 0;
        int clusterCount_ = 0;
        int maxPerCluster_ = 0;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
