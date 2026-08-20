// SPDX-License-Identifier: MS-PL
//
// plans/plan_fna3d.md FNA3D-15: GTest coverage for the FNA3D renderer's shader-variant selection.
//
// FNA3D executes XNA's actual compiled stock effects, each of which carries exactly one technique
// with one pass; the shader variant is chosen by an `int ShaderIndex` parameter that the effect's
// own VSIndices/PSIndices arrays resolve. That arithmetic is therefore the load-bearing decision
// of every 3D draw this renderer makes, and it is pure integer logic with no device, window or
// display involved -- which is exactly what belongs in the unit-test corpus rather than in a
// pixel test. The rendered consequences are covered by the Fna3d_3D CTest binary.
#include <gtest/gtest.h>

// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_FNA3D is defined project-wide.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dStockEffects.hpp"

namespace
{
    using namespace CNA::Internal::Renderers::Fna3d::StockShaderIndex;

    // The reference implementations below are XNA's own, transcribed independently of the
    // production code so that a typo in one is not silently mirrored by the other.
    int ReferenceBasic(bool fog, bool vertexColor, bool texture, bool lighting, bool perPixel,
                       bool oneLight)
    {
        int index = fog ? 0 : 1;
        index += vertexColor ? 2 : 0;
        index += texture ? 4 : 0;
        if (lighting)
        {
            index += perPixel ? 24 : (oneLight ? 16 : 8);
        }
        return index;
    }

    int ReferenceSkinned(bool fog, int weights, bool perPixel, bool oneLight)
    {
        int index = fog ? 0 : 1;
        if (weights == 2) index += 2;
        else if (weights == 4) index += 4;
        index += perPixel ? 12 : (oneLight ? 6 : 0);
        return index;
    }
}

TEST(Fna3dStockShaderIndexTests, BasicMatchesXnaForEveryCombination)
{
    for (int bits = 0; bits < 64; ++bits)
    {
        const bool fog = (bits & 1) != 0;
        const bool vertexColor = (bits & 2) != 0;
        const bool texture = (bits & 4) != 0;
        const bool lighting = (bits & 8) != 0;
        const bool perPixel = (bits & 16) != 0;
        const bool oneLight = (bits & 32) != 0;

        EXPECT_EQ(Basic(fog, vertexColor, texture, lighting, perPixel, oneLight),
                  ReferenceBasic(fog, vertexColor, texture, lighting, perPixel, oneLight))
            << "bits=" << bits;
    }
}

TEST(Fna3dStockShaderIndexTests, BasicStaysWithinTheCompiledVariantRange)
{
    // BasicEffect.fxb carries 32 variants; an index outside 0..31 would index past its
    // VSIndices/PSIndices arrays.
    for (int bits = 0; bits < 64; ++bits)
    {
        const int index = Basic((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0,
                                (bits & 8) != 0, (bits & 16) != 0, (bits & 32) != 0);
        EXPECT_GE(index, 0);
        EXPECT_LT(index, 32);
    }
}

TEST(Fna3dStockShaderIndexTests, BasicDefaultsToTheUnfoggedUntexturedVariant)
{
    // XNA's own defaults: fog off, vertex colour off, texture off, lighting off -> index 1.
    EXPECT_EQ(Basic(false, false, false, false, false, false), 1);
    // Fog on with everything else off is variant 0, the very first compiled pair.
    EXPECT_EQ(Basic(true, false, false, false, false, false), 0);
}

TEST(Fna3dStockShaderIndexTests, BasicLightingTiersAreDistinct)
{
    const int threeLights = Basic(true, false, false, true, false, false);
    const int oneLight = Basic(true, false, false, true, false, true);
    const int perPixel = Basic(true, false, false, true, true, false);
    EXPECT_EQ(threeLights, 8);
    EXPECT_EQ(oneLight, 16);
    EXPECT_EQ(perPixel, 24);
    // preferPerPixelLighting wins over the one-light optimization, exactly as XNA orders them.
    EXPECT_EQ(Basic(true, false, false, true, true, true), 24);
}

TEST(Fna3dStockShaderIndexTests, AlphaTestCoversItsEightVariants)
{
    EXPECT_EQ(AlphaTest(true, false, false), 0);
    EXPECT_EQ(AlphaTest(false, false, false), 1);
    EXPECT_EQ(AlphaTest(true, true, false), 2);
    EXPECT_EQ(AlphaTest(true, false, true), 4);
    EXPECT_EQ(AlphaTest(false, true, true), 7);
    for (int bits = 0; bits < 8; ++bits)
    {
        const int index = AlphaTest((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0);
        EXPECT_GE(index, 0);
        EXPECT_LT(index, 8);
    }
}

TEST(Fna3dStockShaderIndexTests, DualTextureCoversItsFourVariants)
{
    EXPECT_EQ(DualTexture(true, false), 0);
    EXPECT_EQ(DualTexture(false, false), 1);
    EXPECT_EQ(DualTexture(true, true), 2);
    EXPECT_EQ(DualTexture(false, true), 3);
}

TEST(Fna3dStockShaderIndexTests, EnvironmentMapCoversItsSixteenVariants)
{
    EXPECT_EQ(EnvironmentMap(true, false, false, false), 0);
    EXPECT_EQ(EnvironmentMap(false, false, false, false), 1);
    EXPECT_EQ(EnvironmentMap(true, true, false, false), 2);
    EXPECT_EQ(EnvironmentMap(true, false, true, false), 4);
    EXPECT_EQ(EnvironmentMap(true, false, false, true), 8);
    EXPECT_EQ(EnvironmentMap(false, true, true, true), 15);
}

TEST(Fna3dStockShaderIndexTests, SkinnedMatchesXnaForEveryCombination)
{
    for (const int weights : { 1, 2, 4 })
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const bool fog = (bits & 1) != 0;
            const bool perPixel = (bits & 2) != 0;
            const bool oneLight = (bits & 4) != 0;
            EXPECT_EQ(Skinned(fog, weights, perPixel, oneLight),
                      ReferenceSkinned(fog, weights, perPixel, oneLight))
                << "weights=" << weights << " bits=" << bits;
        }
    }
}

TEST(Fna3dStockShaderIndexTests, SkinnedStaysWithinTheCompiledVariantRange)
{
    // SkinnedEffect.fxb carries 18 variants (3 weight tiers x 2 fog x 3 lighting tiers).
    for (const int weights : { 1, 2, 4 })
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const int index =
                Skinned((bits & 1) != 0, weights, (bits & 2) != 0, (bits & 4) != 0);
            EXPECT_GE(index, 0);
            EXPECT_LT(index, 18);
        }
    }
}

TEST(Fna3dStockShaderIndexTests, SkinnedTreatsAnUnknownWeightCountAsOne)
{
    // XNA only ever sets 1, 2 or 4 weights per vertex; anything else must fall into the
    // one-weight tier rather than silently shifting the whole index.
    EXPECT_EQ(Skinned(true, 1, false, false), Skinned(true, 3, false, false));
    EXPECT_EQ(Skinned(true, 1, false, false), Skinned(true, 0, false, false));
}

#endif // CNA_RENDERER_FNA3D / CNA_RENDERER_PRESENT_FNA3D
