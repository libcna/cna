// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2061: the BRDF terms an area light needs, generated at load.
//
// The fitted linearly-transformed-cosine matrix is deliberately absent -- see the header for the
// arithmetic that makes generating it at load infeasible -- and what is here instead are the terms
// importance sampling can produce cheaply and exactly. "Exactly" is a claim, so it is checked
// against a brute-force integration of the same BRDF over the hemisphere, which shares no code with
// the thing it is checking.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/AreaLightBrdfTable.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::AreaLightBrdfTable;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr float kPi = 3.14159265359f;

/// The same specular BRDF integrated over the hemisphere on a uniform grid, with Fresnel factored
/// out. No importance sampling, no shared helpers -- the point is that it agrees by arithmetic
/// rather than by construction.
float ReferenceDirectionalAlbedo(const float roughness, const float nDotV, const int steps)
{
    const float alpha = std::clamp(roughness, 0.02f, 1.0f);
    const float cosV  = std::clamp(nDotV, 1e-3f, 1.0f);
    const float sinV  = std::sqrt(std::max(1.0f - cosV * cosV, 0.0f));

    double total = 0.0;
    for (int i = 0; i < steps; ++i)
        for (int j = 0; j < 2 * steps; ++j)
        {
            const float theta = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps) *
                                (kPi * 0.5f);
            const float phi = (static_cast<float>(j) + 0.5f) / static_cast<float>(2 * steps) *
                              2.0f * kPi;
            const float lx = std::sin(theta) * std::cos(phi);
            const float ly = std::sin(theta) * std::sin(phi);
            const float lz = std::cos(theta);

            const float hx = lx + sinV, hy = ly, hz = lz + cosV;
            const float hLength = std::sqrt(hx * hx + hy * hy + hz * hz);
            if (hLength <= 1e-8f) continue;
            const float nDotH = hz / hLength;
            const float vDotH = std::max((sinV * hx + cosV * hz) / hLength, 0.0f);

            const float a = alpha * alpha;
            const float d = nDotH * nDotH * (a * a - 1.0f) + 1.0f;
            const float distribution = a * a / (kPi * d * d);

            const float k = (alpha + 1.0f) * (alpha + 1.0f) / 8.0f;
            const float geometry = (cosV / (cosV * (1.0f - k) + k)) * (lz / (lz * (1.0f - k) + k));

            const float brdf = distribution * geometry / std::max(4.0f * cosV * lz, 1e-7f);
            total += static_cast<double>(brdf) * lz * std::sin(theta) *
                     (kPi * 0.5 / steps) * (2.0 * kPi / (2 * steps));
            (void)vDotH;
        }
    return static_cast<float>(total);
}

TEST(AreaLightBrdfTableTest, TheMagnitudeMatchesABruteForceIntegration)
{
    // Roughness 0.2 and above only, and the exclusion is the honest part: a uniform hemisphere grid
    // cannot resolve a near-mirror lobe -- at roughness 0.05 the reference itself is wrong by 20%
    // because the spike falls between its samples. Importance sampling exists precisely because of
    // that, so the smooth end is where the reference is the weaker of the two, not the table.
    for (const float roughness : {0.2f, 0.35f, 0.5f, 0.7f, 0.9f})
        for (const float nDotV : {1.0f, 0.7f, 0.3f, 0.1f})
        {
            const AreaLightBrdfTable::Terms terms =
                AreaLightBrdfTable::evaluate(roughness, nDotV, 1024);
            // 384 steps, not fewer: at roughness 0.2 and a grazing view the reference still
            // reads 0.154 at 192 steps against a converged 0.180, so a smaller grid would fail
            // this comparison for its own reasons rather than the table's.
            const float reference = ReferenceDirectionalAlbedo(roughness, nDotV, 384);
            EXPECT_NEAR(terms.Magnitude, reference, 0.02f)
                << "roughness " << roughness << ", N.V " << nDotV;
        }
}

TEST(AreaLightBrdfTableTest, TheMagnitudeNeverExceedsOne)
{
    // A surface cannot reflect more light than reaches it, and a directional albedo above 1 is the
    // shape of an energy bug -- it makes a rough material glow as the view angle changes.
    for (int r = 0; r <= 10; ++r)
        for (int v = 1; v <= 10; ++v)
        {
            const AreaLightBrdfTable::Terms terms = AreaLightBrdfTable::evaluate(
                static_cast<float>(r) / 10.0f, static_cast<float>(v) / 10.0f, 256);
            EXPECT_GE(terms.Magnitude, 0.0f);
            EXPECT_LE(terms.Magnitude, 1.0f);
            EXPECT_GE(terms.Fresnel, 0.0f);
            EXPECT_LE(terms.Fresnel, terms.Magnitude + 1e-4f)
                << "the Fresnel-weighted part cannot exceed the whole";
        }
}

TEST(AreaLightBrdfTableTest, ASmoothSurfaceReflectsTowardsTheMirrorDirection)
{
    // At roughness near zero the average reflection direction *is* the mirror direction, which is
    // the strongest statement available about the direction term -- it can be written down.
    for (const float nDotV : {0.9f, 0.7f, 0.5f})
    {
        const AreaLightBrdfTable::Terms terms = AreaLightBrdfTable::evaluate(0.05f, nDotV, 2048);
        const float sinV = std::sqrt(1.0f - nDotV * nDotV);
        EXPECT_NEAR(terms.AverageNormal, nDotV, 0.02f) << "N.V " << nDotV;
        EXPECT_NEAR(terms.AverageTangent, sinV, 0.03f) << "N.V " << nDotV;
    }
}

TEST(AreaLightBrdfTableTest, ARoughSurfaceLeansTowardsTheNormal)
{
    // The off-specular tilt: as a lobe widens it is clipped by the horizon on one side, so its
    // average leans back towards the normal. A table that reported the mirror direction at every
    // roughness would put a rough surface's highlight in the wrong place.
    const AreaLightBrdfTable::Terms smooth = AreaLightBrdfTable::evaluate(0.1f, 0.3f, 2048);
    const AreaLightBrdfTable::Terms rough  = AreaLightBrdfTable::evaluate(0.9f, 0.3f, 2048);
    EXPECT_GT(rough.AverageNormal, smooth.AverageNormal);
    EXPECT_LT(rough.AverageTangent, smooth.AverageTangent);
    EXPECT_GT(rough.AverageNormal, 0.9f) << "a very rough lobe should sit almost on the normal";
}

TEST(AreaLightBrdfTableTest, AtNormalIncidenceTheAverageIsTheNormal)
{
    for (const float roughness : {0.1f, 0.5f, 0.9f})
    {
        const AreaLightBrdfTable::Terms terms = AreaLightBrdfTable::evaluate(roughness, 1.0f, 512);
        EXPECT_NEAR(terms.AverageNormal, 1.0f, 1e-3f);
        EXPECT_NEAR(terms.AverageTangent, 0.0f, 1e-2f);
    }
}

TEST(AreaLightBrdfTableTest, ANonPositiveSampleCountIsRefused)
{
    EXPECT_THROW((void)AreaLightBrdfTable::evaluate(0.5f, 0.5f, 0), std::invalid_argument);
    EXPECT_THROW((void)AreaLightBrdfTable::evaluate(0.5f, 0.5f, -4), std::invalid_argument);
}

TEST(AreaLightBrdfTableTest, TheTextureHoldsWhatTheRoutineComputes)
{
    GraphicsDevice gd;
    const int size = 16;
    AreaLightBrdfTable table(gd, size, 128);

    ASSERT_NE(table.getTexture(), nullptr);
    EXPECT_EQ(table.getSize(), size);
    EXPECT_EQ(table.getSampleCount(), 128);
    EXPECT_EQ(table.getTexture()->getWidthProperty(), size);
    EXPECT_EQ(table.getTexture()->getHeightProperty(), size);
    EXPECT_GE(table.getGenerationMilliseconds(), 0.0);

    std::vector<Color> texels(static_cast<std::size_t>(size) * size, Color::Black);
    table.getTexture()->GetData(texels.data(), static_cast<int>(texels.size()));

    // The 8-bit encoding is the tolerance: one step is 1/255, and the comparison allows two.
    for (int y = 0; y < size; y += 5)
        for (int x = 0; x < size; x += 5)
        {
            const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float nDotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const AreaLightBrdfTable::Terms terms =
                AreaLightBrdfTable::evaluate(roughness, nDotV, 128);
            const Color texel = texels[static_cast<std::size_t>(y) * size + x];
            EXPECT_NEAR(static_cast<float>(texel.getRProperty()) / 255.0f, terms.Magnitude,
                        2.0f / 255.0f) << x << "," << y;
            EXPECT_NEAR(static_cast<float>(texel.getBProperty()) / 255.0f, terms.AverageTangent,
                        2.0f / 255.0f) << x << "," << y;
            EXPECT_NEAR(static_cast<float>(texel.getAProperty()) / 255.0f, terms.AverageNormal,
                        2.0f / 255.0f) << x << "," << y;
        }
}

TEST(AreaLightBrdfTableTest, ANonPositiveTableSizeIsRefused)
{
    GraphicsDevice gd;
    EXPECT_THROW(AreaLightBrdfTable(gd, 0, 32), std::invalid_argument);
    EXPECT_THROW(AreaLightBrdfTable(gd, 16, 0), std::invalid_argument);
}

TEST(AreaLightBrdfTableTest, GeneratingTheDefaultTableIsCheapEnoughToDoAtLoad)
{
    // The whole reason the fitted matrix is absent is that generating it at load would cost
    // seconds. A replacement that also cost seconds would not be a replacement, so the cost is a
    // number rather than an assurance. The bound is generous -- this runs on a shared machine --
    // and it is still three orders of magnitude below what a Nelder-Mead fit would take.
    GraphicsDevice gd;
    AreaLightBrdfTable table(gd);
    EXPECT_EQ(table.getSize(), AreaLightBrdfTable::kDefaultSize);
    EXPECT_LT(table.getGenerationMilliseconds(), 500.0)
        << "the default table took " << table.getGenerationMilliseconds() << " ms to generate";
}

} // namespace

#endif // CNA_CNAEXT
