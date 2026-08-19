// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2050: exponential height fog, integrated along the view ray.
//
// The integral is the whole reason this is not a distance fade with a height term attached, so the
// integral is what the tests check first -- against the formula computed here, not against numbers
// the implementation happened to produce.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/HeightFogPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::HeightFogPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int   kSize     = 32;
constexpr float kFarPlane = 100.0f;

std::unique_ptr<RenderTarget2D> MakeImage(GraphicsDevice& gd,
                                          const std::function<Color(int, int)>& colourAt)
{
    auto staging = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            texels.push_back(colourAt(x, y));
    staging->SetData(texels.data(), static_cast<int>(texels.size()));
    auto target = std::make_unique<RenderTarget2D>(gd, kSize, kSize);
    CNA::Graphics::FullscreenPass blit(gd);
    blit.draw(staging.get(), target.get(), nullptr, kSize, kSize);
    return target;
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// Near on the top half, far on the bottom half, so one frame carries two distances.
std::unique_ptr<RenderTarget2D> MakeSplitDepth(GraphicsDevice& gd, const float nearWorld,
                                               const float farWorld)
{
    return MakeImage(gd, [nearWorld, farWorld](int, const int y) {
        const float world = y < kSize / 2 ? nearWorld : farWorld;
        const int value = static_cast<int>((world / kFarPlane) * 255.0f + 0.5f);
        return Color(value, value, value, 255);
    });
}

PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination)
{
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 1.0f, kFarPlane);
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 2.0f, 0.0f),
                                             Vector3(0.0f, 2.0f, -1.0f), Vector3::Up);
    PostProcessContext context;
    context.source            = &source;
    context.destination       = &destination;
    context.width             = kSize;
    context.height            = kSize;
    context.projection        = projection;
    context.inverseProjection = Matrix::Invert(projection);
    context.inverseView       = Matrix::Invert(view);
    context.nearPlane         = 1.0f;
    context.farPlane          = kFarPlane;
    return context;
}

std::size_t Centre(const int row) { return static_cast<std::size_t>(row) * kSize + kSize / 2; }

// ── The integral (MOD-2050) ──────────────────────────────────────────────────

TEST(HeightFogTest, TheOpticalDepthMatchesTheAnalyticIntegral)
{
    // Computed here from the closed form rather than compared against recorded numbers, so the test
    // states the physics instead of pinning the implementation.
    const float density = 0.05f, falloff = 0.2f, base = 0.0f;
    for (const float height : {0.0f, 3.0f, 10.0f})
        for (const float step : {-0.6f, -0.2f, 0.3f, 0.8f})
            for (const float distance : {1.0f, 20.0f, 90.0f})
            {
                const float atCamera = density * std::exp(-falloff * (height - base));
                const float climb    = falloff * step;
                const float expected = atCamera * (1.0f - std::exp(-climb * distance)) / climb;
                EXPECT_NEAR(HeightFogPass::opticalDepth(height, step, distance, density, falloff, base),
                            expected, std::fabs(expected) * 1e-4f + 1e-6f)
                    << "at height " << height << " step " << step << " distance " << distance;
            }
}

TEST(HeightFogTest, ALevelLookIsDensityTimesDistanceRatherThanADivisionByZero)
{
    // The general form divides by the ray's climb. A level look is not that form with a small
    // number in it: nudging the climb away from zero would make a level view's fog depend on the
    // size of the nudge, which is a constant nobody chose.
    const float density = 0.05f, falloff = 0.2f, height = 4.0f;
    const float atCamera = density * std::exp(-falloff * height);
    EXPECT_NEAR(HeightFogPass::opticalDepth(height, 0.0f, 40.0f, density, falloff, 0.0f),
                atCamera * 40.0f, 1e-4f);
}

TEST(HeightFogTest, AValleyFillsWhileTheHilltopStaysClear)
{
    // The claim that separates height fog from distance fog: at the same distance, a low ray passes
    // through more fog than a high one.
    const float density = 0.05f, falloff = 0.2f;
    const float low  = HeightFogPass::opticalDepth(1.0f, 0.0f, 50.0f, density, falloff, 0.0f);
    const float high = HeightFogPass::opticalDepth(20.0f, 0.0f, 50.0f, density, falloff, 0.0f);
    EXPECT_GT(low, high * 4.0f) << "height made almost no difference: " << low << " against " << high;
}

TEST(HeightFogTest, DegenerateConfigurationsAnswerZero)
{
    // Each of these either has no fog to integrate or divides by zero. Answering 0 keeps a NaN out
    // of the mix factor, where it would turn the whole frame into fog colour rather than fail.
    EXPECT_FLOAT_EQ(HeightFogPass::opticalDepth(2.0f, 0.5f, 10.0f, 0.0f, 0.2f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(HeightFogPass::opticalDepth(2.0f, 0.5f, 10.0f, -1.0f, 0.2f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(HeightFogPass::opticalDepth(2.0f, 0.5f, 0.0f, 0.05f, 0.2f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(HeightFogPass::opticalDepth(2.0f, 0.5f, 10.0f, 0.05f, 0.0f, 0.0f), 0.0f);
}

// ── The frame ────────────────────────────────────────────────────────────────

TEST(HeightFogTest, TheFurtherHalfOfTheFrameIsFoggedMore)
{
    GraphicsDevice gd;
    HeightFogPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd, 5.0f, 80.0f);
    auto source = MakeImage(gd, [](int, int) { return Color(20, 20, 20, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setColor(Vector3(1.0f, 1.0f, 1.0f));   // white fog over a dark scene: fog reads as light
    pass.setDensity(0.08f);
    pass.setFalloff(0.05f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    const int nearHalf = pixels[Centre(kSize / 4)].getRProperty();
    const int farHalf  = pixels[Centre(3 * kSize / 4)].getRProperty();
    EXPECT_GT(farHalf, nearHalf + 20)
        << "distance did not thicken the fog: near " << nearHalf << ", far " << farHalf;
    EXPECT_GT(farHalf, 20) << "the far half was not fogged at all";
}

TEST(HeightFogTest, ZeroDensityLeavesTheFrameAlone)
{
    GraphicsDevice gd;
    HeightFogPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd, 5.0f, 80.0f);
    auto source = MakeImage(gd, [](int, int) { return Color(20, 20, 20, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    for (const Color& pixel : ReadTarget(destination))
        ASSERT_NEAR(pixel.getRProperty(), 20, 3) << "a disabled pass still fogged the frame";
}

TEST(HeightFogTest, WithoutDepthOrACameraTheFrameIsPassedThrough)
{
    GraphicsDevice gd;
    HeightFogPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(120, 60, 30, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setDensity(0.5f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = nullptr;
    EXPECT_NO_THROW(pass.apply(context));
    EXPECT_NEAR(ReadTarget(destination)[Centre(kSize / 2)].getRProperty(), 120, 4);
}

TEST(HeightFogTest, TheSettingsRoundTripAndNonsenseIsIgnored)
{
    GraphicsDevice gd;
    HeightFogPass pass(gd);
    EXPECT_EQ(pass.getName(), "HeightFog");
    EXPECT_FLOAT_EQ(pass.getDensity(), 0.0f) << "the effect must be off by default";

    pass.setDensity(0.2f);
    pass.setFalloff(0.4f);
    pass.setBaseHeight(-3.0f);
    pass.setColor(Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_FLOAT_EQ(pass.getDensity(), 0.2f);
    EXPECT_FLOAT_EQ(pass.getFalloff(), 0.4f);
    EXPECT_FLOAT_EQ(pass.getBaseHeight(), -3.0f) << "a base height below zero is a valley, not an error";
    EXPECT_FLOAT_EQ(pass.getColor().Y, 0.2f);

    pass.setDensity(-1.0f);
    pass.setFalloff(0.0f);
    pass.setFalloff(-1.0f);
    EXPECT_FLOAT_EQ(pass.getDensity(), 0.2f);
    EXPECT_FLOAT_EQ(pass.getFalloff(), 0.4f);
}

} // namespace

#endif // CNA_CNAEXT
