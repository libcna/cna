// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstddef>
#include <vector>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Filters a list of bounds down to the ones a camera can see.
     *
     * plan_modern.md `MOD-1409`. Built on XNA's own `BoundingFrustum` rather than on planes
     * extracted here: the test is `BoundingFrustum::Intersects`, which already exists and is
     * already tested, and a second implementation would only be a second thing to get wrong.
     *
     * What this adds over calling that directly is the shape a frame actually needs -- one
     * view-projection set once, then a sweep over many bounds that writes indices into a buffer
     * the caller reuses, so culling ten thousand objects allocates nothing after the first frame.
     */
    class FrustumCullerEXT
    {
    public:
        /** @brief Constructs a culler whose frustum is the identity view-projection. */
        FrustumCullerEXT();

        /**
         * @brief Sets the camera to cull against.
         *
         * @param viewProjection The camera's view matrix times its projection matrix.
         */
        void setViewProjection(const Microsoft::Xna::Framework::Matrix& viewProjection);

        /**
         * @brief Sets the camera from its two matrices, in the order XNA multiplies them.
         *
         * @param view       The view matrix.
         * @param projection The projection matrix.
         */
        void setCamera(const Microsoft::Xna::Framework::Matrix& view,
                       const Microsoft::Xna::Framework::Matrix& projection);

        /** @brief Returns the frustum currently being culled against. */
        [[nodiscard]] const Microsoft::Xna::Framework::BoundingFrustum& getFrustum() const;

        /**
         * @brief Returns whether one box is at least partly inside the frustum.
         *
         * @param box The bounds to test.
         * @return True when any part of it is visible.
         */
        [[nodiscard]] bool isVisible(const Microsoft::Xna::Framework::BoundingBox& box) const;

        /**
         * @brief Returns whether one sphere is at least partly inside the frustum.
         *
         * @param sphere The bounds to test.
         * @return True when any part of it is visible.
         */
        [[nodiscard]] bool isVisible(const Microsoft::Xna::Framework::BoundingSphere& sphere) const;

        /**
         * @brief Writes the indices of every visible box into @p visibleIndices.
         *
         * The output is cleared but its capacity is kept, which is the whole point: a game calling
         * this every frame with the same vector stops allocating after the first one.
         *
         * @param bounds         The bounds to test, in the caller's own order.
         * @param visibleIndices Receives the indices of the visible entries, ascending.
         * @return How many entries were visible.
         */
        std::size_t cull(const std::vector<Microsoft::Xna::Framework::BoundingBox>& bounds,
                         std::vector<std::size_t>& visibleIndices) const;

        /**
         * @brief Writes the indices of every visible sphere into @p visibleIndices.
         *
         * @param bounds         The bounds to test.
         * @param visibleIndices Receives the indices of the visible entries, ascending.
         * @return How many entries were visible.
         */
        std::size_t cull(const std::vector<Microsoft::Xna::Framework::BoundingSphere>& bounds,
                         std::vector<std::size_t>& visibleIndices) const;

        /**
         * @brief Keeps only the transforms whose bounds are visible, in their original order.
         *
         * plan_modern.md `MOD-1410`: the composition an instanced draw wants, since uploading a
         * transform for an object nobody can see costs the same as one they can. The two input
         * vectors are parallel; a shorter bounds list means the extra transforms are treated as
         * having no bounds, and are kept -- dropping geometry because a caller forgot a bound
         * would be the more surprising of the two failures.
         *
         * @param transforms       One world matrix per object.
         * @param bounds           One world-space bound per object.
         * @param visibleTransforms Receives the visible transforms, in input order.
         * @return How many transforms were kept.
         */
        std::size_t cullTransforms(
            const std::vector<Microsoft::Xna::Framework::Matrix>& transforms,
            const std::vector<Microsoft::Xna::Framework::BoundingBox>& bounds,
            std::vector<Microsoft::Xna::Framework::Matrix>& visibleTransforms) const;

    private:
        Microsoft::Xna::Framework::BoundingFrustum frustum_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
