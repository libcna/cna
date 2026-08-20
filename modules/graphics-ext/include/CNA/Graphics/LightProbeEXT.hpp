// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief What the light looks like arriving at one point in a scene, as nine coefficients.
     *
     * `ImageBasedLightEXT` lights a whole scene from one environment, applied uniformly: every
     * surface in the level receives the same ambient, whether it is standing in the doorway or at
     * the back of the cellar. A probe is the answer to *where*: it records the incoming light at one
     * position, and a grid of them (`LightProbeVolumeEXT`) makes indirect light vary through a
     * space.
     *
     * **Stored as second-order spherical harmonics**, nine coefficients per colour channel, and
     * that is what makes a grid affordable: an irradiance *cube* per probe would be kilobytes and
     * could not be interpolated between neighbours, while nine values can be averaged directly --
     * the average of two probes' coefficients *is* the projection of the average of their light.
     * Nothing else about the storage would allow trilinear blending at all.
     *
     * What nine coefficients cannot hold is detail. Irradiance is a very low-frequency signal -- it
     * is the environment convolved with a cosine lobe -- so second order is close to exact for it,
     * and correspondingly useless for anything sharp. A probe carries no reflections, no shadows
     * and no visibility; it is the *ambient* term, and `MOD-2083` is where the visibility problem
     * gets its own answer.
     */
    class LightProbeEXT
    {
    public:
        /** @brief Coefficients per channel: second-order spherical harmonics. */
        static constexpr int kCoefficientCount = 9;

        /** @brief Creates a probe at the origin with no light in it. */
        LightProbeEXT();

        /**
         * @brief Creates an unlit probe at a position.
         *
         * @param position The probe's world-space position.
         */
        explicit LightProbeEXT(const Microsoft::Xna::Framework::Vector3& position);

        /** @brief Returns the probe's world-space position. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getPosition() const;
        /**
         * @brief Sets the probe's world-space position.
         *
         * @param value The position. It does not change the light the probe holds -- moving a probe
         *              after it was captured moves where its *stale* light is applied.
         */
        void setPosition(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Returns one spherical-harmonic coefficient.
         *
         * @param index 0 to @ref kCoefficientCount - 1, in the usual (l, m) order: 0 is the
         *              constant term, 1..3 the linear ones, 4..8 the quadratic ones.
         * @return The coefficient, per channel.
         * @throws std::out_of_range When the index is outside that range.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getCoefficient(int index) const;

        /**
         * @brief Sets one spherical-harmonic coefficient.
         *
         * @param index 0 to @ref kCoefficientCount - 1.
         * @param value The coefficient, per channel.
         * @throws std::out_of_range When the index is outside that range.
         */
        void setCoefficient(int index, const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns every coefficient, in index order. */
        [[nodiscard]] const std::array<Microsoft::Xna::Framework::Vector3, kCoefficientCount>&
        getCoefficients() const;

        /**
         * @brief Returns the irradiance arriving on a surface with this normal.
         *
         * This is *irradiance*, not outgoing radiance: a Lambertian surface reflects
         * `albedo / pi` of it, and the caller applies that. Returning the reflected value instead
         * would bake an albedo into a probe that has nothing to do with any surface.
         *
         * @param normal The surface normal; it is normalised.
         * @return The irradiance, per channel, never negative.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 irradiance(
            const Microsoft::Xna::Framework::Vector3& normal) const;

        /** @brief Directions a probe records visibility along: +X, −X, +Y, −Y, +Z, −Z. */
        static constexpr int kVisibilityDirections = 6;

        /**
         * @brief Records how far the geometry is, on average, along one axis direction.
         *
         * **This is the answer to light leaking through walls**, which is the defect that makes a
         * naive probe grid unusable indoors: a probe in a lit room and a surface in the dark room
         * next door are neighbours in the grid and nothing about the grid knows there is a wall
         * between them. A probe that has recorded a wall one unit away cannot be lighting a surface
         * three units away in that direction, and @ref visibilityWeight is where that is said.
         *
         * Both moments are stored because a hard edge and a soft one need to behave differently: a
         * flat wall has almost no variance and cuts off sharply, while a cluttered direction has a
         * lot and fades. The pair is exactly a variance-shadow-map test, applied to a probe.
         *
         * @param direction          0 to @ref kVisibilityDirections − 1, in cube-face order.
         * @param meanDistance       Mean distance to geometry; non-positive means "unknown", which
         *                           is how a direction is left out of the test.
         * @param meanSquaredDistance Mean of the squared distance; clamped to at least the square
         *                           of the mean, since no distribution has negative variance.
         * @throws std::out_of_range When the direction is outside that range.
         */
        void setVisibility(int direction, float meanDistance, float meanSquaredDistance);

        /**
         * @brief Returns the recorded mean distance along one direction, or 0 when none was set.
         *
         * @param direction 0 to @ref kVisibilityDirections − 1.
         * @return The mean distance.
         * @throws std::out_of_range When the direction is outside that range.
         */
        [[nodiscard]] float getVisibilityMean(int direction) const;

        /**
         * @brief Returns the recorded mean squared distance along one direction, or 0 when none.
         *
         * @param direction 0 to @ref kVisibilityDirections − 1.
         * @return The mean squared distance.
         * @throws std::out_of_range When the direction is outside that range.
         */
        [[nodiscard]] float getVisibilityMeanSquared(int direction) const;

        /** @brief Returns whether any direction has a recorded distance. */
        [[nodiscard]] bool hasVisibility() const;

        /**
         * @brief Returns how much this probe can be trusted to light a point in a direction.
         *
         * @param direction From the probe towards the point; it is normalised.
         * @param distance  How far the point is from the probe.
         * @return 1 where the probe plainly sees the point, 0 where geometry is plainly between
         *         them, and a fade in between. **1 when the probe recorded no visibility at all**,
         *         because a probe that was never told about its surroundings must not become
         *         useless -- an untested probe leaks, which is what it did before this existed.
         */
        [[nodiscard]] float visibilityWeight(
            const Microsoft::Xna::Framework::Vector3& direction, float distance) const;

        /** @brief Returns whether every coefficient is zero, so the probe adds nothing. */
        [[nodiscard]] bool isZero() const;

        /**
         * @brief Scales every coefficient.
         *
         * @param factor The multiplier; negatives are ignored, since a probe cannot hold negative
         *               light and a sign flip there produces a surface lit from the inside.
         */
        void scale(float factor);

        /**
         * @brief Compares position and every coefficient exactly.
         *
         * @param other The probe to compare with.
         * @return True when the two describe the same light at the same place.
         */
        [[nodiscard]] bool operator==(const LightProbeEXT& other) const;

        /**
         * @brief The negation of @ref operator==.
         *
         * @param other The probe to compare with.
         * @return True when the two differ.
         */
        [[nodiscard]] bool operator!=(const LightProbeEXT& other) const;

        /**
         * @brief Returns the GLSL that evaluates the same irradiance.
         *
         * Defines `cnaProbeIrradiance(vec3 coefficients[9], vec3 normal)`, so a shader that has the
         * nine values in hand -- from a uniform array or read out of a volume texture -- evaluates
         * them with the arithmetic this class runs and cannot drift from it.
         *
         * @return The GLSL source, with no `#version` line.
         */
        [[nodiscard]] static std::string getEvaluationGlsl();

    private:
        Microsoft::Xna::Framework::Vector3 position_{0.0f, 0.0f, 0.0f};
        std::array<Microsoft::Xna::Framework::Vector3, kCoefficientCount> coefficients_{};
        std::array<float, kVisibilityDirections> visibilityMean_{};
        std::array<float, kVisibilityDirections> visibilityMeanSquared_{};
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
