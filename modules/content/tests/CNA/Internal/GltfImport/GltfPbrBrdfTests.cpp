// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-235: renderer-independent analytic spot checks for the direct-light
// metallic-roughness BRDF used by every CNA PBR shader. These are specification values, not a
// captured image: each test pins one term so a plausible alternative formula cannot accidentally
// preserve the final colour.

#include <algorithm>
#include <array>
#include <cmath>

#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/PbrFresnelEXT.hpp"

namespace
{
    constexpr double kShaderPi = 3.14159265;
    constexpr double kGeometryPi = 3.14159265358979323846;

    struct BrdfTerms
    {
        double distribution = 0.0;
        double geometry = 0.0;
        std::array<double, 3> f0{};
        std::array<double, 3> fresnel{};
        std::array<double, 3> directLight{};
    };

    // glTF Appendix B's direct-light form, expressed in dot products so the tests can choose
    // exact analytic configurations without duplicating vector normalization code. CNA's shaders
    // use the same small denominator guards; the selected cases are far from either guard.
    BrdfTerms EvaluateDirectLight(
        const std::array<double, 3>& albedo, double metallic, double roughness,
        double nDotL, double nDotV, double nDotH, double vDotH)
    {
        BrdfTerms out;

        const double alphaSquared = std::pow(roughness, 4.0);
        const double dDenominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
        out.distribution = alphaSquared /
            (kShaderPi * dDenominator * dDenominator + 1e-7);

        const double k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        const auto smith = [k](double nDotX) {
            return nDotX / (nDotX * (1.0 - k) + k);
        };
        out.geometry = smith(nDotV) * smith(nDotL);

        for (std::size_t channel = 0; channel < out.directLight.size(); ++channel)
        {
            out.f0[channel] = 0.04 * (1.0 - metallic) + albedo[channel] * metallic;
            out.fresnel[channel] = out.f0[channel] + (1.0 - out.f0[channel]) *
                std::pow(std::clamp(1.0 - vDotH, 0.0, 1.0), 5.0);

            const double specular =
                out.distribution * out.geometry * out.fresnel[channel] /
                std::max(4.0 * nDotV * nDotL, 1e-4);
            const double diffuse = albedo[channel] * (1.0 - metallic);
            const double diffuseWeight = 1.0 - out.fresnel[channel];
            out.directLight[channel] =
                (diffuseWeight * diffuse / kShaderPi + specular) * nDotL;
        }
        return out;
    }
}

TEST(GltfPbrBrdf, NormalIncidencePinsEveryAppendixBTerm)
{
    const BrdfTerms terms = EvaluateDirectLight(
        {1.0, 1.0, 1.0}, 0.0, 0.5,
        /*NdotL=*/1.0, /*NdotV=*/1.0, /*NdotH=*/1.0, /*VdotH=*/1.0);

    EXPECT_NEAR(terms.distribution, 5.092916684, 1e-9);
    EXPECT_NEAR(terms.geometry, 1.0, 1e-12);
    EXPECT_NEAR(terms.f0[0], 0.04, 1e-12);
    EXPECT_NEAR(terms.fresnel[0], 0.04, 1e-12);
    EXPECT_NEAR(terms.directLight[0], 0.356506658, 1e-9);
}

TEST(GltfPbrBrdf, GrazingIncidenceRaisesSchlickFresnelToTheAnalyticValue)
{
    // V and L are 80 degrees from N on opposite sides. H is therefore exactly N:
    // NdotL=NdotV=VdotH=cos(80 degrees), NdotH=1.
    const double cosine80 = std::cos(80.0 * kGeometryPi / 180.0);
    const BrdfTerms terms = EvaluateDirectLight(
        {1.0, 1.0, 1.0}, 0.0, 0.5,
        cosine80, cosine80, 1.0, cosine80);

    EXPECT_NEAR(terms.fresnel[0], 0.409910091, 1e-9);
    EXPECT_NEAR(terms.directLight[0], 0.582266037, 1e-9);
    EXPECT_GT(terms.fresnel[0], 0.04);
}

TEST(GltfPbrBrdf, DirectLightingUsesTheRoughnessPlusOneSmithGeometryTerm)
{
    const double cosine80 = std::cos(80.0 * kGeometryPi / 180.0);
    const BrdfTerms terms = EvaluateDirectLight(
        {1.0, 1.0, 1.0}, 0.0, 0.5,
        cosine80, cosine80, 1.0, cosine80);

    // 0.1828777 is k=(roughness+1)^2/8. The IBL form k=roughness^2/2 would be about 0.3759 here,
    // so this separates the two formulas instead of merely checking that G is in [0,1].
    EXPECT_NEAR(terms.geometry, 0.182877736, 1e-9);
}

TEST(GltfPbrBrdf, F0InterpolatesFromFourPercentDielectricToMetalAlbedo)
{
    const BrdfTerms dielectric = EvaluateDirectLight(
        {1.0, 0.25, 0.0}, 0.0, 0.5, 1.0, 1.0, 1.0, 1.0);
    const BrdfTerms halfMetal = EvaluateDirectLight(
        {1.0, 0.25, 0.0}, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0);
    const BrdfTerms metal = EvaluateDirectLight(
        {1.0, 0.25, 0.0}, 1.0, 0.5, 1.0, 1.0, 1.0, 1.0);

    const auto expectF0 = [](const BrdfTerms& terms, const std::array<double, 3>& expected) {
        for (std::size_t channel = 0; channel < expected.size(); ++channel)
        {
            EXPECT_NEAR(terms.f0[channel], expected[channel], 1e-12);
        }
    };
    expectF0(dielectric, {0.04, 0.04, 0.04});
    expectF0(halfMetal, {0.52, 0.145, 0.02});
    expectF0(metal, {1.0, 0.25, 0.0});
}

TEST(GltfPbrBrdf, IorAndSpecularInteractionClampsColourBeforeStrength)
{
    using CNA::Internal::Graphics::ComputePbrDielectricFresnelEXT;

    const auto core = ComputePbrDielectricFresnelEXT(1.5f, 1.0f, {1.0f, 1.0f, 1.0f});
    EXPECT_NEAR(core.f0[0], 0.04f, 1e-7f);
    EXPECT_NEAR(core.f0[1], 0.04f, 1e-7f);
    EXPECT_NEAR(core.f0[2], 0.04f, 1e-7f);
    EXPECT_FLOAT_EQ(core.f90, 1.0f);

    // IOR 2 gives F0=1/9. Blue's product is 4/3, so the specified clamp-before-strength order
    // yields 0.3. Multiplying strength first and clamping afterwards would incorrectly yield 0.4.
    const auto extended =
        ComputePbrDielectricFresnelEXT(2.0f, 0.3f, {0.25f, 1.0f, 12.0f});
    EXPECT_NEAR(extended.f0[0], 1.0f / 120.0f, 1e-7f);
    EXPECT_NEAR(extended.f0[1], 1.0f / 30.0f, 1e-7f);
    EXPECT_NEAR(extended.f0[2], 0.3f, 1e-7f);
    EXPECT_FLOAT_EQ(extended.f90, 0.3f);
}
