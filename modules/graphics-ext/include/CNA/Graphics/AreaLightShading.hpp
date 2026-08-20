// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    struct AreaLightEXT;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Shading a surface with a light that has area, on the CPU and in GLSL.
     *
     * The integral over a light's surface has a closed form when the reflectance being integrated
     * is a **clamped cosine**: the irradiance a polygon delivers is a sum of one term per edge. That
     * identity is the whole basis of linearly transformed cosines, and it does not need the fitted
     * matrix `MOD-2061` refuses to generate -- the fit only decides how well a *cosine* stands in
     * for a GGX lobe. So:
     *
     * - **Diffuse is exact.** A Lambertian surface reflects a clamped cosine, so the polygon
     *   integral is the answer rather than an approximation of one.
     * - **Specular is a cosine lobe aimed along the BRDF's average reflection direction** and
     *   widened with roughness, with its energy taken from `AreaLightBrdfTable`. The shape is
     *   approximate -- a fitted matrix would skew the lobe as well as aiming it -- and the energy
     *   is not, which is the half a viewer notices.
     *
     * **The three shapes become one quad**, because the edge sum is what the maths is written for:
     * a rectangle is its own corners; a disc is an *area-matched* rectangle, its axes scaled by
     * `sqrt(pi)/2` so the two enclose the same area; and a tube is a quad turned to face the
     * surface being lit, which is what a cylinder looks like from anywhere. Each is an
     * approximation only in outline, never in energy.
     *
     * @ref getShadingGlsl emits the same arithmetic these functions run, so a shader and a test
     * cannot drift apart.
     */
    class AreaLightShading
    {
    public:
        /** @brief The four corners a light's outline becomes, in world space. */
        using Quad = std::array<Microsoft::Xna::Framework::Vector3, 4>;

        /**
         * @brief Returns the quad a light's outline becomes, seen from one surface point.
         *
         * @param light   The light.
         * @param surface The world-space point being lit; only a tube uses it, to turn to face.
         * @return The four corners, counter-clockwise about the light's own normal.
         */
        [[nodiscard]] static Quad quadOf(const Microsoft::Xna::Framework::Graphics::AreaLightEXT& light,
                                         const Microsoft::Xna::Framework::Vector3& surface);

        /**
         * @brief Returns the fraction of a clamped-cosine lobe the light covers.
         *
         * With @p lobeAxis equal to the surface normal and @p lobeScale 1, this is the diffuse form
         * factor and is exact. Aiming and narrowing the lobe is how the specular term is taken.
         *
         * @param quad      The light's outline.
         * @param surface   The world-space point being lit.
         * @param lobeAxis  The direction the lobe points, normalised.
         * @param lobeScale How wide the lobe is; 1 is a full clamped cosine, smaller is tighter.
         * @param twoSided  Whether the light emits from both faces.
         * @return The covered fraction, 0 to 1.
         */
        [[nodiscard]] static float coverage(const Quad& quad,
                                            const Microsoft::Xna::Framework::Vector3& surface,
                                            const Microsoft::Xna::Framework::Vector3& lobeAxis,
                                            float lobeScale, bool twoSided);

        /**
         * @brief Returns one area light's contribution to one surface point.
         *
         * @param light          The light.
         * @param surface        The world-space point being lit.
         * @param normal         The surface normal, normalised.
         * @param cameraPosition The world-space eye position.
         * @param baseColor      The surface's base colour.
         * @param metallic       How metallic the surface is.
         * @param roughness      The surface's roughness.
         * @return The contribution, unbounded above; zero when the light cannot reach the point.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 contribution(
            const Microsoft::Xna::Framework::Graphics::AreaLightEXT& light,
            const Microsoft::Xna::Framework::Vector3& surface,
            const Microsoft::Xna::Framework::Vector3& normal,
            const Microsoft::Xna::Framework::Vector3& cameraPosition,
            const Microsoft::Xna::Framework::Vector3& baseColor, float metallic, float roughness);

        /**
         * @brief Returns the specular lobe's width for a roughness.
         *
         * Exposed because it is the one number in the specular path that is a modelling choice
         * rather than a derivation, and a test that pins the lobe's behaviour should be able to
         * name it.
         *
         * @param roughness Surface roughness, 0 to 1.
         * @return The lobe scale, never zero.
         */
        [[nodiscard]] static float lobeScaleFor(float roughness);

        /**
         * @brief Returns the GLSL implementing the same shading.
         *
         * Defines `cnaAreaCoverage`, `cnaAreaQuad` and `cnaAreaContribution`, and expects the
         * uniforms `uAreaShape`, `uAreaPosition`, `uAreaRight`, `uAreaUp`, `uAreaColour`,
         * `uAreaRange` and `uAreaTwoSided`, plus `AreaLightBrdfTable`'s own lookup.
         *
         * @return The GLSL source, with no `#version` line.
         */
        [[nodiscard]] static std::string getShadingGlsl();
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
