// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/LightProbeEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <vector>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief A grid of light probes, blended so indirect light varies through a space.
     *
     * One probe answers "what does the light look like *here*". A volume answers it everywhere
     * inside a box, by holding a probe at each corner of a regular grid and blending the eight that
     * surround any point. Because a probe is nine coefficients and the projection onto them is
     * linear, blending is an ordinary trilinear interpolation of those coefficients -- and the
     * result is itself a valid probe, not an approximation of one.
     *
     * **A point outside the volume clamps to the edge** rather than falling back to nothing. A
     * character stepping one unit past the last probe should not lose its ambient light entirely;
     * clamping keeps the nearest answer, which is wrong slowly instead of wrong suddenly.
     *
     * The volume knows nothing about walls. Two probes either side of one blend straight through
     * it, which is the defect that makes naive probe grids unusable indoors -- `MOD-2083` is where
     * that gets its own answer.
     */
    class LightProbeVolumeEXT
    {
    public:
        /** @brief The largest number of probes one volume may hold. */
        static constexpr int kMaxProbes = 32768;

        /**
         * @brief Creates a volume of unlit probes spread over a box.
         *
         * The probes sit at the grid's *corners*, so a count of 2 on an axis puts one probe at each
         * face of the box and a count of 1 puts a single probe at the box's near edge on that axis.
         *
         * @param bounds The world-space box the grid spans.
         * @param countX Probes along x; at least 1.
         * @param countY Probes along y; at least 1.
         * @param countZ Probes along z; at least 1.
         * @throws std::invalid_argument When a count is below 1, the product exceeds
         *         @ref kMaxProbes, or the box is inverted on any axis.
         */
        LightProbeVolumeEXT(const Microsoft::Xna::Framework::BoundingBox& bounds, int countX,
                            int countY, int countZ);

        /** @brief Returns the world-space box the grid spans. */
        [[nodiscard]] Microsoft::Xna::Framework::BoundingBox getBounds() const;

        /** @brief Returns the probe count along x. */
        [[nodiscard]] int getCountX() const;
        /** @brief Returns the probe count along y. */
        [[nodiscard]] int getCountY() const;
        /** @brief Returns the probe count along z. */
        [[nodiscard]] int getCountZ() const;
        /** @brief Returns the total probe count, which is the product of the three. */
        [[nodiscard]] int getProbeCount() const;

        /**
         * @brief Returns where one grid position sits in the world.
         *
         * @param x Grid index along x.
         * @param y Grid index along y.
         * @param z Grid index along z.
         * @return The world-space position.
         * @throws std::out_of_range When any index is outside the grid.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getProbePosition(int x, int y,
                                                                         int z) const;

        /**
         * @brief Returns one probe.
         *
         * @param x Grid index along x.
         * @param y Grid index along y.
         * @param z Grid index along z.
         * @return The probe.
         * @throws std::out_of_range When any index is outside the grid.
         */
        [[nodiscard]] const LightProbeEXT& getProbe(int x, int y, int z) const;

        /**
         * @brief Replaces one probe, keeping the grid position it was created with.
         *
         * The stored probe's own position is overwritten with the grid position, because a probe
         * placed somewhere other than where the grid says it is makes the interpolation weights
         * describe one arrangement and the light another.
         *
         * @param x     Grid index along x.
         * @param y     Grid index along y.
         * @param z     Grid index along z.
         * @param probe The probe.
         * @throws std::out_of_range When any index is outside the grid.
         */
        void setProbe(int x, int y, int z, const LightProbeEXT& probe);

        /** @brief Returns whether a world-space point lies inside the volume's box. */
        [[nodiscard]] bool contains(const Microsoft::Xna::Framework::Vector3& position) const;

        /**
         * @brief Returns the blended probe at a world-space point.
         *
         * @param position The world-space point; outside the box it clamps to the edge.
         * @return A probe whose coefficients are the trilinear blend of the eight around the point,
         *         and whose position is @p position clamped into the box.
         */
        [[nodiscard]] LightProbeEXT sampleProbe(
            const Microsoft::Xna::Framework::Vector3& position) const;

        /**
         * @brief Returns the irradiance at a point on a surface facing one way.
         *
         * @param position The world-space point.
         * @param normal   The surface normal.
         * @return The irradiance, per channel.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 irradiance(
            const Microsoft::Xna::Framework::Vector3& position,
            const Microsoft::Xna::Framework::Vector3& normal) const;

        /** @brief Returns whether every probe in the volume is unlit. */
        [[nodiscard]] bool isZero() const;

    private:
        [[nodiscard]] int indexOf(int x, int y, int z) const;

        Microsoft::Xna::Framework::BoundingBox bounds_;
        int countX_ = 0;
        int countY_ = 0;
        int countZ_ = 0;
        std::vector<LightProbeEXT> probes_;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
