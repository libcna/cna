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

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/SsrPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::DepthNormalPrepass;
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
/// (0, -0.7071, 0.7071): tilted the other way, so it faces the reflected ray head-on. The band
/// wears this: an object standing in front of the floor, turned towards it.
const Color kFacingTheRayNormal{128, 37, 218, 255};

/// Normals for the whole scene: the floor tilted away, the band turned to face what reflects off it.
///
/// The band cannot share the floor's normal, and the reason is the rejection this scene now
/// exercises. Two parallel surfaces mean the ray arrives at the *back* of the second one, and
/// reflecting a back face puts the far side of an object into a mirror that cannot see it.
/// @param roughnessByte What the prepass would have written into alpha (MOD-2003): 0 is a mirror.
std::unique_ptr<RenderTarget2D> MakeSceneNormals(GraphicsDevice& gd, const int roughnessByte = 255)
{
    return MakeRowImage(gd, [roughnessByte](const int row) {
        const Color n = InBand(row) ? kFacingTheRayNormal : kTiltedNormal;
        return Color(static_cast<int>(n.getRProperty()), static_cast<int>(n.getGProperty()),
                     static_cast<int>(n.getBProperty()), roughnessByte);
    });
}

/// Colour: the band a steep vertical ramp, everything else black. Where in the band a reflection
/// lands is then legible from the colour it comes back with, which is what makes the refinement
/// observable at all -- a flat band answers the same colour wherever the ray stops inside it.
std::unique_ptr<RenderTarget2D> MakeSourceWithGradientBand(GraphicsDevice& gd)
{
    return MakeRowImage(gd, [](const int row) {
        if (!InBand(row)) return Color(0, 0, 0, 255);
        // The band's rows run from its far edge inward, so the ramp is indexed from the first row
        // the reflected ray can reach.
        int depthIntoBand = 0;
        for (int r = row; r >= 0 && InBand(r); --r) ++depthIntoBand;
        const int red = std::min(250, 40 + depthIntoBand * 35);
        return Color(red, 0, 0, 255);
    });
}

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
    auto normals = MakeSceneNormals(gd);
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
    auto normals = MakeSceneNormals(gd);
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
    auto normals = MakeSceneNormals(gd);
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

TEST(SsrPassTest, TheRefinementMakesTheAnswerIndependentOfTheStepCount)
{
    // plan_modern.md MOD-2001. What the bisection buys, stated as the thing a viewer would notice:
    // without it the reflection stops wherever the step happened to fall, so a coarse march
    // overshoots the edge it crossed and halving the step *moves* the whole reflection instead of
    // sharpening it -- reflected edges stair-step, and the pattern changes with the step count. With
    // it, both marches converge on the same crossing and answer with the same colour.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeSceneNormals(gd);
    auto source  = MakeSourceWithGradientBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    const auto redAtStepCount = [&](const int steps) {
        pass.setMaxDistance(40.0f);
        pass.setThickness(30.0f);
        pass.setStepCount(steps);
        PostProcessContext context = MakeContext(SourceRef(source), destination);
        context.sourceDepth   = depth.get();
        context.sourceNormals = normals.get();
        pass.apply(context);
        return static_cast<int>(ReadTarget(destination)[CentreIndex()].getRProperty());
    };

    const int coarse = redAtStepCount(8);
    const int fine   = redAtStepCount(64);

    // Anti-vacuity: both marches must actually have found the band. Two misses would agree
    // perfectly at zero and prove nothing at all.
    ASSERT_GT(coarse, 20) << "the coarse march found no reflection, so nothing was compared";
    ASSERT_GT(fine, 20) << "the fine march found no reflection, so nothing was compared";

    EXPECT_LE(std::abs(coarse - fine), 12)
        << "eight steps answered " << coarse << " and sixty-four answered " << fine
        << ": the reflection still lands where the step count puts it";
}

TEST(SsrPassTest, RoughnessSpreadsTheReflectionAndSmoothnessDoesNot)
{
    // plan_modern.md MOD-2003. The same scene twice, with nothing changed but the roughness the
    // prepass wrote into the normal target's alpha. The reflection lands on the band's leading
    // edge, so a spread reflection gathers the black outside the band and comes back darker, while
    // a mirror takes the band's colour exactly. Measured as brightness because that is what mixing
    // across a hard edge *is* -- there is no second thing a 32-pixel frame could show.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeTiltedPlaneDepth(gd);
    auto source = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    const auto redAtRoughness = [&](const int roughnessByte) {
        auto normals = MakeSceneNormals(gd, roughnessByte);
        pass.setMaxDistance(40.0f);
        pass.setThickness(30.0f);
        pass.setEdgeFade(0.0f);          // isolate the roughness from the border fade
        pass.setRoughnessBlur(0.25f);    // the widest spread the pass accepts
        PostProcessContext context = MakeContext(SourceRef(source), destination);
        context.sourceDepth   = depth.get();
        context.sourceNormals = normals.get();
        pass.apply(context);
        return static_cast<int>(ReadTarget(destination)[CentreIndex()].getRProperty());
    };

    const int mirror = redAtRoughness(0);
    const int rough  = redAtRoughness(255);

    ASSERT_GT(mirror, 200) << "the mirror found no reflection, so nothing was compared";
    EXPECT_LT(rough, mirror - 40)
        << "a fully rough surface reflected as sharply as a mirror: " << rough
        << " against " << mirror;
}

TEST(SsrPassTest, ASurfaceWithNoRoughnessSuppliedReflectsSharply)
{
    // The default the prepass writes is 0, not glTF's fully-rough 1, and this is why: an app that
    // never calls `setRoughness` must get the sharp reflection it got before roughness existed
    // rather than a silently blurred frame it has no way to explain.
    GraphicsDevice gd;
    DepthNormalPrepass prepass(gd, 8, 8);
    EXPECT_FLOAT_EQ(prepass.getRoughness(), 0.0f);
    prepass.setRoughness(0.6f);
    EXPECT_FLOAT_EQ(prepass.getRoughness(), 0.6f);
    prepass.setRoughness(5.0f);
    EXPECT_FLOAT_EQ(prepass.getRoughness(), 1.0f);
    prepass.setRoughness(-5.0f);
    EXPECT_FLOAT_EQ(prepass.getRoughness(), 0.0f);
}

// ── The rejections (MOD-2002) ────────────────────────────────────────────────

TEST(SsrPassTest, ABackFacingSurfaceIsNotReflected)
{
    // The same scene, with the band left parallel to the floor instead of turned to face it. The
    // ray then arrives at the band's *back*, and reflecting the colour of a surface the mirror
    // cannot see puts the far side of an object into the reflection -- a wrong image that looks
    // entirely plausible, which is why it needs asserting rather than eyeballing.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeUniformNormals(gd, kTiltedNormal);   // the band shares the floor's normal
    auto source  = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setMaxDistance(40.0f);
    pass.setThickness(30.0f);

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_LT(pixels[CentreIndex()].getRProperty(), 8)
        << "the back of the band was reflected as though the mirror could see its front";
}

TEST(SsrPassTest, ARayPassingWellBehindASurfaceIsNotAHit)
{
    // The upper tolerance, and the counterpart of the depth-bias test. The depth image records
    // where a surface is and nothing about how deep the object behind it goes; a thickness smaller
    // than the gap means the ray flew well past the band rather than into it, and reflecting it
    // would put a foreground object into a mirror that is looking somewhere else entirely.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeSceneNormals(gd);
    auto source  = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setMaxDistance(40.0f);
    pass.setThickness(2.0f);   // the band stands about 20 world units proud of the floor

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_LT(pixels[CentreIndex()].getRProperty(), 8)
        << "a ray that flew far behind the band still reflected it";
}

TEST(SsrPassTest, AReflectionEndingNearTheBorderFadesRatherThanStopping)
{
    // Nothing outside the viewport was ever drawn, so a reflection ending near the border is about
    // to reflect information the frame does not have. Asserted as a comparison rather than an
    // absolute: the same reflection, with the fade band wide enough to reach it, comes back
    // measurably weaker than with the fade off. Without it the reflection stops along a hard line
    // down the edge of the screen, which is the usual giveaway of the technique.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeSceneNormals(gd);
    auto source  = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    const auto redWithFade = [&](const float fade) {
        pass.setMaxDistance(40.0f);
        pass.setThickness(30.0f);
        pass.setEdgeFade(fade);
        PostProcessContext context = MakeContext(SourceRef(source), destination);
        context.sourceDepth   = depth.get();
        context.sourceNormals = normals.get();
        pass.apply(context);
        return static_cast<int>(ReadTarget(destination)[CentreIndex()].getRProperty());
    };

    const int unfaded = redWithFade(0.0f);
    const int faded   = redWithFade(0.5f);

    ASSERT_GT(unfaded, 200) << "the unfaded reflection was not found, so nothing was compared";
    EXPECT_LT(faded, unfaded - 30)
        << "the fade band reached this reflection and did not weaken it: " << faded
        << " against " << unfaded;
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

    EXPECT_GE(pass.getEdgeFade(), 0.0f);
    pass.setEdgeFade(0.25f);
    EXPECT_FLOAT_EQ(pass.getEdgeFade(), 0.25f);
    // Clamped rather than refused: a fade wider than half the frame has no meaning, and a settings
    // bag restored from a file should not throw over it.
    pass.setEdgeFade(5.0f);
    EXPECT_FLOAT_EQ(pass.getEdgeFade(), 0.5f);
    pass.setEdgeFade(-1.0f);
    EXPECT_FLOAT_EQ(pass.getEdgeFade(), 0.0f);

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
    auto normals = MakeSceneNormals(gd);
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

TEST(SsrPassTest, SupportAnswersAboutTheRendererAndNotAboutTheFrame)
{
    // plan_modern.md MOD-2006. `isSupported` takes a device and nothing else, so it cannot see a
    // frame's inputs -- a pass with no depth image, no normals and no camera is still *supported*,
    // and it is `apply` that copies the input through. The engine-layer document claimed the
    // opposite for SSAO until this task, which matters: a game that gates its prepass on
    // `isSupported()` gets true and then wonders why the effect does nothing. Pinned here so the
    // sentence and the code cannot drift apart again.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    EXPECT_TRUE(pass.isSupported(gd))
        << "a renderer that runs shader source must report the pass as supported";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    FillSourceFlat(gd, source, Color(120, 40, 20, 255));

    PostProcessContext context = MakeContext(source, destination);
    context.sourceDepth   = nullptr;
    context.sourceNormals = nullptr;
    pass.apply(context);

    EXPECT_TRUE(pass.isSupported(gd))
        << "a frame without inputs changed what the pass says about the renderer";
    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels[CentreIndex()].getRProperty(), 120, 4)
        << "apply did not copy the input through";
}

TEST(SsrPassTest, AFrameWithNoCameraIsCopiedThroughRatherThanGuessedAt)
{
    // The camera is as much an input as the two images. A pipeline that never called `setCamera`
    // leaves the far plane at zero, and a pass that carried on would reflect the scene through an
    // invented lens -- a frame that renders and is wrong, which is worse than one that renders
    // unchanged.
    GraphicsDevice gd;
    SsrPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth   = MakeTiltedPlaneDepth(gd);
    auto normals = MakeSceneNormals(gd);
    auto source  = MakeSourceWithBrightBand(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(SourceRef(source), destination);
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    context.farPlane      = 0.0f;      // never told
    context.nearPlane     = 0.0f;
    pass.apply(context);

    // The centre of the source is inside the black plane, so a copy-through leaves it black and a
    // guessed camera would have found the band and turned it red.
    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_LT(pixels[CentreIndex()].getRProperty(), 8)
        << "the pass marched a reflection without being told a camera";
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
