// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"

#include <vector>

namespace CNA::Graphics {

    struct PointLightEXT;
    struct SpotLightEXT;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The lights a clustered frame is drawn with, as the application hands them over.
     *
     * A game's lights are its own; this is the boundary where they become a list with stable
     * indices, because that is what every stage downstream refers to them by -- the cluster
     * assignment stores indices, the uploaded buffer is in this order, and the shader's light loop
     * reads that order.
     *
     * **Adding validates.** A light with a non-positive range, a negative intensity, a cone whose
     * inner angle exceeds its outer, or any non-finite number in it is refused at the point it is
     * added rather than accepted and skipped later -- a light that silently does nothing is far
     * harder to find than one that refused to be added.
     *
     * **Removing renumbers.** `removeAt` closes the gap, so every light after it takes a new index;
     * a caller holding indices must re-read them. To switch a light off without renumbering, set
     * its intensity to zero, which is explicitly allowed and is why zero intensity is not refused.
     */
    class PunctualLightSetEXT
    {
    public:
        /**
         * @brief The largest number of lights one set may hold.
         *
         * The bound is what the uploaded light buffer and the shader's index width are sized from,
         * and it is deliberately a round number rather than the largest that would fit.
         */
        static constexpr int kMaxLights = 256;

        /** @brief Creates an empty set. */
        PunctualLightSetEXT();

        /**
         * @brief Adds a point light.
         *
         * @param light The light to add.
         * @return The index it was given.
         * @throws std::invalid_argument When the light is not usable.
         * @throws std::length_error     When the set already holds @ref kMaxLights lights.
         */
        int add(const PointLightEXT& light);

        /**
         * @brief Adds a spot light.
         *
         * @param light The light to add.
         * @return The index it was given.
         * @throws std::invalid_argument When the light is not usable.
         * @throws std::length_error     When the set already holds @ref kMaxLights lights.
         */
        int add(const SpotLightEXT& light);

        /**
         * @brief Adds a light already in the uniform shape.
         *
         * @param light The light to add.
         * @return The index it was given.
         * @throws std::invalid_argument When the light is not usable.
         * @throws std::length_error     When the set already holds @ref kMaxLights lights.
         */
        int add(const PunctualLightEXT& light);

        /**
         * @brief Replaces one light in place, keeping its index.
         *
         * @param index The index to replace.
         * @param light The new light.
         * @throws std::out_of_range     When the index is not in the set.
         * @throws std::invalid_argument When the light is not usable.
         */
        void replaceAt(int index, const PunctualLightEXT& light);

        /**
         * @brief Removes one light, renumbering every light after it.
         *
         * @param index The index to remove.
         * @throws std::out_of_range When the index is not in the set.
         */
        void removeAt(int index);

        /** @brief Removes every light. */
        void clear();

        /** @brief Returns how many lights the set holds. */
        [[nodiscard]] int getCount() const;

        /** @brief Returns whether the set holds no lights. */
        [[nodiscard]] bool isEmpty() const;

        /**
         * @brief Returns one light.
         *
         * @param index The index to read.
         * @return The light.
         * @throws std::out_of_range When the index is not in the set.
         */
        [[nodiscard]] const PunctualLightEXT& getAt(int index) const;

        /** @brief Returns every light, in index order. */
        [[nodiscard]] const std::vector<PunctualLightEXT>& getLights() const;

        /**
         * @brief Returns the volume a light can possibly reach, for the cluster assignment.
         *
         * A point light's volume is a sphere of its range. **A spot light's is the sphere around
         * its cone**, which is a good deal smaller for a narrow cone -- and it is not centred on
         * the light: a cone's bounding sphere sits out along the axis, which is what keeps a
         * torchlight out of the clusters behind the person holding it.
         *
         * @param index The index to bound.
         * @return The bounding sphere in world space.
         * @throws std::out_of_range When the index is not in the set.
         */
        [[nodiscard]] Microsoft::Xna::Framework::BoundingSphere getBoundsAt(int index) const;

        /**
         * @brief Returns every light's bounding volume, in index order.
         *
         * This is what `ClusteredLightAssignment::assign` takes, and the index agreement between
         * the two is the reason it is produced here rather than assembled by the caller.
         */
        [[nodiscard]] std::vector<Microsoft::Xna::Framework::BoundingSphere> collectBounds() const;

        /**
         * @brief Returns whether a light would be accepted by @ref add.
         *
         * Offered so a caller can check a light it built from data without catching an exception.
         *
         * @param light The light to inspect.
         * @return True when the light is usable.
         */
        [[nodiscard]] static bool isUsable(const PunctualLightEXT& light);

    private:
        std::vector<PunctualLightEXT> lights_;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
