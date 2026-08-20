// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2140..MOD-2142: the atmosphere on geometry, not only on the sky.
//
// The layer has had an atmospheric sky since MOD-1100 and has drawn everything in front of it as
// though the air between were not there. A mountain twenty kilometres away arriving at full
// contrast against a visibly atmospheric sky is the clearest tell that the sky is a backdrop.
//
// The claim that makes this a model rather than a tint is the far end: a surface far enough away
// must contribute nothing of its own and be *replaced* by the radiance the sky would have drawn
// there. That is asserted here against `AtmosphericSky::radiance` itself, not against a tolerance.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/AerialPerspectivePass.hpp"
#include "CNA/Graphics/AtmosphericSky.hpp"
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::AerialPerspectivePass;
using CNA::Graphics::AtmosphericSky;
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

constexpr int   kSize      = 32;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 1000000.0f;   // far enough that the horizon cap can bind
constexpr float kScaleHeight = 8400.0f;

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNearPlane, kFarPlane);
}

/// A camera at the origin looking along -Z with +Y up, so the view ray at the centre of the frame
/// is horizontal and its world direction has a zero Y -- the worst case for air mass, and the one
/// this test wants.
Matrix InverseView()
{
    return Matrix::Invert(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 0.0f),
                                               Vector3(0.0f, 0.0f, -1.0f),
                                               Vector3(0.0f, 1.0f, 0.0f)));
}

std::unique_ptr<Texture2D> MakeFlatDepth(GraphicsDevice& gd, const float viewDistance)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    const std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize,
                                    DepthTexel(gd, viewDistance / kFarPlane));
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::unique_ptr<Texture2D> MakeFlatScene(GraphicsDevice& gd, const int level)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    const std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize,
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
    context.inverseView       = InverseView();
    return context;
}

Color CentrePixel(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
}

// ── Settings ────────────────────────────────────────────────────────────────

TEST(AerialPerspectiveTest, ItNamesItselfAndItsSettingsRoundTrip)
{
    GraphicsDevice gd;
    AerialPerspectivePass pass(gd);
    EXPECT_EQ(pass.getName(), "AerialPerspective");

    pass.setSunDirection(Vector3(0.0f, -0.5f, -1.0f));
    EXPECT_FLOAT_EQ(pass.getSunDirection().Z, -1.0f);
    pass.setTurbidity(4.0f);
    EXPECT_FLOAT_EQ(pass.getTurbidity(), 4.0f);
    pass.setIntensity(0.5f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.5f);
    pass.setScaleHeight(1000.0f);
    EXPECT_FLOAT_EQ(pass.getScaleHeight(), 1000.0f);
}

TEST(AerialPerspectiveTest, TurbidityBelowOneIsClampedBecauseTheModelHasNoAirBelowIt)
{
    GraphicsDevice gd;
    AerialPerspectivePass pass(gd);
    pass.setTurbidity(0.0f);
    EXPECT_FLOAT_EQ(pass.getTurbidity(), 1.0f);
}

// ── The air mass, and where world scale enters ──────────────────────────────

TEST(AerialPerspectiveTest, AirMassIsDistanceInColumnsOfAir)
{
    // The model's coefficients are optical depth through one vertical column, so a distance divided
    // by the scale height is already in the right units. This is the only place a game's world
    // scale enters the model, which is why it is a function rather than a constant in a shader.
    const Vector3 horizontal(0.0f, 0.0f, -1.0f);
    EXPECT_NEAR(AerialPerspectivePass::airMassForDistance(horizontal, 8400.0f, 8400.0f), 1.0f,
                1e-3f);
    EXPECT_NEAR(AerialPerspectivePass::airMassForDistance(horizontal, 4200.0f, 8400.0f), 0.5f,
                1e-3f);
    EXPECT_NEAR(AerialPerspectivePass::airMassForDistance(horizontal, 0.0f, 8400.0f), 0.0f, 1e-4f);
}

TEST(AerialPerspectiveTest, AirMassIsCappedAtTheWholeAtmosphereAlongThatDirection)
{
    // Not cosmetic. Without the cap a distant enough object accumulates more air than the entire
    // sky behind it has, and comes back hazier than the horizon -- which cannot happen.
    const Vector3 straightUp(0.0f, 1.0f, 0.0f);
    // Straight up is one column exactly, however far the geometry claims to be.
    EXPECT_NEAR(AerialPerspectivePass::airMassForDistance(straightUp, 1e9f, 8400.0f), 1.0f, 1e-3f);

    const Vector3 horizontal(0.0f, 0.0f, -1.0f);
    const float horizon = AerialPerspectivePass::airMassForDistance(horizontal, 1e9f, 8400.0f);
    // Kasten and Young's fit reaches about 38 at the horizon rather than the secant's infinity.
    EXPECT_GT(horizon, 30.0f);
    EXPECT_LT(horizon, 45.0f);
}

TEST(AerialPerspectiveTest, TransmittanceFallsWithAirAndFallsFastestInBlue)
{
    // Rayleigh's coefficients fall as the fourth power of wavelength, so blue is scattered out of a
    // ray several times faster than red. That ordering *is* the effect: a distant hill going grey
    // and then blue-grey is this and nothing else.
    const Vector3 none = AerialPerspectivePass::transmittance(2.5f, 0.0f);
    EXPECT_NEAR(none.X, 1.0f, 1e-5f);
    EXPECT_NEAR(none.Z, 1.0f, 1e-5f);

    const Vector3 some = AerialPerspectivePass::transmittance(2.5f, 2.0f);
    EXPECT_LT(some.Z, some.Y);
    EXPECT_LT(some.Y, some.X);
    EXPECT_LT(some.X, 1.0f);

    const Vector3 lots = AerialPerspectivePass::transmittance(2.5f, 30.0f);
    EXPECT_LT(lots.X, 0.3f);
}

TEST(AerialPerspectiveTest, TheShaderAgreesWithTheCpuTwinOnAirMass)
{
    // The house pattern. `cnaAerialAirMass` and `airMassForDistance` are two statements of one
    // rule, and the only comparison that proves them equal is on the GPU.
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
    source += AtmosphericSky::getModelGlsl();
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec3  uDirection;
uniform float uDistance;
uniform float uScaleHeight;
uniform float uScale;
void main() {
    float mass = cnaAerialAirMass(uDirection, uDistance, uScaleHeight);
    FragColor = vec4(clamp(mass / uScale, 0.0, 1.0), 0.0, 0.0, 1.0);
}
)";

    ShaderEffect probe(gd, kVertexSource, source);
    ASSERT_TRUE(probe.IsEffectValid());

    Texture2D dummy(gd, 1, 1);
    const Color white = Color::White;
    dummy.SetData(&white, 1);
    RenderTarget2D destination(gd, 1, 1);
    FullscreenPass fullscreen(gd);

    struct Case { Vector3 direction; float distance; };
    const std::vector<Case> cases = {
        {Vector3(0.0f, 1.0f, 0.0f),   4200.0f},
        {Vector3(0.0f, 1.0f, 0.0f),   1e9f},
        {Vector3(0.0f, 0.0f, -1.0f),  8400.0f},
        {Vector3(0.0f, 0.0f, -1.0f),  84000.0f},
        {Vector3(0.0f, 0.5f, -1.0f),  20000.0f},
        {Vector3(0.0f, -0.3f, -1.0f), 20000.0f},
        {Vector3(1.0f, 0.2f, 0.0f),   1000.0f},
    };

    constexpr float kScale = 48.0f;   // above the horizon's ~38, so nothing clamps
    for (const Case& item : cases)
    {
        probe.Apply();
        probe.SetUniformVec3("uDirection", item.direction.X, item.direction.Y, item.direction.Z);
        probe.SetUniformFloat("uDistance", item.distance);
        probe.SetUniformFloat("uScaleHeight", kScaleHeight);
        probe.SetUniformFloat("uScale", kScale);
        fullscreen.draw(&dummy, &destination, &probe, 1, 1);

        Color pixel = Color::Black;
        destination.GetData(&pixel, 1);
        const float shader = static_cast<float>(pixel.getRProperty()) / 255.0f * kScale;
        const float cpu = AerialPerspectivePass::airMassForDistance(item.direction, item.distance,
                                                                    kScaleHeight);
        // One eighth-bit step of the scaled encoding, which is what the probe can carry.
        EXPECT_NEAR(shader, cpu, kScale / 255.0f * 1.5f)
            << "direction (" << item.direction.X << ", " << item.direction.Y << ", "
            << item.direction.Z << ") at " << item.distance;
    }
}

// ── Fallbacks ───────────────────────────────────────────────────────────────

TEST(AerialPerspectiveTest, WithoutDepthTheFrameIsCopiedThroughAndTheReasonIsNamed)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto scene = MakeFlatScene(gd, 200);
    RenderTarget2D destination(gd, kSize, kSize);
    AerialPerspectivePass pass(gd);

    PostProcessContext context = MakeContext(scene.get(), nullptr, &destination);
    pass.apply(context);

    EXPECT_NE(pass.getFallbackReason().find("depth"), std::string::npos)
        << "the fallback was silent: '" << pass.getFallbackReason() << "'";
    EXPECT_NEAR(static_cast<int>(CentrePixel(destination).getRProperty()), 200, 2);
}

TEST(AerialPerspectiveTest, WithoutCameraMatricesThePassRefuses)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeFlatDepth(gd, 5000.0f);
    RenderTarget2D destination(gd, kSize, kSize);
    AerialPerspectivePass pass(gd);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);
    context.inverseView = Matrix();
    pass.apply(context);
    EXPECT_NE(pass.getFallbackReason().find("camera"), std::string::npos)
        << "the fallback was silent: '" << pass.getFallbackReason() << "'";
}

// ── MOD-2140: the effect itself ─────────────────────────────────────────────

TEST(AerialPerspectiveTest, NearGeometryIsUntouchedAndDistantGeometryIsNot)
{
    // The shape of the whole effect in one comparison. At a hundred metres there is essentially no
    // air in the way and the surface must arrive as it was drawn; at twenty kilometres it must not.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    RenderTarget2D destination(gd, kSize, kSize);

    AerialPerspectivePass pass(gd);
    if (!pass.isSupported(gd)) GTEST_SKIP() << "this renderer cannot run the pass";
    pass.setSunDirection(Vector3(0.0f, -0.4f, -1.0f));
    pass.setScaleHeight(kScaleHeight);

    auto near = MakeFlatDepth(gd, 100.0f);
    PostProcessContext nearContext = MakeContext(scene.get(), near.get(), &destination);
    pass.apply(nearContext);
    ASSERT_TRUE(pass.getFallbackReason().empty()) << pass.getFallbackReason();
    const Color close = CentrePixel(destination);

    auto far = MakeFlatDepth(gd, 20000.0f);
    PostProcessContext farContext = MakeContext(scene.get(), far.get(), &destination);
    pass.apply(farContext);
    const Color distant = CentrePixel(destination);

    std::printf("    grey 200 at 100 m: (%d, %d, %d); at 20 km: (%d, %d, %d)\n",
                close.getRProperty(), close.getGProperty(), close.getBProperty(),
                distant.getRProperty(), distant.getGProperty(), distant.getBProperty());

    EXPECT_NEAR(static_cast<int>(close.getRProperty()), 200, 4)
        << "a surface a hundred metres away was changed, so the world scale is wrong";
    EXPECT_GT(std::abs(distant.getRProperty() - close.getRProperty()), 8)
        << "twenty kilometres of air changed nothing";
}

TEST(AerialPerspectiveTest, TheSkyItselfIsLeftAloneBecauseItAlreadyCarriesTheAtmosphere)
{
    // The seam where geometry meets sky is where a double-count shows first, and it shows as a
    // visible edge along every silhouette rather than as a wrong colour.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 120);
    // The prepass clears depth to white: nothing was drawn.
    auto empty = MakeFlatDepth(gd, kFarPlane);
    RenderTarget2D destination(gd, kSize, kSize);

    AerialPerspectivePass pass(gd);
    if (!pass.isSupported(gd)) GTEST_SKIP() << "this renderer cannot run the pass";

    PostProcessContext context = MakeContext(scene.get(), empty.get(), &destination);
    pass.apply(context);
    EXPECT_NEAR(static_cast<int>(CentrePixel(destination).getRProperty()), 120, 2);
}

TEST(AerialPerspectiveTest, FarEnoughAwayBlueIsReplacedByTheSkyItself)
{
    // MOD-2140's real claim, and what makes this a model rather than a distance tint: where the air
    // is thick enough, the geometry contributes nothing of its own and what remains is the radiance
    // `AtmosphericSky` would have drawn along that ray. Asserted two ways at once -- two opposite
    // surfaces converge to the same pixel, and that pixel is the sky's own colour.
    //
    // In **blue**. Red is a separate case below, and the difference between the two is the effect.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    RenderTarget2D destination(gd, kSize, kSize);
    // Past 38 air masses the cap is in force, which is the state the sky itself is in.
    auto veryFar = MakeFlatDepth(gd, kFarPlane * 0.9f);

    AerialPerspectivePass pass(gd);
    if (!pass.isSupported(gd)) GTEST_SKIP() << "this renderer cannot run the pass";
    const Vector3 sun(0.0f, -0.4f, -1.0f);
    constexpr float kTurbidity = 2.5f;

    // The sky at the horizon is well above 1.0 in every channel, so a comparison at intensity 1
    // would be between two clipped whites and would prove nothing. The intensity is chosen from the
    // model itself to put the brightest channel at 0.6.
    const Vector3 sky = AtmosphericSky::radiance(Vector3(0.0f, 0.0f, -1.0f), sun, kTurbidity);
    const float brightest = std::max({sky.X, sky.Y, sky.Z});
    ASSERT_GT(brightest, 1e-3f);
    const float intensity = 0.6f / brightest;

    pass.setSunDirection(sun);
    pass.setScaleHeight(kScaleHeight);
    pass.setTurbidity(kTurbidity);
    pass.setIntensity(intensity);

    auto black = MakeFlatScene(gd, 0);
    PostProcessContext blackContext = MakeContext(black.get(), veryFar.get(), &destination);
    pass.apply(blackContext);
    const Color fromBlack = CentrePixel(destination);

    auto white = MakeFlatScene(gd, 255);
    PostProcessContext whiteContext = MakeContext(white.get(), veryFar.get(), &destination);
    pass.apply(whiteContext);
    const Color fromWhite = CentrePixel(destination);

    const auto encode = [](const float value) {
        return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    std::printf("    at the horizon: from black (%d, %d, %d), from white (%d, %d, %d), "
                "AtmosphericSky says (%d, %d, %d)\n",
                fromBlack.getRProperty(), fromBlack.getGProperty(), fromBlack.getBProperty(),
                fromWhite.getRProperty(), fromWhite.getGProperty(), fromWhite.getBProperty(),
                encode(sky.X * intensity), encode(sky.Y * intensity), encode(sky.Z * intensity));

    // Anti-vacuity: if the sky's blue were near zero, "converged to the sky" and "went black"
    // would be the same assertion.
    ASSERT_GT(encode(sky.Z * intensity), 40) << "the sky is too dark here to tell convergence from "
                                                "an unlit frame";
    EXPECT_LE(std::abs(fromBlack.getBProperty() - fromWhite.getBProperty()), 2)
        << "a black surface and a white one at the horizon did not converge in blue, so the "
           "geometry's own colour still survives the whole atmosphere";
    EXPECT_NEAR(static_cast<int>(fromBlack.getBProperty()), encode(sky.Z * intensity), 4)
        << "the far end does not meet the sky the same model draws";
}

TEST(AerialPerspectiveTest, RedSurvivesTheWholeAtmosphereAndThatIsTheEffect)
{
    // The finding that came out of writing the test above, and it is physics rather than a defect.
    // Rayleigh's red coefficient is 0.0464 against blue's 0.2650, so even the horizon's ~38 air
    // masses leave `exp(-0.0464 * 38)` -- about **17%** -- of a surface's red, while its blue is
    // gone to four decimal places. That asymmetry is exactly why a distant mountain goes blue-grey
    // rather than sky-coloured, and why aerial perspective is classically described as losing
    // contrast and shifting blue rather than as fading to the sky.
    const float horizonMass = AerialPerspectivePass::airMassForDistance(
        Vector3(0.0f, 0.0f, -1.0f), 1e9f, kScaleHeight);
    const Vector3 survives = AerialPerspectivePass::transmittance(1.0f, horizonMass);

    std::printf("    at %.1f air masses, transmittance is (%.4f, %.4f, %.4f)\n",
                horizonMass, survives.X, survives.Y, survives.Z);

    EXPECT_GT(survives.X, 0.1f) << "red should still be arriving from the horizon";
    EXPECT_LT(survives.Y, 0.05f);
    EXPECT_LT(survives.Z, 0.001f);
}

TEST(AerialPerspectiveTest, HazierAirTakesMoreOfADistantSurface)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 255);
    auto depth = MakeFlatDepth(gd, 20000.0f);
    RenderTarget2D destination(gd, kSize, kSize);

    AerialPerspectivePass pass(gd);
    if (!pass.isSupported(gd)) GTEST_SKIP() << "this renderer cannot run the pass";
    pass.setSunDirection(Vector3(0.0f, -0.4f, -1.0f));
    pass.setScaleHeight(kScaleHeight);

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);

    pass.setTurbidity(1.0f);
    pass.apply(context);
    const int clear = CentrePixel(destination).getRProperty();

    pass.setTurbidity(8.0f);
    pass.apply(context);
    const int hazy = CentrePixel(destination).getRProperty();

    std::printf("    white at 20 km: turbidity 1 gives %d, turbidity 8 gives %d\n", clear, hazy);
    EXPECT_LT(hazy, clear) << "aerosol took nothing from the surface";
}

TEST(AerialPerspectiveTest, TheScaleHeightIsWhereAGamesWorldScaleEnters)
{
    // A game whose visible world is a few hundred units gets no atmosphere at all from the real
    // scale height, and the fix is this one number rather than a hidden multiplier.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto scene = MakeFlatScene(gd, 200);
    auto depth = MakeFlatDepth(gd, 500.0f);
    RenderTarget2D destination(gd, kSize, kSize);

    AerialPerspectivePass pass(gd);
    if (!pass.isSupported(gd)) GTEST_SKIP() << "this renderer cannot run the pass";
    pass.setSunDirection(Vector3(0.0f, -0.4f, -1.0f));

    PostProcessContext context = MakeContext(scene.get(), depth.get(), &destination);

    pass.setScaleHeight(8400.0f);
    pass.apply(context);
    const int metres = CentrePixel(destination).getRProperty();

    pass.setScaleHeight(200.0f);
    pass.apply(context);
    const int compressed = CentrePixel(destination).getRProperty();

    std::printf("    grey 200 at 500 units: scale height 8400 gives %d, 200 gives %d\n",
                metres, compressed);
    EXPECT_NEAR(metres, 200, 4) << "the real scale height did something at 500 metres";
    EXPECT_GT(std::abs(compressed - metres), 8)
        << "shrinking the scale height did not bring the atmosphere into range";
}

} // namespace

#endif // CNA_CNAEXT
