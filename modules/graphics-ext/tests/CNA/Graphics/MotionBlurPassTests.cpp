// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2030..MOD-2034: camera motion blur.
//
// Every test here changes exactly one thing between two frames of the same scene, because that is
// the only way to say what the blur is reacting to. A blur that reacted to nothing, or to
// everything, would still produce a blurred frame.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/MotionBlurPass.hpp"
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

using CNA::Graphics::MotionBlurPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int   kSize     = 64;
constexpr float kFarPlane = 100.0f;
constexpr float kDepth    = 20.0f;    // the whole scene sits on one plane this far out

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

/// Vertical stripes: contrast that survives a vertical smear and dies under a horizontal one, which
/// is what lets the tests say *which way* the blur ran.
std::unique_ptr<RenderTarget2D> MakeVerticalStripes(GraphicsDevice& gd)
{
    return MakeImage(gd, [](const int x, int) {
        return (x / 4) % 2 == 0 ? Color(240, 240, 240, 255) : Color(15, 15, 15, 255);
    });
}

std::unique_ptr<RenderTarget2D> MakeFlatDepth(GraphicsDevice& gd)
{
    const int value = static_cast<int>((kDepth / kFarPlane) * 255.0f + 0.5f);
    return MakeImage(gd, [value](int, int) { return Color(value, value, value, 255); });
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// Spread of red across a row band. High where the stripes survived, low where they smeared away.
double ContrastAcross(const std::vector<Color>& pixels)
{
    double sum = 0.0, sumSquares = 0.0;
    int count = 0;
    for (int y = 8; y < kSize - 8; ++y)
        for (int x = 8; x < kSize - 8; ++x)
        {
            const double v = pixels[static_cast<std::size_t>(y) * kSize + x].getRProperty();
            sum += v; sumSquares += v * v; ++count;
        }
    const double mean = sum / count;
    return std::sqrt(sumSquares / count - mean * mean);
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 1.0f, kFarPlane);
}

Matrix ViewAt(const float x)
{
    return Matrix::CreateLookAt(Vector3(x, 0.0f, 0.0f),
                                Vector3(x, 0.0f, -1.0f), Vector3::Up);
}

/// A context describing "the camera was at `previousX` last frame and is at `currentX` now".
PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination,
                               const float currentX, const float previousX)
{
    const Matrix projection = Projection();
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.projection        = projection;
    context.inverseProjection = Matrix::Invert(projection);
    context.inverseView       = Matrix::Invert(ViewAt(currentX));
    context.previousViewProjection = ViewAt(previousX) * projection;
    context.hasPreviousFrame  = true;
    context.nearPlane         = 1.0f;
    context.farPlane          = kFarPlane;
    return context;
}

TEST(MotionBlurTest, AStationaryCameraLeavesTheFrameAlone)
{
    // The anti-vacuity anchor for everything below: with the two cameras identical the velocity is
    // zero everywhere, and a pass that blurred anyway would be reacting to something other than
    // motion.
    GraphicsDevice gd;
    MotionBlurPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeFlatDepth(gd);
    auto source = MakeVerticalStripes(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setStrength(1.0f);

    PostProcessContext context = MakeContext(*source, destination, 0.0f, 0.0f);
    context.sourceDepth = depth.get();
    pass.apply(context);

    const double before = ContrastAcross(ReadTarget(*source));
    const double after  = ContrastAcross(ReadTarget(destination));
    EXPECT_GT(after, before * 0.95)
        << "a still camera smeared the frame: " << after << " against " << before;
}

TEST(MotionBlurTest, APanningCameraSmearsAlongThePan)
{
    // The claim the pass exists for. The camera slid sideways, so vertical stripes -- which are
    // pure horizontal contrast -- lose it.
    GraphicsDevice gd;
    MotionBlurPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeFlatDepth(gd);
    auto source = MakeVerticalStripes(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setStrength(1.0f);
    pass.setMaxDistance(0.25f);

    PostProcessContext context = MakeContext(*source, destination, 0.0f, -1.5f);
    context.sourceDepth = depth.get();
    pass.apply(context);

    const double before = ContrastAcross(ReadTarget(*source));
    const double after  = ContrastAcross(ReadTarget(destination));
    EXPECT_LT(after, before * 0.7)
        << "a panning camera did not smear the stripes: " << after << " against " << before;
}

TEST(MotionBlurTest, TheFirstFrameHasNoHistoryAndIsLeftAlone)
{
    // A pass that used the identity matrix as "the previous frame" would blur the opening frame of
    // every scene along an arbitrary direction, which looks like a one-frame glitch on every cut.
    GraphicsDevice gd;
    MotionBlurPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeFlatDepth(gd);
    auto source = MakeVerticalStripes(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setStrength(1.0f);
    pass.setMaxDistance(0.25f);

    PostProcessContext context = MakeContext(*source, destination, 0.0f, -1.5f);
    context.sourceDepth      = depth.get();
    context.hasPreviousFrame = false;
    pass.apply(context);

    const double before = ContrastAcross(ReadTarget(*source));
    const double after  = ContrastAcross(ReadTarget(destination));
    EXPECT_GT(after, before * 0.95) << "the first frame was blurred despite having no history";
}

TEST(MotionBlurTest, TheMaxDistanceCapsWhatOneSlowFrameCanDo)
{
    // A single long frame makes every velocity enormous. The cap is what keeps a stutter from
    // smearing the whole image, so it has to bite on a movement far larger than a normal one.
    GraphicsDevice gd;
    MotionBlurPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeFlatDepth(gd);
    auto source = MakeVerticalStripes(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setStrength(1.0f);

    const auto contrastWithCap = [&](const float cap) {
        pass.setMaxDistance(cap);
        PostProcessContext context = MakeContext(*source, destination, 0.0f, -6.0f);
        context.sourceDepth = depth.get();
        pass.apply(context);
        return ContrastAcross(ReadTarget(destination));
    };

    const double tightCap = contrastWithCap(0.005f);
    const double looseCap = contrastWithCap(0.25f);
    EXPECT_GT(tightCap, looseCap * 1.5)
        << "the cap did not limit the smear: " << tightCap << " against " << looseCap;
}

TEST(MotionBlurTest, WithoutDepthOrACameraTheFrameIsPassedThrough)
{
    GraphicsDevice gd;
    MotionBlurPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeVerticalStripes(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setStrength(1.0f);

    PostProcessContext context = MakeContext(*source, destination, 0.0f, -1.5f);
    context.sourceDepth = nullptr;
    EXPECT_NO_THROW(pass.apply(context));
    EXPECT_GT(ContrastAcross(ReadTarget(destination)), ContrastAcross(ReadTarget(*source)) * 0.95);
}

TEST(MotionBlurTest, TheSettingsAreClampedAndTheNameIsStable)
{
    GraphicsDevice gd;
    MotionBlurPass pass(gd);
    EXPECT_EQ(pass.getName(), "MotionBlur");
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.0f) << "the effect must be off by default";

    pass.setStrength(0.5f);
    pass.setMaxDistance(0.1f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.5f);
    EXPECT_FLOAT_EQ(pass.getMaxDistance(), 0.1f);

    pass.setStrength(9.0f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 1.0f);
    pass.setStrength(-1.0f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.0f);
    pass.setMaxDistance(9.0f);
    EXPECT_FLOAT_EQ(pass.getMaxDistance(), 0.25f);
    pass.setMaxDistance(-1.0f);
    EXPECT_FLOAT_EQ(pass.getMaxDistance(), 0.0f);
}

} // namespace

#endif // CNA_CNAEXT
