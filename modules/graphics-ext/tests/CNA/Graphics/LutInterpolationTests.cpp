// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2131: which interpolation a lookup table is read with, decided by measurement.
//
// Both agree exactly on every entry the table holds, so nothing about a grid point separates them.
// The difference is entirely between the entries, which is where almost every pixel lands: a
// 32-entry table has 32 values to describe 256. The test builds a table from a grade whose exact
// answer is known, runs both interpolations on the GPU, and compares each against that exact answer
// -- with the neutral axis measured separately, because a grey that comes back tinted is the one
// error that reads as a creative decision rather than as a bug.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/CubeLut.hpp"
#include "CNA/Graphics/LutInterpolation.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::ColorGradePass;
using CNA::Graphics::CubeLut;
using CNA::Graphics::LutInterpolation;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;

constexpr int kTableSize = 8;
constexpr int kWidth     = 256;
constexpr int kHeight    = 4;

/// The grade the table is built from: a saturation boost that gets stronger with brightness.
///
/// Chosen for three properties, each of which the measurement needs. It is **non-separable**, so
/// each output channel depends on all three inputs and a per-channel curve cannot stand in for it.
/// It is **nonlinear**, so interpolation between entries is an approximation rather than exact --
/// a linear grade is reproduced perfectly by both and would separate nothing. And it maps every
/// **neutral to a neutral**, which gives the neutral-axis measurement an exact answer to compare
/// against instead of a tolerance.
Vector3 Grade(const Vector3& colour)
{
    const float luminance = 0.2126f * colour.X + 0.7152f * colour.Y + 0.0722f * colour.Z;
    const float saturation = 1.0f + 3.0f * luminance;
    const auto channel = [&](const float value) {
        return std::clamp(luminance + (value - luminance) * saturation, 0.0f, 1.0f);
    };
    return Vector3(channel(colour.X), channel(colour.Y), channel(colour.Z));
}

std::string GradeAsCubeText(const int size)
{
    std::ostringstream out;
    out << "TITLE \"luminance saturation\"\nLUT_3D_SIZE " << size << "\n";
    const float last = static_cast<float>(size - 1);
    for (int blue = 0; blue < size; ++blue)
        for (int green = 0; green < size; ++green)
            for (int red = 0; red < size; ++red)
            {
                const Vector3 value = Grade(Vector3(static_cast<float>(red) / last,
                                                    static_cast<float>(green) / last,
                                                    static_cast<float>(blue) / last));
                out << value.X << " " << value.Y << " " << value.Z << "\n";
            }
    return out.str();
}

/// A grey ramp: column x is the neutral colour (x, x, x), repeated down every row.
std::unique_ptr<Texture2D> MakeGreyRamp(GraphicsDevice& gd)
{
    auto texture = std::make_unique<Texture2D>(gd, kWidth, kHeight);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) texels.emplace_back(x, x, x, 255);
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

/// A deterministic spread of coloured inputs, one per column, for the accuracy measurement.
std::unique_ptr<Texture2D> MakeColourSpread(GraphicsDevice& gd, std::vector<Vector3>& inputs)
{
    auto texture = std::make_unique<Texture2D>(gd, kWidth, kHeight);
    inputs.clear();
    inputs.reserve(kWidth);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kWidth) * kHeight);
    for (int x = 0; x < kWidth; ++x)
    {
        // Three coprime strides, so the walk visits the cube rather than a diagonal of it.
        const int red   = (x * 37) % 256;
        const int green = (x * 61) % 256;
        const int blue  = (x * 97) % 256;
        inputs.emplace_back(static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f,
                            static_cast<float>(blue) / 255.0f);
        (void)red;
    }
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
            texels.emplace_back(static_cast<int>(inputs[x].X * 255.0f + 0.5f),
                                static_cast<int>(inputs[x].Y * 255.0f + 0.5f),
                                static_cast<int>(inputs[x].Z * 255.0f + 0.5f), 255);
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::vector<Color> GradeRow(ColorGradePass& pass, Texture2D& source, RenderTarget2D& destination)
{
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kWidth;
    context.height      = kHeight;
    pass.apply(context);

    std::vector<Color> pixels(static_cast<std::size_t>(kWidth) * kHeight, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return std::vector<Color>(pixels.begin() + kWidth, pixels.begin() + 2 * kWidth);
}

int WorstNeutralSpread(const std::vector<Color>& row)
{
    int worst = 0;
    for (const Color& pixel : row)
    {
        const int high = std::max({pixel.getRProperty(), pixel.getGProperty(),
                                   pixel.getBProperty()});
        const int low  = std::min({pixel.getRProperty(), pixel.getGProperty(),
                                   pixel.getBProperty()});
        worst = std::max(worst, high - low);
    }
    return worst;
}

struct Error { double mean; int worst; };

Error ErrorAgainstTheGrade(const std::vector<Color>& row, const std::vector<Vector3>& inputs)
{
    long total = 0;
    int worst = 0;
    for (std::size_t x = 0; x < row.size(); ++x)
    {
        const Vector3 exact = Grade(inputs[x]);
        const int dr = std::abs(row[x].getRProperty()
                                - static_cast<int>(exact.X * 255.0f + 0.5f));
        const int dg = std::abs(row[x].getGProperty()
                                - static_cast<int>(exact.Y * 255.0f + 0.5f));
        const int db = std::abs(row[x].getBProperty()
                                - static_cast<int>(exact.Z * 255.0f + 0.5f));
        const int channel = std::max({dr, dg, db});
        total += channel;
        worst = std::max(worst, channel);
    }
    return Error{static_cast<double>(total) / static_cast<double>(row.size()), worst};
}

// ── Settings ────────────────────────────────────────────────────────────────

TEST(LutInterpolationTest, TrilinearIsTheDefaultAndTheSettingRoundTrips)
{
    // The default is what every frame graded before this existed, so it stays where it was; the
    // measurement below is what makes changing it an informed decision rather than a preference.
    GraphicsDevice gd;
    ColorGradePass pass(gd);
    EXPECT_EQ(pass.getInterpolation(), LutInterpolation::Trilinear);
    pass.setInterpolation(LutInterpolation::Tetrahedral);
    EXPECT_EQ(pass.getInterpolation(), LutInterpolation::Tetrahedral);
}

TEST(LutInterpolationTest, AVolumeThatIsNotACubeIsRefused)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        GTEST_SKIP() << "this renderer has no volume textures";
    Texture3D slab(gd, 8, 8, 4, false, Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
    ColorGradePass pass(gd);
    EXPECT_THROW(pass.setVolumeLut(&slab), std::invalid_argument);
}

// ── The measurement ─────────────────────────────────────────────────────────

TEST(LutInterpolationTest, TetrahedralKeepsANeutralNeutralAndTrilinearDoesNot)
{
    // The decisive one. A neutral colour lies on the edge from a cell's black corner to its white
    // corner, so tetrahedral computes it from two neutral entries and it stays neutral exactly.
    // Trilinear mixes in the six coloured corners around it, so the grey comes back tinted -- and
    // because the tint varies smoothly with brightness, it reads as a grading decision.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    const CubeLut lut = CubeLut::parse(GradeAsCubeText(kTableSize));
    auto strip = lut.createStripTexture(gd);
    auto ramp = MakeGreyRamp(gd);
    RenderTarget2D destination(gd, kWidth, kHeight);

    ColorGradePass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the grade";
    pass.setLut(strip.get());

    pass.setInterpolation(LutInterpolation::Trilinear);
    const int trilinear = WorstNeutralSpread(GradeRow(pass, *ramp, destination));

    pass.setInterpolation(LutInterpolation::Tetrahedral);
    const int tetrahedral = WorstNeutralSpread(GradeRow(pass, *ramp, destination));

    std::printf("    worst neutral spread over 256 greys: trilinear %d, tetrahedral %d (of 255)\n",
                trilinear, tetrahedral);

    // Anti-vacuity: a table that tinted nothing would let both pass.
    ASSERT_GT(trilinear, 4) << "the table used here does not separate the two interpolations, so "
                               "this test would pass against anything";
    EXPECT_LE(tetrahedral, 1)
        << "tetrahedral tinted a neutral by " << tetrahedral << "/255";
    EXPECT_GT(trilinear, tetrahedral * 4);
}

TEST(LutInterpolationTest, TetrahedralIsTheMoreAccurateOfTheTwoAgainstTheExactGrade)
{
    // The general claim, which is weaker than the neutral one and worth stating separately: over
    // coloured inputs neither is exact, and the interesting question is which is closer to the
    // grade the table was built from.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    const CubeLut lut = CubeLut::parse(GradeAsCubeText(kTableSize));
    auto strip = lut.createStripTexture(gd);
    std::vector<Vector3> inputs;
    auto spread = MakeColourSpread(gd, inputs);
    RenderTarget2D destination(gd, kWidth, kHeight);

    ColorGradePass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the grade";
    pass.setLut(strip.get());

    pass.setInterpolation(LutInterpolation::Trilinear);
    const Error trilinear = ErrorAgainstTheGrade(GradeRow(pass, *spread, destination), inputs);

    pass.setInterpolation(LutInterpolation::Tetrahedral);
    const Error tetrahedral = ErrorAgainstTheGrade(GradeRow(pass, *spread, destination), inputs);

    std::printf("    against the exact grade, %d-entry table: trilinear mean %.2f worst %d, "
                "tetrahedral mean %.2f worst %d (of 255)\n",
                kTableSize, trilinear.mean, trilinear.worst, tetrahedral.mean, tetrahedral.worst);

    ASSERT_GT(trilinear.mean, 1.0)
        << "neither interpolation had anything to approximate, so this compares nothing";
    EXPECT_LT(tetrahedral.mean, trilinear.mean);
}

TEST(LutInterpolationTest, BothAgreeExactlyOnTheTablesOwnEntries)
{
    // The other half of the story, and the reason a difference between them is easy to dismiss: at
    // every value the table actually holds there is nothing to interpolate and the two are the same
    // number. A test that only sampled grid points would find them identical.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    const CubeLut lut = CubeLut::parse(GradeAsCubeText(kTableSize));
    auto strip = lut.createStripTexture(gd);
    RenderTarget2D destination(gd, kWidth, kHeight);

    // A source whose first kTableSize columns are exactly the table's own red/green/blue steps.
    auto source = std::make_unique<Texture2D>(gd, kWidth, kHeight);
    std::vector<Color> texels(static_cast<std::size_t>(kWidth) * kHeight, Color::Black);
    for (int y = 0; y < kHeight; ++y)
        for (int index = 0; index < kTableSize; ++index)
        {
            const int step = static_cast<int>(static_cast<float>(index)
                                              / static_cast<float>(kTableSize - 1) * 255.0f + 0.5f);
            texels[static_cast<std::size_t>(y) * kWidth + index] = Color(step, step, step, 255);
        }
    source->SetData(texels.data(), static_cast<int>(texels.size()));

    ColorGradePass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the grade";
    pass.setLut(strip.get());

    pass.setInterpolation(LutInterpolation::Trilinear);
    const std::vector<Color> trilinear = GradeRow(pass, *source, destination);
    pass.setInterpolation(LutInterpolation::Tetrahedral);
    const std::vector<Color> tetrahedral = GradeRow(pass, *source, destination);

    for (int index = 0; index < kTableSize; ++index)
        EXPECT_LE(std::abs(trilinear[index].getRProperty() - tetrahedral[index].getRProperty()), 1)
            << "at the table's own entry " << index;
}

TEST(LutInterpolationTest, TheVolumeLayoutGivesTheSameAnswerAsTheStrip)
{
    // Two layouts of one table, and the addressing arithmetic is different in each. They must
    // agree, or one of them is reading somebody else's entry.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        GTEST_SKIP() << "this renderer has no volume textures";

    const CubeLut lut = CubeLut::parse(GradeAsCubeText(kTableSize));
    auto strip  = lut.createStripTexture(gd);
    auto volume = lut.createVolumeTexture(gd);
    std::vector<Vector3> inputs;
    auto spread = MakeColourSpread(gd, inputs);
    RenderTarget2D destination(gd, kWidth, kHeight);

    ColorGradePass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the grade";
    pass.setInterpolation(LutInterpolation::Tetrahedral);

    pass.setLut(strip.get());
    const std::vector<Color> fromStrip = GradeRow(pass, *spread, destination);

    pass.setVolumeLut(volume.get());
    const std::vector<Color> fromVolume = GradeRow(pass, *spread, destination);

    // Anti-vacuity, and it is the whole risk of this test: if either shader failed to compile the
    // pass copies its input through, and two copy-throughs agree perfectly. So first establish that
    // a grade actually happened.
    int graded = 0;
    for (std::size_t x = 0; x < fromStrip.size(); ++x)
        if (std::abs(fromStrip[x].getRProperty()
                     - static_cast<int>(inputs[x].X * 255.0f + 0.5f)) > 8) ++graded;
    ASSERT_GT(graded, static_cast<int>(fromStrip.size()) / 4)
        << "the strip path returned its input, so this comparison is between two copies";

    int worst = 0;
    for (std::size_t x = 0; x < fromStrip.size(); ++x)
        worst = std::max({worst,
                          std::abs(fromStrip[x].getRProperty() - fromVolume[x].getRProperty()),
                          std::abs(fromStrip[x].getGProperty() - fromVolume[x].getGProperty()),
                          std::abs(fromStrip[x].getBProperty() - fromVolume[x].getBProperty())});
    std::printf("    strip against volume, same table: worst channel difference %d (of 255)\n",
                worst);
    EXPECT_LE(worst, 1);
}

} // namespace

#endif // CNA_CNAEXT
