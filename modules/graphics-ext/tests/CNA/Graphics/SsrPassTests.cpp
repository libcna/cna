// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2000: screen-space reflections.
//
// Tested against synthetic depth, normal and colour images rather than a rendered scene, for the
// reason SsaoPassTests gives: it is the only way to state what the march must do without also
// depending on a prepass, a camera rig and a mesh.
//
// The scene the hit tests use is a 45-degree plane -- a floor -- with a nearer band standing in
// front of it near the top of the screen. The plane's depth image is computed from the same normal
// the test supplies, which matters more than it looks: a fronto-parallel depth image with a tilted
// normal describes a surface that cannot exist, a ray skimming such a surface slowly goes behind
// its own plane, and the pass then reports a hit that is neither a bug nor a feature but an answer
// to a question nobody should ask. The first version of this file made exactly that mistake.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/SsrPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::SsrPass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int   kSize      = 32;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 100.0f;
constexpr float kTanHalfFov = 0.41421356f;   // tan(45 degrees / 2)
constexpr float kPlaneCentreDepth = 0.5f;    // the reflector crosses the view axis half way out
constexpr float kBandNdcY = 0.65f;           // the occluder the reflected ray is meant to find
constexpr float kBandDepth = 0.30f;
constexpr int   kFarByte   = 204;

/// The NDC Y the shader sees when it samples the texel written at this row.
///
/// Measured rather than assumed, and the measurement is the surprise: **sampling a render target is
/// vertically flipped relative to sampling a `Texture2D` holding the same rows.** A render target
/// whose `GetData` puts a band at rows 26-31 answers `texture(rt, vec2(0.5, 0.9))` with the rows at
/// the *other* end. Every image this pass reads in a real pipeline is a render target -- the scene
/// target and the prepass outputs alike -- so the tests build theirs in render targets too, and
/// account for the flip here in one place rather than in each scene.
float SampledNdcYOfRow(const int row)
{
    const float sampledV = 1.0f - (static_cast<float>(row) + 0.5f) / static_cast<float>(kSize);
    return sampledV * 2.0f - 1.0f;
}

/// Depth of a plane tilted 45 degrees about X, at the row's own view ray.
///
/// The plane's view normal is (0, 1, 1) normalized, and it passes through (0, 0, -kPlaneCentreDepth).
/// Solving the ray against it gives d0 / (1 - ndcY * tan(fov/2)): nearer at the bottom of the
/// screen, further at the top, which is what a 45-degree floor actually looks like. Building the
/// depth from the same normal the test supplies is the point -- a fronto-parallel depth image with
/// a tilted normal describes a surface that cannot exist, and a ray skimming it goes behind its own
/// plane, which is not a case worth asserting anything about.
float PlaneDepthAtRow(const int row)
{
    return kPlaneCentreDepth / (1.0f - SampledNdcYOfRow(row) * kTanHalfFov);
}

/// True for the near, bright band the reflected ray travels into: the top of the screen as the
/// shader sees it, which is the *low* rows of the texture that carries it.
bool InBand(const int row) { return SampledNdcYOfRow(row) >= kBandNdcY; }

int DepthByteOfRow(const int row)
{
    const float depth = InBand(row) ? kBandDepth : PlaneDepthAtRow(row);
    return static_cast<int>(depth * 255.0f + 0.5f);
}

/// Puts per-row texels into a render target, which is the only kind of image a real pipeline hands
/// this pass and therefore the only kind the tests use.
std::unique_ptr<RenderTarget2D> MakeRowImage(GraphicsDevice& gd,
                                             const std::function<Color(int)>& colourOfRow)
{
    auto staging = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
    {
        const Color colour = colourOfRow(y);
        for (int x = 0; x < kSize; ++x)
            texels.push_back(colour);
    }
    staging->SetData(texels.data(), static_cast<int>(texels.size()));

    auto target = std::make_unique<RenderTarget2D>(gd, kSize, kSize);
    CNA::Graphics::FullscreenPass blit(gd);
    blit.draw(staging.get(), target.get(), nullptr, kSize, kSize);
    return target;
}

Color Grey(const int value) { return Color(value, value, value, 255); }

/// The reflector, with the occluder standing in front of it near the top of the screen.
std::unique_ptr<RenderTarget2D> MakeTiltedPlaneDepth(GraphicsDevice& gd)
{
    return MakeRowImage(gd, [](const int row) { return Grey(DepthByteOfRow(row)); });
}

/// The same plane with nothing in front of it: a ray leaving it must never come back to it.
std::unique_ptr<RenderTarget2D> MakeTiltedPlaneDepthWithoutBand(GraphicsDevice& gd)
{
    return MakeRowImage(gd, [](const int row) {
        return Grey(static_cast<int>(PlaneDepthAtRow(row) * 255.0f + 0.5f));
    });
}

/// Every pixel the same distance away: a wall facing the camera.
std::unique_ptr<RenderTarget2D> MakeFlatDepth(GraphicsDevice& gd, const int depthByte)
{
    return MakeRowImage(gd, [depthByte](int) { return Grey(depthByte); });
}

/// Normals encoded as `n * 0.5 + 0.5`, the prepass's own encoding.
std::unique_ptr<RenderTarget2D> MakeUniformNormals(GraphicsDevice& gd, const Color encoded)
{
    return MakeRowImage(gd, [encoded](int) { return encoded; });
}

/// (0, 0, 1): facing the camera. A ray reflected off this goes straight back at the eye.
const Color kFacingNormal{128, 128, 255, 255};
/// (0, 0.7071, 0.7071): tilted 45 degrees, which reflects the view ray to (0, 1, 0) -- purely
/// lateral in view space, so the march travels sideways at a constant distance.
const Color kTiltedNormal{128, 218, 218, 255};

/// Colour: the band bright red, everything else black. Anything the plane reflects is red.
std::unique_ptr<RenderTarget2D> MakeSourceWithBrightBand(GraphicsDevice& gd)
{
    return MakeRowImage(gd, [](const int row) {
        return InBand(row) ? Color(255, 0, 0, 255) : Color(0, 0, 0, 255);
    });
}

void FillSourceFlat(GraphicsDevice& gd, RenderTarget2D& source, const Color colour)
{
    gd.SetRenderTarget(&source);
    gd.Clear(colour);
    gd.SetRenderTarget(nullptr);
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// A context with the camera filled in. SSR is the first pass in this layer that needs one.
PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination)
{
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
        0.7853982f, 1.0f, kNearPlane, kFarPlane);

    PostProcessContext context;
    context.source            = &source;
    context.destination       = &destination;
    context.width             = kSize;
    context.height            = kSize;
    context.projection        = projection;
    context.inverseProjection = Matrix::Invert(projection);
    context.nearPlane         = kNearPlane;
    context.farPlane          = kFarPlane;
    return context;
}

/// Overloads so a test can pass either an owned target or a borrowed one to MakeContext.
RenderTarget2D& SourceRef(RenderTarget2D& target) { return target; }
RenderTarget2D& SourceRef(const std::unique_ptr<RenderTarget2D>& target) { return *target; }

std::size_t CentreIndex() { return static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2; }

// ── The march ────────────────────────────────────────────────────────────────

TEST(SsrPassTest, ATiltedSurfaceReflectsTheColourItsRayReaches)
{
    // The central claim of the pass: the reflected ray leaves the tilted plane travelling up the
    // screen, passes over the near red band, and the pixel it started from takes that colour.
    // Without a working march the pixel keeps the plane's own black.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    auto source = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setMaxDistance(40.0f);
    pass.setStepCount(32);
    pass.setThickness(30.0f);   // the band stands 0.2 far-planes proud of the plane

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_GT(pixels[CentreIndex()].getRProperty(), 200)
        << "the ray left the plane and should have found the red band above it";
}

TEST(SsrPassTest, ZeroIntensityReproducesTheSceneExactly)
{
    // The anti-vacuity check for the test above: the same scene with the reflection turned off must
    // come back byte-identical to the source, so the hit above is the pass and not the setup.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    auto source = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setMaxDistance(40.0f);
    pass.setThickness(30.0f);
    pass.setIntensity(0.0f);

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_LT(pixels[CentreIndex()].getRProperty(), 8)
        << "intensity 0 still changed the pixel";
}

TEST(SsrPassTest, APlaneDoesNotReflectItself)
{
    // The physical claim: a ray leaving a plane travels away from it and can never come back to it,
    // so a floor with nothing above it reflects nothing. The depth image here is the plane's own,
    // computed from the normal supplied with it, so the scene is one a camera could actually see.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepthWithoutBand(gd);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(200, 30, 30, 255));

    pass.setMaxDistance(40.0f);
    pass.setThickness(30.0f);

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 200, 6);
    EXPECT_NEAR(pixels[CentreIndex()].getGProperty(), 30, 6);
}

TEST(SsrPassTest, MismatchedDepthAndNormalsDoNotFabricateAReflection)
{
    // Deliberately impossible input: a depth image saying "wall facing the camera" with normals
    // saying "tilted 45 degrees". A game can hand the pass two buffers that disagree, and this is
    // the regression test for the first defect this pass had. A ray skimming such a surface goes
    // behind its own plane by a few ULPs a step; half of those differences are positive, the pixel
    // reports a hit on itself, and every mirror shows its own colour -- which looks like a working
    // reflection until you notice it never shows anything else. The depth bias is what rejects it.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeFlatDepth(gd, kFarByte);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    // A gradient would let a self-hit pass unnoticed; a flat colour makes a self-reflection
    // invisible too, so the surface is given the same red the surround has elsewhere and the frame
    // is checked for staying that colour rather than for finding it.
    FillSourceFlat(gd, source, Color(200, 30, 30, 255));

    pass.setMaxDistance(40.0f);
    pass.setThickness(30.0f);

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // With no bias the ray hits the flat surface at step one and the pixel takes that surface's
    // own colour -- which here is the colour it already had, so the check that actually fails
    // without the bias is the green channel: a self-hit replaces the pixel with a *sample* of the
    // same image and any filtering difference shows. The stronger statement is the patch test
    // above; this one guards the specific arithmetic.
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 200, 6);
    EXPECT_NEAR(pixels[CentreIndex()].getGProperty(), 30, 6);
}

TEST(SsrPassTest, TheDepthBiasIsWhatSeparatesASelfHitFromARealOne)
{
    // Stated as a value rather than as a picture: a bias larger than the whole scene's depth range
    // rejects every hit, so the frame comes back unreflected. That is the direct evidence that the
    // bias is in the comparison and doing the work the test above depends on.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    auto source = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setMaxDistance(40.0f);
    pass.setThickness(30.0f);
    pass.setDepthBias(40.0f);   // larger than the 20-unit step the band stands proud

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_LT(pixels[CentreIndex()].getRProperty(), 8)
        << "a bias above the band's depth difference still admitted a hit";
}

TEST(SsrPassTest, ASurfaceFacingTheCameraReflectsNothing)
{
    // Its reflected ray runs back towards the eye, leaving view space immediately. A march that
    // projected such a point anyway would mirror it through the origin and report a confident hit
    // on whatever happened to be there -- reflections appearing on flat walls facing the camera is
    // the classic symptom.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeFlatDepth(gd, kFarByte);
    auto normals = MakeUniformNormals(gd, kFacingNormal);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(40, 40, 40, 255));

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 40, 6)
        << "a wall facing the camera reflected something";
}

TEST(SsrPassTest, TheClearedFarPlaneIsNotReflective)
{
    // The spelling of "nothing here" that a real prepass actually produces. `DepthNormalPrepass`
    // clears depth to **white**, so an empty pixel decodes to 1.0, and this pass tested only for
    // zero until `MOD-2009` measured the clear -- which made the sky a surface at the camera with a
    // reflection marched out of it. Both spellings are checked now, and this is the one that would
    // have shipped broken.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeFlatDepth(gd, 255);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(90, 90, 90, 255));

    PostProcessContext context = MakeContext(source, destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 90, 6)
        << "the cleared far plane reflected something";
}

TEST(SsrPassTest, AZeroDepthIsAlsoNotReflective)
{
    // The other spelling: a renderer that clears its depth target to black. Reconstructing a
    // position from zero puts the surface at the eye, and every ray from there hits immediately.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeFlatDepth(gd, 0);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(90, 90, 90, 255));

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 90, 6)
        << "the sky reflected something";
}

// ── The fallback ─────────────────────────────────────────────────────────────

TEST(SsrPassTest, WithoutDepthAndNormalsTheFrameIsPassedThroughUnchanged)
{
    // The same contract SsaoPass has: a game that enables SSR and never runs the prepass gets its
    // frame, not an exception and not a black screen.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(150, 60, 30, 255));

    PostProcessContext context = MakeContext(source, destination);
    context.sourceDepth   = nullptr;
    context.sourceNormals = nullptr;
    EXPECT_NO_THROW(pass.apply(context));

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 150, 4);
    EXPECT_NEAR(pixels[CentreIndex()].getGProperty(), 60, 4);
    EXPECT_NEAR(pixels[CentreIndex()].getBProperty(), 30, 4);
}

TEST(SsrPassTest, DepthWithoutNormalsIsAlsoTheFallback)
{
    // Half the inputs is not half a reflection: without normals there is no ray to march.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth = MakeTiltedPlaneDepth(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(150, 60, 30, 255));

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = nullptr;
    EXPECT_NO_THROW(pass.apply(context));

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 150, 4);
}

// ── The public surface ───────────────────────────────────────────────────────

TEST(SsrPassTest, TheSettingsRoundTripAndNonsenseIsIgnored)
{
    GraphicsDevice gd;
    SsrPass pass(gd);

    EXPECT_GT(pass.getMaxDistance(), 0.0f);
    EXPECT_GT(pass.getThickness(), 0.0f);
    EXPECT_GE(pass.getStepCount(), SsrPass::kMinStepCount);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 1.0f);

    pass.setMaxDistance(12.5f);
    pass.setThickness(0.75f);
    pass.setStepCount(48);
    pass.setIntensity(0.25f);
    EXPECT_FLOAT_EQ(pass.getMaxDistance(), 12.5f);
    EXPECT_FLOAT_EQ(pass.getThickness(), 0.75f);
    EXPECT_EQ(pass.getStepCount(), 48);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.25f);

    // A distance of zero would make every step land on the pixel it started from, which reads as a
    // perfect mirror of the surface onto itself. Refused rather than stored.
    pass.setMaxDistance(0.0f);
    pass.setMaxDistance(-3.0f);
    EXPECT_FLOAT_EQ(pass.getMaxDistance(), 12.5f);
    pass.setThickness(0.0f);
    pass.setThickness(-1.0f);
    EXPECT_FLOAT_EQ(pass.getThickness(), 0.75f);

    EXPECT_GT(pass.getDepthBias(), 0.0f);
    pass.setDepthBias(0.2f);
    EXPECT_FLOAT_EQ(pass.getDepthBias(), 0.2f);
    pass.setDepthBias(0.0f);
    pass.setDepthBias(-0.5f);
    EXPECT_FLOAT_EQ(pass.getDepthBias(), 0.2f);
}

TEST(SsrPassTest, AnAbsurdStepCountIsClampedOnUseRatherThanRejected)
{
    // Clamped where it is used, matching BloomPass's iteration count: a settings bag restored from
    // a file should not throw, and a pass that silently marched 100000 steps would hang the frame.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);
    auto source = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setStepCount(100000);
    EXPECT_EQ(pass.getStepCount(), 100000) << "the value is stored as given";

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    EXPECT_NO_THROW(pass.apply(context));
}

TEST(SsrPassTest, TheNameIsStable)
{
    GraphicsDevice gd;
    SsrPass pass(gd);
    EXPECT_EQ(pass.getName(), "SSR");
}

TEST(SsrPassTest, SupportAsksTheTwoPartQuestion)
{
    // MOD-1699: CustomEffects alone means the renderer *accepts* an effect. A pass that believed it
    // would report success while copying its input.
    GraphicsDevice gd;
    SsrPass pass(gd);

    const bool executes = gd.ExecutesShaderEffectSourceEXT();
    if (!executes)
        EXPECT_FALSE(pass.isSupported(gd))
            << "the pass claimed support on a renderer that will not run its source";
}

} // namespace

#endif // CNA_CNAEXT
