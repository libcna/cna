// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2120..MOD-2123: screen-space contact shadows.
//
// The pass exists because of a limit no shadow-map resolution removes: where an object meets the
// floor, visibility changes over a distance smaller than the map's filter width, so the map
// averages the contact away and the object floats. These tests state that gap as arithmetic --
// a short ray through the prepass depth, a thickness the depth image cannot supply, and a
// composition that multiplies rather than adds -- rather than as a picture someone looked at.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/ContactShadowPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::ContactShadowPass;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::FullscreenPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CnaTest::EngineLayer::DepthTexel;

constexpr int   kSize        = 64;
constexpr float kNearPlane   = 0.1f;
constexpr float kFarPlane    = 10.0f;
constexpr float kWallDepth   = 5.0f;     // the floor the ray travels over
constexpr float kPatchDepth  = 4.85f;    // the object resting on it, 15 cm nearer the camera
constexpr int   kPatchColumns = 24;      // columns [0, 24) hold the object

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNearPlane, kFarPlane);
}

/// World units covered by one pixel at the wall's distance -- what turns a ray length in metres
/// into the screen distance the test expects a shadow to reach.
float WorldUnitsPerPixel()
{
    const float halfHeight = kWallDepth * std::tan(MathHelper::PiOver4 * 0.5f);
    return (halfHeight * 2.0f) / static_cast<float>(kSize);
}

/// A floor at kWallDepth with an object sitting on it across the left kPatchColumns columns.
std::unique_ptr<Texture2D> MakeContactDepth(GraphicsDevice& gd)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const float depth = (x < kPatchColumns ? kPatchDepth : kWallDepth) / kFarPlane;
            texels.push_back(DepthTexel(gd, depth));
        }
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::unique_ptr<Texture2D> MakeFlatScene(GraphicsDevice& gd, const int level)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize,
                              Color(level, level, level, 255));
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

PostProcessContext MakeContext(Texture2D* scene, Texture2D* depth, RenderTarget2D* destination)
{
    PostProcessContext context;
    context.source            = scene;
    context.sourceDepth       = depth;
    context.destination       = destination;
    context.width             = kSize;
    context.height            = kSize;
    context.projection        = Projection();
    context.inverseProjection = Matrix::Invert(Projection());
    context.nearPlane         = kNearPlane;
    context.farPlane          = kFarPlane;
    // The camera sits at the origin looking down -Z, so view and its inverse are both the
    // identity: the light direction set in world space arrives in view space unchanged, which is
    // what lets this test state the march direction in screen terms.
    context.inverseView       = Matrix::getIdentityProperty();
    return context;
}

/// Sets the pass up so its ray reaches `pixels` pixels across the wall.
void AimTheRay(ContactShadowPass& pass, const float pixels)
{
    // Light travelling in +X means the ray marches toward -X, which is toward lower columns --
    // toward the object. Every expectation below is written in those terms.
    pass.setLightDirection(Vector3(1.0f, 0.0f, 0.0f));
    pass.setMaxDistance(pixels * WorldUnitsPerPixel());
    pass.setStepCount(32);
    pass.setBias(0.02f);
    pass.setThickness(0.3f);
    pass.setIntensity(1.0f);
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// The mean red channel over one column of the output, so a claim is about a column of the wall
/// rather than about whichever single pixel a rounding landed on.
float ColumnMean(const std::vector<Color>& pixels, const int column)
{
    float total = 0.0f;
    for (int y = 0; y < kSize; ++y)
        total += static_cast<float>(pixels[static_cast<std::size_t>(y) * kSize + column]
                                        .getRProperty());
    return total / static_cast<float>(kSize);
}

// ── Identity and settings ───────────────────────────────────────────────────

TEST(ContactShadowPassTest, ItNamesItself)
{
    GraphicsDevice gd;
    ContactShadowPass pass(gd);
    EXPECT_EQ(pass.getName(), "ContactShadow");
}

TEST(ContactShadowPassTest, TheDefaultRayIsShort)
{
    // Not a style point. A long ray is a bad shadow map: noisier, more expensive, and wrong
    // wherever the occluder leaves the screen. The default has to say what the pass is for.
    GraphicsDevice gd;
    ContactShadowPass pass(gd);
    EXPECT_LE(pass.getMaxDistance(), 0.5f);
    EXPECT_GT(pass.getMaxDistance(), 0.0f);
    EXPECT_GE(pass.getStepCount(), 4);
    EXPECT_GT(pass.getThickness(), 0.0f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 1.0f);
    EXPECT_GT(pass.getBias(), 0.0f);
}

TEST(ContactShadowPassTest, EverySettingRoundTrips)
{
    GraphicsDevice gd;
    ContactShadowPass pass(gd);

    pass.setLightDirection(Vector3(0.0f, -1.0f, 0.5f));
    EXPECT_FLOAT_EQ(pass.getLightDirection().Z, 0.5f);
    pass.setMaxDistance(0.75f);
    EXPECT_FLOAT_EQ(pass.getMaxDistance(), 0.75f);
    pass.setStepCount(48);
    EXPECT_EQ(pass.getStepCount(), 48);
    pass.setThickness(0.4f);
    EXPECT_FLOAT_EQ(pass.getThickness(), 0.4f);
    pass.setIntensity(0.5f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.5f);
    pass.setBias(0.03f);
    EXPECT_FLOAT_EQ(pass.getBias(), 0.03f);
}

TEST(ContactShadowPassTest, TheLightDirectionIsNotNormalizedOnAssignment)
{
    // The same courtesy DirectionalLightEXT extends: a caller may write a convenient (-1, -1, -1)
    // and read back what they wrote.
    GraphicsDevice gd;
    ContactShadowPass pass(gd);
    pass.setLightDirection(Vector3(-1.0f, -1.0f, -1.0f));
    EXPECT_FLOAT_EQ(pass.getLightDirection().X, -1.0f);
}

// ── The occlusion test, and its two twins ───────────────────────────────────

TEST(ContactShadowPassTest, AnOccluderInFrontOfTheRayIsAHit)
{
    EXPECT_TRUE(ContactShadowPass::isOccluded(5.0f, 4.85f, 0.02f, 0.3f));
}

TEST(ContactShadowPassTest, ASurfaceDoesNotShadowItself)
{
    // The first step of a ray leaving a flat surface is still level with it, so the difference is
    // zero give or take a few ULPs. Without the bias half of every lit surface finds itself.
    EXPECT_FALSE(ContactShadowPass::isOccluded(5.0f, 5.0f, 0.02f, 0.3f));
    EXPECT_FALSE(ContactShadowPass::isOccluded(5.0f, 5.0f - 1e-4f, 0.02f, 0.3f));
}

TEST(ContactShadowPassTest, AnOccluderBeyondTheAssumedThicknessIsNotAHit)
{
    // MOD-2121. The ray is deemed to have passed behind the object rather than into it -- the one
    // decision the depth image cannot inform, because it holds a surface and not a solid.
    EXPECT_FALSE(ContactShadowPass::isOccluded(5.0f, 4.0f, 0.02f, 0.3f));
}

TEST(ContactShadowPassTest, SomethingBehindTheRayIsNotAHit)
{
    EXPECT_FALSE(ContactShadowPass::isOccluded(5.0f, 5.5f, 0.02f, 0.3f));
}

TEST(ContactShadowPassTest, TheShaderAgreesWithTheCpuTwinOnEveryCase)
{
    // The house pattern: the predicate is written twice and the two are compared where one of them
    // actually runs. A C++ twin nothing checks against the GPU is a second opinion, not a proof.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

    std::string source = "#version 300 es\nprecision highp float;\n";
    source += ContactShadowPass::getOcclusionTestGlsl();
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uRay;
uniform float uScene;
uniform float uBias;
uniform float uThickness;
void main() {
    bool hit = cnaContactOccluded(uRay, uScene, uBias, uThickness);
    FragColor = vec4(hit ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
)";

    ShaderEffect probe(gd, kVertexSource, source);
    ASSERT_TRUE(probe.IsEffectValid());

    Texture2D dummy(gd, 1, 1);
    const Color white = Color::White;
    dummy.SetData(&white, 1);
    RenderTarget2D destination(gd, 1, 1);
    FullscreenPass fullscreen(gd);

    struct Case { float ray; float scene; float bias; float thickness; };
    const std::vector<Case> cases = {
        {5.0f, 5.0f,   0.02f, 0.3f},
        {5.0f, 4.99f,  0.02f, 0.3f},   // inside the bias
        {5.0f, 4.95f,  0.02f, 0.3f},
        {5.0f, 4.85f,  0.02f, 0.3f},
        {5.0f, 4.71f,  0.02f, 0.3f},   // just inside the thickness
        {5.0f, 4.69f,  0.02f, 0.3f},   // just outside it
        {5.0f, 4.0f,   0.02f, 0.3f},
        {5.0f, 5.5f,   0.02f, 0.3f},   // behind the ray
        {5.0f, 4.85f,  0.2f,  0.3f},   // a bias wide enough to swallow the hit
        {5.0f, 4.85f,  0.02f, 0.1f},   // a thickness too thin to hold it
        {0.5f, 0.4f,   0.02f, 0.3f},
        {9.9f, 9.7f,   0.02f, 0.3f},
    };

    int compared = 0;
    for (const Case& item : cases)
    {
        probe.Apply();
        probe.SetUniformFloat("uRay", item.ray);
        probe.SetUniformFloat("uScene", item.scene);
        probe.SetUniformFloat("uBias", item.bias);
        probe.SetUniformFloat("uThickness", item.thickness);
        fullscreen.draw(&dummy, &destination, &probe, 1, 1);

        Color pixel = Color::Black;
        destination.GetData(&pixel, 1);
        const bool shader = pixel.getRProperty() > 128;
        const bool cpu = ContactShadowPass::isOccluded(item.ray, item.scene, item.bias,
                                                       item.thickness);
        EXPECT_EQ(shader, cpu)
            << "ray " << item.ray << ", scene " << item.scene << ", bias " << item.bias
            << ", thickness " << item.thickness << ": the shader says " << shader
            << " and the C++ twin says " << cpu;
        ++compared;
    }
    EXPECT_EQ(compared, static_cast<int>(cases.size()));
}

// ── MOD-2122: composition ───────────────────────────────────────────────────

TEST(ContactShadowPassTest, TheTwoVisibilitiesMultiply)
{
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(0.5f, 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(1.0f, 0.4f), 0.4f);
}

TEST(ContactShadowPassTest, APixelTheShadowMapAlreadyLostDoesNotDarkenTwice)
{
    // The artefact adding the two occlusions produces: over a real contact both terms fire, so the
    // shadow gains a black core with a visible edge where only one of them reaches. A product
    // cannot -- zero is already the floor, and nothing multiplies below it.
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(0.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(0.0f, 0.0f), 0.0f);

    // The alternative, spelled out so the choice is on the record: 1 - ((1 - s) + (1 - c)) for a
    // pixel both terms half-shadow is 0, not 0.25 -- twice the darkening either term asked for.
    const float shadowMap = 0.5f;
    const float contact   = 0.5f;
    const float added = 1.0f - ((1.0f - shadowMap) + (1.0f - contact));
    EXPECT_FLOAT_EQ(added, 0.0f);
    EXPECT_GT(ContactShadowPass::combineVisibility(shadowMap, contact), added);
}

TEST(ContactShadowPassTest, CombiningClampsRatherThanTrustingItsInputs)
{
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(2.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ContactShadowPass::combineVisibility(-1.0f, 1.0f), 0.0f);
}

// ── Fallbacks ───────────────────────────────────────────────────────────────

TEST(ContactShadowPassTest, WithoutDepthTheFrameIsCopiedThroughAndTheReasonIsNamed)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto scene = MakeFlatScene(gd, 200);
    RenderTarget2D destination(gd, kSize, kSize);
    ContactShadowPass pass(gd);
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), nullptr, &destination);
    pass.apply(context);

    EXPECT_NE(pass.getFallbackReason().find("depth"), std::string::npos)
        << "the fallback was silent: '" << pass.getFallbackReason() << "'";
    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(static_cast<int>(pixels[0].getRProperty()), 200, 2);
}

TEST(ContactShadowPassTest, WithoutAFarPlaneTheStoredDepthHasNoScaleAndThePassRefuses)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    ContactShadowPass pass(gd);
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    context.farPlane = 0.0f;
    pass.apply(context);

    EXPECT_NE(pass.getFallbackReason().find("far plane"), std::string::npos)
        << "the fallback was silent: '" << pass.getFallbackReason() << "'";
}

TEST(ContactShadowPassTest, WithoutAViewMatrixTheLightDirectionCannotBePlacedAndThePassRefuses)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    ContactShadowPass pass(gd);
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    context.inverseView = Matrix();
    pass.apply(context);

    EXPECT_NE(pass.getFallbackReason().find("view"), std::string::npos)
        << "the fallback was silent: '" << pass.getFallbackReason() << "'";
}

TEST(ContactShadowPassTest, ARunThatSucceedsNamesNoReason)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    ContactShadowPass pass(gd);
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    pass.apply(context);
    EXPECT_TRUE(pass.getFallbackReason().empty()) << pass.getFallbackReason();
}

// ── MOD-2120: the march over a real contact ─────────────────────────────────

TEST(ContactShadowPassTest, TheWallBesideTheObjectDarkensAndTheWallBeyondTheRayDoesNot)
{
    // The whole claim of the pass, in one image: the floor immediately behind an object -- in the
    // light's direction -- loses its light, and the floor further away than the ray reaches keeps
    // it. A shadow map cannot make that distinction at any resolution a frame can afford, because
    // the distance involved is smaller than one of its texels.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    pass.apply(context);
    ASSERT_TRUE(pass.getFallbackReason().empty()) << pass.getFallbackReason();

    const std::vector<Color> pixels = ReadTarget(destination);

    // Two columns of wall: one right against the object, one well past where a 12-pixel ray can
    // reach. Both are wall, both were the same colour going in.
    const float contact = ColumnMean(pixels, kPatchColumns + 2);
    const float distant = ColumnMean(pixels, kPatchColumns + 30);

    EXPECT_NEAR(distant, 200.0f, 4.0f)
        << "wall beyond the ray's reach was darkened, so the ray is not the length it was given";
    EXPECT_LT(contact, 100.0f)
        << "the wall against the object did not darken: contact " << contact
        << ", distant " << distant;
}

TEST(ContactShadowPassTest, TheObjectItselfDoesNotShadowItself)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // Deep inside the object, where every step of the ray stays on its own flat surface.
    EXPECT_NEAR(ColumnMean(pixels, kPatchColumns / 2), 200.0f, 4.0f);
}

TEST(ContactShadowPassTest, TheIntensityScalesHowMuchLightAHitRemoves)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";
    AimTheRay(pass, 12.0f);
    pass.setIntensity(0.5f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // Half the light removed from a 200 pixel leaves 100, not 0.
    EXPECT_NEAR(ColumnMean(pixels, kPatchColumns + 2), 100.0f, 8.0f);
}

TEST(ContactShadowPassTest, AShortenedRayGivesUpTheContactItCanNoLongerReach)
{
    // The cost dial stated as behaviour: the ray length is the shadow's length, so shortening it
    // does not make the shadow softer or dimmer -- it makes it end sooner.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";

    // A column ten pixels out from the object: inside a twelve-pixel ray, outside a four-pixel one.
    const int column = kPatchColumns + 10;

    AimTheRay(pass, 12.0f);
    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    pass.apply(context);
    const float reached = ColumnMean(ReadTarget(destination), column);

    AimTheRay(pass, 4.0f);
    pass.apply(context);
    const float notReached = ColumnMean(ReadTarget(destination), column);

    EXPECT_LT(reached, 100.0f) << "the twelve-pixel ray did not reach the object";
    EXPECT_NEAR(notReached, 200.0f, 4.0f)
        << "the four-pixel ray darkened a column it cannot reach";
}

// ── MOD-2121: the thickness the depth image cannot supply ───────────────────

TEST(ContactShadowPassTest, AThicknessTooThinForTheGapLosesTheShadowEntirely)
{
    // The other half of MOD-2121, on the GPU rather than in the predicate. The object here is 15 cm
    // in front of the wall; a thickness of 5 cm means the ray is judged to have passed *behind* it,
    // so the contact this pass exists for disappears. Nothing about the depth image says which
    // reading is right -- that is the boundary, and it is a setting rather than a fix.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);

    pass.setThickness(0.3f);
    pass.apply(context);
    const float thick = ColumnMean(ReadTarget(destination), kPatchColumns + 2);

    pass.setThickness(0.05f);
    pass.apply(context);
    const float thin = ColumnMean(ReadTarget(destination), kPatchColumns + 2);

    EXPECT_LT(thick, 100.0f) << "the wide thickness found no contact to lose";
    EXPECT_NEAR(thin, 200.0f, 4.0f)
        << "the narrow thickness kept a shadow the ray should have been judged to pass behind: "
        << thin;
}

// ── MOD-2122: composition, on the GPU ───────────────────────────────────────

TEST(ContactShadowPassTest, APixelAlreadyBlackFromTheShadowMapStaysBlack)
{
    // The composition the screen-space pass actually performs: it multiplies into an image that
    // already carries the shadow map's term. A pixel the map put at zero is at zero afterwards --
    // there is no second darkening to apply and no way to go below it.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 0);
    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";
    AimTheRay(pass, 12.0f);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(ColumnMean(pixels, kPatchColumns + 2), 0.0f, 1.0f);
    EXPECT_NEAR(ColumnMean(pixels, kPatchColumns + 30), 0.0f, 1.0f);
}

TEST(ContactShadowPassTest, TheDarkeningIsAProductOfTheImageItIsGiven)
{
    // Stated as a ratio rather than as an absolute: the same contact over a half-lit wall removes
    // the same *fraction*, which is what makes multiplying into an already-shadowed image the
    // right composition rather than a convenience.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto depth = MakeContactDepth(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    ContactShadowPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the contact shadow pass";
    AimTheRay(pass, 12.0f);
    pass.setIntensity(0.5f);

    auto bright = MakeFlatScene(gd, 200);
    PostProcessContext brightContext = MakeContext(bright.get(), depth.get(), &destination);
    pass.apply(brightContext);
    const float brightContact = ColumnMean(ReadTarget(destination), kPatchColumns + 2);

    auto dim = MakeFlatScene(gd, 100);
    PostProcessContext dimContext = MakeContext(dim.get(), depth.get(), &destination);
    pass.apply(dimContext);
    const float dimContact = ColumnMean(ReadTarget(destination), kPatchColumns + 2);

    ASSERT_GT(brightContact, 1.0f);
    EXPECT_NEAR(dimContact / brightContact, 0.5f, 0.08f)
        << "bright " << brightContact << ", dim " << dimContact;
}

} // namespace

#endif // CNA_CNAEXT
