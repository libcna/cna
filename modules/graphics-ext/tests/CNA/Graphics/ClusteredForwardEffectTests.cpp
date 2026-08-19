// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2045: shading a surface with every light its cluster holds.
//
// The claim is that a fragment finds its own cluster and walks the list -- so the tests that matter
// are the ones a wrong cluster would fail: a light placed where the geometry is has to light it, a
// light placed somewhere else must not, and the amount has to match the same arithmetic run on the
// CPU rather than merely be "brighter than before".

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/AreaLightBrdfTable.hpp"
#include "CNA/Graphics/AreaLightShading.hpp"
#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::AreaLightBrdfTable;
using CNA::Graphics::AreaLightShading;
using CNA::Graphics::ClusteredForwardEffect;
using CNA::Graphics::ClusteredLightAssignment;
using CNA::Graphics::ClusteredLightBuffer;
using CNA::Graphics::ClusteredLightEXT;
using CNA::Graphics::ClusteredLightGrid;
using CNA::Graphics::ClusteredLightSetEXT;
using CNA::Graphics::ClusteredLightType;
using CNA::Graphics::PbrMaterialExtensions;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AreaLightEXT;
using Microsoft::Xna::Framework::Graphics::AreaLightShapeEXT;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kSize = 64;
constexpr float kNear = 1.0f;
constexpr float kFar  = 100.0f;
constexpr float kWallZ = -12.0f;
constexpr float kHalf  = 10.0f;

Matrix View()
{
    return Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f), Vector3::Up);
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(1.0471975512f, 1.0f, kNear, kFar);
}

/// A wall across the whole view, facing the camera, so every pixel is a shaded surface point.
std::array<VertexPositionNormalTexture, 6> Wall()
{
    const Vector3 facing(0.0f, 0.0f, 1.0f);
    const auto vertex = [&](const float x, const float y) {
        return VertexPositionNormalTexture(Vector3(x, y, kWallZ), facing, Vector2(0.0f, 0.0f));
    };
    return {vertex(-kHalf, -kHalf), vertex(kHalf, -kHalf), vertex(kHalf, kHalf),
            vertex(-kHalf, -kHalf), vertex(kHalf, kHalf),  vertex(-kHalf, kHalf)};
}

ClusteredLightGrid MakeGrid()
{
    ClusteredLightGrid grid;
    grid.setProjection(Projection(), kNear, kFar);
    return grid;
}

ClusteredLightEXT MakePoint(const Vector3& position, const float range, const float intensity)
{
    ClusteredLightEXT light;
    light.Type = ClusteredLightType::Point;
    light.Position = position;
    light.Range = range;
    light.Intensity = intensity;
    return light;
}

std::vector<Color> RenderWall(GraphicsDevice& gd, ClusteredForwardEffect& effect,
                              const ClusteredLightSetEXT& lights)
{
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    assignment.assign(grid, View(), lights.collectBounds());

    ClusteredLightBuffer buffer(gd);
    buffer.upload(lights, grid, assignment);

    RenderTarget2D target(gd, kSize, kSize);
    gd.SetRenderTarget(&target);
    gd.Clear(Color::Black);

    gd.setRasterizerStateProperty(RasterizerState::CullNone);
    gd.setDepthStencilStateProperty(DepthStencilState::Default);
    gd.setBlendStateProperty(BlendState::Opaque);
    gd.SetVertexBuffer(nullptr);

    effect.begin(Matrix::getIdentityProperty(), View(), Projection(), Vector3::Zero, buffer);
    effect.getEffect()->Apply();
    const auto wall = Wall();
    gd.DrawUserPrimitives(PrimitiveType::TriangleList, wall.data(), 0, 2);

    gd.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

long long TotalBrightness(const std::vector<Color>& pixels)
{
    long long total = 0;
    for (const Color& p : pixels)
        total += static_cast<int>(p.getRProperty()) + static_cast<int>(p.getGProperty()) +
                 static_cast<int>(p.getBProperty());
    return total;
}

// ── The CPU model ────────────────────────────────────────────────────────────

TEST(ClusteredForwardEffectTest, ALightBeyondItsRangeContributesExactlyNothing)
{
    // The falloff is windowed rather than asymptotic, and it has to reach exactly zero at the
    // range: the light was sorted into clusters by a sphere of that radius, so anything it still
    // contributed outside would be light no cluster knows about.
    // The light is above the surface and the surface faces up at it, so the only thing separating
    // the samples is distance. (Placing the light on the far side of the normal instead would have
    // measured the facing term and called it the range -- which is how the first draft of this
    // test managed to read zero on both sides.)
    const ClusteredLightEXT light = MakePoint(Vector3(0.0f, 0.0f, 10.0f), 5.0f, 10.0f);
    const Vector3 normal(0.0f, 0.0f, 1.0f);
    const Vector3 eye(0.0f, 0.0f, 30.0f);
    const auto at = [&](const float distance) {
        return ClusteredForwardEffect::contribution(light, Vector3(0.0f, 0.0f, 10.0f - distance),
                                                    normal, eye, Vector3(0.8f, 0.8f, 0.8f), 0.0f,
                                                    0.5f);
    };

    EXPECT_GT(at(1.0f).X, 0.0f);
    EXPECT_GT(at(4.0f).X, 0.0f);
    EXPECT_GT(at(1.0f).X, at(4.0f).X) << "the falloff went the wrong way";

    for (const float distance : {5.0f, 5.001f, 50.0f})
        EXPECT_FLOAT_EQ(at(distance).X, 0.0f) << "at distance " << distance;
}

TEST(ClusteredForwardEffectTest, TheContributionFallsOffAndFacesAway)
{
    const ClusteredLightEXT light = MakePoint(Vector3(0.0f, 0.0f, 10.0f), 40.0f, 5.0f);
    const Vector3 towards(0.0f, 0.0f, 1.0f);
    const Vector3 away(0.0f, 0.0f, -1.0f);
    const Vector3 eye(0.0f, 0.0f, 20.0f);
    const Vector3 base(0.8f, 0.8f, 0.8f);

    const Vector3 near = ClusteredForwardEffect::contribution(light, Vector3(0.0f, 0.0f, 5.0f),
                                                              towards, eye, base, 0.0f, 0.5f);
    const Vector3 far = ClusteredForwardEffect::contribution(light, Vector3(0.0f, 0.0f, -5.0f),
                                                             towards, eye, base, 0.0f, 0.5f);
    EXPECT_GT(near.X, far.X) << "the closer point was not brighter";

    const Vector3 backwards = ClusteredForwardEffect::contribution(
        light, Vector3(0.0f, 0.0f, 5.0f), away, eye, base, 0.0f, 0.5f);
    EXPECT_FLOAT_EQ(backwards.X, 0.0f) << "a surface facing away was lit";
}

TEST(ClusteredForwardEffectTest, ASpotConeCutsOffOutsideItsOuterAngle)
{
    ClusteredLightEXT spot = MakePoint(Vector3(0.0f, 0.0f, 10.0f), 40.0f, 5.0f);
    spot.Type = ClusteredLightType::Spot;
    spot.Direction = Vector3(0.0f, 0.0f, -1.0f);
    spot.InnerAngle = 0.2f;
    spot.OuterAngle = 0.4f;

    const Vector3 normal(0.0f, 0.0f, 1.0f);
    const Vector3 eye(0.0f, 0.0f, 20.0f);
    const Vector3 base(0.8f, 0.8f, 0.8f);

    // All three points sit on the same plane ten units below the light, so the distance term is
    // nearly identical and only the cone factor separates them. Sideways offsets put each at a
    // known angle from the axis: 0, inside the outer angle, and well outside it.
    const float depth = 10.0f;
    const Vector3 onAxis(0.0f, 0.0f, 0.0f);
    const Vector3 inside(depth * std::tan(0.3f), 0.0f, 0.0f);
    const Vector3 outside(depth * std::tan(0.8f), 0.0f, 0.0f);

    const Vector3 axisLit = ClusteredForwardEffect::contribution(spot, onAxis, normal, eye, base,
                                                                 0.0f, 0.5f);
    const Vector3 edgeLit = ClusteredForwardEffect::contribution(spot, inside, normal, eye, base,
                                                                 0.0f, 0.5f);
    const Vector3 beyond  = ClusteredForwardEffect::contribution(spot, outside, normal, eye, base,
                                                                 0.0f, 0.5f);

    EXPECT_GT(axisLit.X, 0.0f) << "the cone's own axis was not lit";
    EXPECT_GT(axisLit.X, edgeLit.X) << "the cone did not soften towards its edge";
    EXPECT_GT(edgeLit.X, 0.0f) << "a point inside the outer angle was cut off";
    EXPECT_FLOAT_EQ(beyond.X, 0.0f) << "a point outside the cone was lit";

    // The same light as a point light lights all three, which is what makes the cone the cause.
    ClusteredLightEXT asPoint = spot;
    asPoint.Type = ClusteredLightType::Point;
    EXPECT_GT(ClusteredForwardEffect::contribution(asPoint, outside, normal, eye, base, 0.0f,
                                                   0.5f).X,
              0.0f);
}

// ── The GPU ──────────────────────────────────────────────────────────────────

TEST(ClusteredForwardEffectTest, AnUnuploadedBufferIsRefused)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    ClusteredLightBuffer buffer(gd);
    EXPECT_THROW(effect.begin(Matrix::getIdentityProperty(), View(), Projection(), Vector3::Zero,
                              buffer),
                 std::runtime_error);
}

TEST(ClusteredForwardEffectTest, TheSettingsRoundTripAndNonsenseIsClamped)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);

    effect.setBaseColor(Vector3(0.2f, 0.4f, 0.6f));
    EXPECT_FLOAT_EQ(effect.getBaseColor().Y, 0.4f);
    effect.setBaseColor(Vector3(-1.0f, 5.0f, 0.5f));
    EXPECT_FLOAT_EQ(effect.getBaseColor().X, 0.0f);
    EXPECT_FLOAT_EQ(effect.getBaseColor().Y, 1.0f);

    effect.setMetallic(0.5f);
    EXPECT_FLOAT_EQ(effect.getMetallic(), 0.5f);
    effect.setMetallic(2.0f);
    EXPECT_FLOAT_EQ(effect.getMetallic(), 1.0f);

    effect.setRoughness(0.3f);
    EXPECT_FLOAT_EQ(effect.getRoughness(), 0.3f);
    effect.setRoughness(0.0f);
    EXPECT_FLOAT_EQ(effect.getRoughness(), 0.04f)
        << "a perfectly smooth surface divides by zero in the specular term";

    effect.setAmbient(Vector3(0.1f, 0.1f, 0.1f));
    EXPECT_FLOAT_EQ(effect.getAmbient().X, 0.1f);
    effect.setAmbient(Vector3(-1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(effect.getAmbient().X, 0.0f);
}

TEST(ClusteredForwardEffectTest, OneLightLightsTheWallWhereItIs)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    ClusteredLightSetEXT lights;
    // Off to one side of the wall, close to it, so the lit region is a patch rather than the frame.
    lights.add(MakePoint(Vector3(-4.0f, 0.0f, kWallZ + 2.0f), 6.0f, 40.0f));

    const std::vector<Color> pixels = RenderWall(gd, effect, lights);

    // The lit patch is on the light's side. Which side of the *image* that is depends on the render
    // target's row order, so both halves are measured and the brighter one only has to be one of
    // them by a wide margin -- what is being asserted is that the light is somewhere, not nowhere.
    long long left = 0, right = 0;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const int value = pixels[static_cast<std::size_t>(y) * kSize + x].getRProperty();
            (x < kSize / 2 ? left : right) += value;
        }
    EXPECT_GT(left, right * 3) << "the lit patch is not on the light's side of the wall";
    EXPECT_GT(TotalBrightness(pixels), 0) << "the wall came back black";
}

TEST(ClusteredForwardEffectTest, ALightNowhereNearTheWallLeavesItDark)
{
    // The counterpart, and the one that would still pass if the shader ignored the cluster list and
    // lit everything: a light behind the camera reaches no cluster the wall occupies.
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    ClusteredLightSetEXT far;
    far.add(MakePoint(Vector3(0.0f, 0.0f, 40.0f), 5.0f, 40.0f));
    const std::vector<Color> dark = RenderWall(gd, effect, far);
    EXPECT_EQ(TotalBrightness(dark), 0) << "a light nowhere near the wall lit it anyway";

    ClusteredLightSetEXT near;
    near.add(MakePoint(Vector3(0.0f, 0.0f, kWallZ + 2.0f), 6.0f, 40.0f));
    EXPECT_GT(TotalBrightness(RenderWall(gd, effect, near)), 0)
        << "the same scene with the light moved onto the wall stayed dark too, so the test proves "
           "nothing";
}

TEST(ClusteredForwardEffectTest, TwoHundredAndFiftySixLightsRender)
{
    // The acceptance criterion for the section. Not a performance claim -- a correctness one: the
    // light set's maximum, the assignment, the upload and the shader's loop all have to agree at
    // the top of their range.
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    ClusteredLightSetEXT lights;
    for (int i = 0; i < ClusteredLightSetEXT::kMaxLights; ++i)
    {
        const float t = static_cast<float>(i);
        lights.add(MakePoint(Vector3(std::sin(t * 0.9f) * 7.0f, std::cos(t * 0.7f) * 7.0f,
                                     kWallZ + 1.0f + std::sin(t * 0.31f)),
                             3.0f, 8.0f));
    }
    ASSERT_EQ(lights.getCount(), 256);

    const std::vector<Color> pixels = RenderWall(gd, effect, lights);
    EXPECT_GT(TotalBrightness(pixels), 0) << "256 lights produced a black frame";

    int lit = 0;
    for (const Color& p : pixels)
        if (p.getRProperty() > 8) ++lit;
    EXPECT_GT(lit, kSize * kSize / 8) << "256 lights lit almost nothing";
}

TEST(ClusteredForwardEffectTest, TheShadedValueMatchesTheCpuModel)
{
    // The strongest claim here: not "there is light" but "this much light". The wall's centre is a
    // known world point with a known normal, one light is placed on the axis, and the pixel is
    // compared against ClusteredForwardEffect::contribution -- the CPU mirror of the same shader.
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    const ClusteredLightEXT light = MakePoint(Vector3(0.0f, 0.0f, kWallZ + 3.0f), 8.0f, 6.0f);
    ClusteredLightSetEXT lights;
    lights.add(light);

    effect.setBaseColor(Vector3(0.8f, 0.8f, 0.8f));
    effect.setMetallic(0.0f);
    effect.setRoughness(0.5f);

    const std::vector<Color> pixels = RenderWall(gd, effect, lights);

    const Vector3 centre(0.0f, 0.0f, kWallZ);
    const Vector3 expected = ClusteredForwardEffect::contribution(
        light, centre, Vector3(0.0f, 0.0f, 1.0f), Vector3::Zero, Vector3(0.8f, 0.8f, 0.8f), 0.0f,
        0.5f);
    ASSERT_GT(expected.X, 0.02f) << "the reference itself is too dark to compare against";
    ASSERT_LT(expected.X, 0.95f) << "the reference saturates, so the comparison would prove little";

    const Color middle = pixels[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];
    const float measured = static_cast<float>(middle.getRProperty()) / 255.0f;
    EXPECT_NEAR(measured, expected.X, 0.03f)
        << "the shader and the CPU model disagree about how bright the centre is";
}


// ── The area light (MOD-2062) ────────────────────────────────────────────────

namespace {

/// A rectangle floating in front of the wall, facing it, so its footprint lands on the wall.
AreaLightEXT WallLight(const float halfWidth)
{
    AreaLightEXT light;
    light.Shape = AreaLightShapeEXT::Rectangle;
    light.Position = Vector3(0.0f, 0.0f, kWallZ + 2.0f);
    light.RightAxis = Vector3(halfWidth, 0.0f, 0.0f);
    light.UpAxis = Vector3(0.0f, halfWidth, 0.0f);
    light.Color = Vector3(1.0f, 1.0f, 1.0f);
    light.Intensity = 1.0f;
    light.Range = 60.0f;
    light.TwoSided = true;
    return light;
}

}  // namespace

TEST(ClusteredForwardEffectTest, AnAreaLightLightsTheWallWithNoPunctualLightsAtAll)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    const ClusteredLightSetEXT none;
    EXPECT_EQ(TotalBrightness(RenderWall(gd, effect, none)), 0)
        << "the wall was not black before the area light was added";

    AreaLightBrdfTable table(gd, 16, 64);
    effect.setBaseColor(Vector3(0.8f, 0.8f, 0.8f));
    effect.setRoughness(0.6f);
    effect.setAreaLight(WallLight(3.0f), table);
    EXPECT_TRUE(effect.hasAreaLight());

    EXPECT_GT(TotalBrightness(RenderWall(gd, effect, none)), 0)
        << "an area light with no punctual lights lit nothing";

    effect.clearAreaLight();
    EXPECT_FALSE(effect.hasAreaLight());
    EXPECT_EQ(TotalBrightness(RenderWall(gd, effect, none)), 0)
        << "clearing the area light left it lighting the wall";
}

TEST(ClusteredForwardEffectTest, AnInvalidAreaLightClearsTheSlotRatherThanBeingStored)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    AreaLightBrdfTable table(gd, 8, 32);

    effect.setAreaLight(WallLight(2.0f), table);
    ASSERT_TRUE(effect.hasAreaLight());

    AreaLightEXT degenerate = WallLight(2.0f);
    degenerate.UpAxis = degenerate.RightAxis;   // parallel axes, no area
    effect.setAreaLight(degenerate, table);
    EXPECT_FALSE(effect.hasAreaLight())
        << "a light the form factor cannot integrate was kept rather than refused";
}

TEST(ClusteredForwardEffectTest, TheAreaLightsShadedValueMatchesTheCpuModel)
{
    // The same claim as for the punctual lights, on the half of the shading that is exact: the
    // wall's centre is a known point, and what the shader puts there has to be the number
    // AreaLightShading::contribution computes for it.
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    const AreaLightEXT light = WallLight(2.5f);
    AreaLightBrdfTable table(gd, 32, 128);

    effect.setBaseColor(Vector3(0.7f, 0.7f, 0.7f));
    effect.setMetallic(0.0f);
    effect.setRoughness(0.6f);
    effect.setAreaLight(light, table);

    const std::vector<Color> pixels = RenderWall(gd, effect, ClusteredLightSetEXT());

    const Vector3 centre(0.0f, 0.0f, kWallZ);
    const Vector3 expected = AreaLightShading::contribution(
        light, centre, Vector3(0.0f, 0.0f, 1.0f), Vector3::Zero, Vector3(0.7f, 0.7f, 0.7f), 0.0f,
        0.6f);
    ASSERT_GT(expected.X, 0.05f) << "the reference is too dark to compare against";
    ASSERT_LT(expected.X, 0.95f) << "the reference saturates, so the comparison proves little";

    const Color middle = pixels[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];
    const float measured = static_cast<float>(middle.getRProperty()) / 255.0f;
    // Looser than the punctual comparison by design: the BRDF table reaching the shader is an
    // 8-bit texture, so the specular term carries a quantisation the CPU reference does not.
    EXPECT_NEAR(measured, expected.X, 0.05f)
        << "the shader and the CPU model disagree about the area light";
}

TEST(ClusteredForwardEffectTest, TheHighlightHasTheLightsShapeOnASmoothSurface)
{
    // The property no punctual light can produce. A small bright rectangle on a smooth wall leaves
    // a bright patch with an edge; the same light on a rough wall leaves a gradient. Measured as
    // the contrast between the frame's centre and its corner.
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    AreaLightBrdfTable table(gd, 32, 128);
    AreaLightEXT light = WallLight(1.5f);
    light.Intensity = 4.0f;

    const auto contrastAt = [&](const float roughness) {
        effect.setBaseColor(Vector3(0.05f, 0.05f, 0.05f));   // dark, so specular dominates
        effect.setMetallic(1.0f);
        effect.setRoughness(roughness);
        effect.setAreaLight(light, table);
        const std::vector<Color> pixels = RenderWall(gd, effect, ClusteredLightSetEXT());
        const int centre = pixels[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2]
                               .getRProperty();
        const int edge = pixels[static_cast<std::size_t>(kSize) * (kSize / 2) + 2].getRProperty();
        return std::make_pair(centre, edge);
    };

    const auto smooth = contrastAt(0.08f);
    const auto rough  = contrastAt(0.9f);

    EXPECT_GT(smooth.first, 20) << "the smooth surface shows no highlight at all";
    EXPECT_GT(smooth.first - smooth.second, rough.first - rough.second)
        << "the rough surface's highlight had at least as much of an edge as the smooth one's";
}

// ── The clearcoat lobe (MOD-2070) ────────────────────────────────────────────

TEST(ClusteredForwardEffectTest, AClearcoatAddsASecondLobeAndTakesFromTheFirst)
{
    // What makes lacquer look like lacquer is not that it is brighter -- it is that it is brighter
    // *where it catches the light* and darker everywhere else, because the coat reflects exactly
    // what it stops reaching the base. Both halves are asserted; a coat that only ever added would
    // pass the first and be wrong.
    const ClusteredLightEXT light = MakePoint(Vector3(0.0f, 3.0f, 0.0f), 20.0f, 4.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 base(0.5f, 0.5f, 0.5f);

    // A rough base under a smooth coat: the configuration one roughness cannot describe.
    const float roughness = 0.9f;
    const float coatRoughness = 0.05f;

    // Straight above, so the half-vector is the normal and the coat's lobe is aimed at the eye.
    const Vector3 onAxis(0.0f, 5.0f, 0.0f);
    const Vector3 bare = ClusteredForwardEffect::contribution(light, surface, normal, onAxis, base,
                                                              0.0f, roughness);
    const Vector3 coated = ClusteredForwardEffect::contribution(light, surface, normal, onAxis,
                                                                base, 0.0f, roughness, 1.0f,
                                                                coatRoughness);
    EXPECT_GT(coated.X, bare.X * 1.5f) << "the coat added no highlight where it should be brightest";

    // Well off the specular direction, where the coat's own lobe contributes almost nothing and
    // all it does is take from the base.
    const Vector3 offAxis(6.0f, 0.6f, 0.0f);
    const Vector3 bareOff = ClusteredForwardEffect::contribution(light, surface, normal, offAxis,
                                                                 base, 0.0f, roughness);
    const Vector3 coatedOff = ClusteredForwardEffect::contribution(light, surface, normal, offAxis,
                                                                   base, 0.0f, roughness, 1.0f,
                                                                   coatRoughness);
    EXPECT_LT(coatedOff.X, bareOff.X) << "the coat reflected without taking anything from the base";
    EXPECT_GT(coatedOff.X, 0.0f) << "the coat swallowed the base entirely";
}

TEST(ClusteredForwardEffectTest, AZeroClearcoatIsExactlyTheUncoatedResult)
{
    // The default has to change nothing at all, bit for bit -- not "close enough".
    const ClusteredLightEXT light = MakePoint(Vector3(1.0f, 3.0f, 2.0f), 20.0f, 3.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 eye(2.0f, 4.0f, 1.0f);
    const Vector3 base(0.6f, 0.4f, 0.2f);

    const Vector3 bare = ClusteredForwardEffect::contribution(light, surface, normal, eye, base,
                                                              0.2f, 0.5f);
    const Vector3 zeroCoat = ClusteredForwardEffect::contribution(light, surface, normal, eye,
                                                                  base, 0.2f, 0.5f, 0.0f, 0.5f);
    EXPECT_FLOAT_EQ(bare.X, zeroCoat.X);
    EXPECT_FLOAT_EQ(bare.Y, zeroCoat.Y);
    EXPECT_FLOAT_EQ(bare.Z, zeroCoat.Z);
}

TEST(ClusteredForwardEffectTest, TheClearcoatSettingsReachTheShader)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    EXPECT_TRUE(effect.getMaterialExtensions().isNeutral())
        << "an effect nobody gave extensions to must shade as if there were none";

    ClusteredLightSetEXT lights;
    lights.add(MakePoint(Vector3(0.0f, 0.0f, kWallZ + 3.0f), 10.0f, 5.0f));

    effect.setBaseColor(Vector3(0.4f, 0.4f, 0.4f));
    effect.setMetallic(0.0f);
    effect.setRoughness(0.9f);

    const long long bare = TotalBrightness(RenderWall(gd, effect, lights));

    PbrMaterialExtensions extensions;
    extensions.setClearcoatFactor(1.0f);
    // Not a mirror coat, on purpose: a roughness of 0.05 gives a lobe about a seventh of a degree
    // wide, and the pixel the comparison reads sits about four degrees off the specular direction,
    // so the reference would be reading a peak the frame cannot contain. 0.35 is a lobe wide
    // enough that half a pixel does not decide the answer.
    extensions.setClearcoatRoughness(0.35f);
    effect.setMaterialExtensions(extensions);
    EXPECT_FALSE(effect.getMaterialExtensions().isNeutral());

    const long long coated = TotalBrightness(RenderWall(gd, effect, lights));
    EXPECT_NE(coated, bare) << "the clearcoat uniforms never reached the shader";

    // And the frame agrees with the CPU model at the point directly under the light, which is
    // where the coat's own lobe is aimed.
    const Vector3 centre(0.0f, 0.0f, kWallZ);
    const Vector3 expected = ClusteredForwardEffect::contribution(
        MakePoint(Vector3(0.0f, 0.0f, kWallZ + 3.0f), 10.0f, 5.0f), centre,
        Vector3(0.0f, 0.0f, 1.0f), Vector3::Zero, Vector3(0.4f, 0.4f, 0.4f), 0.0f, 0.9f, 1.0f,
        0.35f);
    const std::vector<Color> pixels = RenderWall(gd, effect, lights);
    const Color middle = pixels[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];
    ASSERT_LT(expected.X, 0.95f) << "the reference saturates, so the comparison proves little";
    EXPECT_NEAR(static_cast<float>(middle.getRProperty()) / 255.0f, expected.X, 0.03f)
        << "the shader and the CPU model disagree about the clearcoat";
}

// ── The sheen lobe (MOD-2071) ────────────────────────────────────────────────

TEST(ClusteredForwardEffectTest, SheenBrightensAtGrazingAnglesWhereSpecularIsFading)
{
    // The property that makes sheen sheen. A specular lobe peaks where the half-vector is *aligned*
    // with the normal; the Charlie distribution peaks where it is *perpendicular* to it. So the
    // test compares the sheen's share of the result head-on against its share at a grazing view,
    // and the share has to grow -- not merely be present.
    const ClusteredLightEXT light = MakePoint(Vector3(0.0f, 4.0f, 0.0f), 40.0f, 4.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 base(0.3f, 0.3f, 0.3f);

    PbrMaterialExtensions velvet;
    velvet.setSheenColorFactor(Vector3(1.0f, 1.0f, 1.0f));
    velvet.setSheenRoughness(0.4f);

    const auto share = [&](const Vector3& eye) {
        const Vector3 plain = ClusteredForwardEffect::contribution(light, surface, normal, eye,
                                                                   base, 0.0f, 0.6f);
        const Vector3 sheened = ClusteredForwardEffect::contribution(light, surface, normal, eye,
                                                                     base, 0.0f, 0.6f, velvet);
        return (sheened.X - plain.X) / std::max(plain.X, 1e-6f);
    };

    const float headOn = share(Vector3(0.0f, 6.0f, 0.0f));
    const float grazing = share(Vector3(0.0f, 0.35f, 6.0f));
    EXPECT_GT(headOn, 0.0f) << "sheen contributed nothing at all";
    EXPECT_GT(grazing, headOn * 2.0f)
        << "sheen did not grow towards grazing, so it is behaving like an ordinary specular lobe";
}

TEST(ClusteredForwardEffectTest, ABlackSheenIsExactlyTheUnsheenedResult)
{
    const ClusteredLightEXT light = MakePoint(Vector3(1.0f, 3.0f, 2.0f), 20.0f, 3.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 eye(2.0f, 4.0f, 1.0f);
    const Vector3 base(0.6f, 0.4f, 0.2f);

    const PbrMaterialExtensions neutral;
    const Vector3 plain = ClusteredForwardEffect::contribution(light, surface, normal, eye, base,
                                                               0.2f, 0.5f);
    const Vector3 viaExtensions = ClusteredForwardEffect::contribution(light, surface, normal, eye,
                                                                       base, 0.2f, 0.5f, neutral);
    EXPECT_FLOAT_EQ(plain.X, viaExtensions.X);
    EXPECT_FLOAT_EQ(plain.Y, viaExtensions.Y);
    EXPECT_FLOAT_EQ(plain.Z, viaExtensions.Z);
}

TEST(ClusteredForwardEffectTest, SheenKeepsItsOwnColour)
{
    // Sheen has a colour of its own -- a blue rim on a red cushion is a thing velvet does -- so the
    // lobe must not be tinted by the base colour on its way out.
    const ClusteredLightEXT light = MakePoint(Vector3(0.0f, 4.0f, 0.0f), 40.0f, 4.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 eye(0.0f, 0.4f, 6.0f);
    const Vector3 redBase(0.5f, 0.0f, 0.0f);

    PbrMaterialExtensions blueSheen;
    blueSheen.setSheenColorFactor(Vector3(0.0f, 0.0f, 1.0f));
    blueSheen.setSheenRoughness(0.4f);

    const Vector3 plain = ClusteredForwardEffect::contribution(light, surface, normal, eye,
                                                               redBase, 0.0f, 0.6f);
    const Vector3 sheened = ClusteredForwardEffect::contribution(light, surface, normal, eye,
                                                                 redBase, 0.0f, 0.6f, blueSheen);
    EXPECT_GT(sheened.Z, plain.Z + 1e-4f) << "the blue sheen did not reach the blue channel";
    EXPECT_NEAR(sheened.X, plain.X, 1e-5f) << "the blue sheen leaked into the red channel";
}

TEST(ClusteredForwardEffectTest, TheSheenSettingsReachTheShader)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    ClusteredLightSetEXT lights;
    lights.add(MakePoint(Vector3(0.0f, 0.0f, kWallZ + 3.0f), 12.0f, 4.0f));

    effect.setBaseColor(Vector3(0.3f, 0.3f, 0.3f));
    effect.setMetallic(0.0f);
    effect.setRoughness(0.6f);
    const long long plain = TotalBrightness(RenderWall(gd, effect, lights));

    PbrMaterialExtensions velvet;
    velvet.setSheenColorFactor(Vector3(1.0f, 1.0f, 1.0f));
    velvet.setSheenRoughness(0.4f);
    effect.setMaterialExtensions(velvet);
    const long long sheened = TotalBrightness(RenderWall(gd, effect, lights));
    EXPECT_GT(sheened, plain) << "the sheen uniforms never reached the shader";

    // And the value at the wall's centre matches the CPU model, as the other lobes do.
    const Vector3 centre(0.0f, 0.0f, kWallZ);
    const Vector3 expected = ClusteredForwardEffect::contribution(
        MakePoint(Vector3(0.0f, 0.0f, kWallZ + 3.0f), 12.0f, 4.0f), centre,
        Vector3(0.0f, 0.0f, 1.0f), Vector3::Zero, Vector3(0.3f, 0.3f, 0.3f), 0.0f, 0.6f, velvet);
    const std::vector<Color> pixels = RenderWall(gd, effect, lights);
    const Color middle = pixels[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];
    ASSERT_LT(expected.X, 0.95f) << "the reference saturates, so the comparison proves little";
    EXPECT_NEAR(static_cast<float>(middle.getRProperty()) / 255.0f, expected.X, 0.03f)
        << "the shader and the CPU model disagree about the sheen";
}

// ── Transmission and volume (MOD-2072) ───────────────────────────────────────

TEST(ClusteredForwardEffectTest, TheVolumeAbsorbsByBeersLaw)
{
    // The defining property of the attenuation distance: after travelling exactly that far, white
    // light has become the attenuation colour. Nothing else about the model is worth asserting if
    // that one number is wrong.
    const Vector3 amber(0.9f, 0.5f, 0.2f);
    const Vector3 atOneDistance = ClusteredForwardEffect::volumeAttenuation(amber, 2.0f, 2.0f);
    EXPECT_NEAR(atOneDistance.X, amber.X, 1e-4f);
    EXPECT_NEAR(atOneDistance.Y, amber.Y, 1e-4f);
    EXPECT_NEAR(atOneDistance.Z, amber.Z, 1e-4f);

    // And twice as far absorbs twice as much, in the exponent -- so the survival squares.
    const Vector3 atTwo = ClusteredForwardEffect::volumeAttenuation(amber, 2.0f, 4.0f);
    EXPECT_NEAR(atTwo.X, amber.X * amber.X, 1e-4f);
    EXPECT_NEAR(atTwo.Z, amber.Z * amber.Z, 1e-4f);

    // A volume with no thickness, or no attenuation distance, absorbs nothing at all.
    for (const Vector3& clear : {ClusteredForwardEffect::volumeAttenuation(amber, 2.0f, 0.0f),
                                 ClusteredForwardEffect::volumeAttenuation(amber, 0.0f, 5.0f),
                                 ClusteredForwardEffect::volumeAttenuation(amber, -1.0f, 5.0f)})
    {
        EXPECT_FLOAT_EQ(clear.X, 1.0f);
        EXPECT_FLOAT_EQ(clear.Y, 1.0f);
        EXPECT_FLOAT_EQ(clear.Z, 1.0f);
    }
}

TEST(ClusteredForwardEffectTest, ATransmissiveMaterialWithoutAnOpaqueFrameIsRefused)
{
    // Refused, not approximated. Without the copy the surface would come back opaque, which is not
    // a slightly wrong glass -- it is the absence of one, and it would look like the extension was
    // never implemented.
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);

    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    const ClusteredLightSetEXT lights;
    assignment.assign(grid, View(), lights.collectBounds());
    ClusteredLightBuffer buffer(gd);
    buffer.upload(lights, grid, assignment);

    PbrMaterialExtensions glass;
    glass.setTransmissionFactor(1.0f);
    effect.setMaterialExtensions(glass);
    EXPECT_THROW(effect.begin(Matrix::getIdentityProperty(), View(), Projection(), Vector3::Zero,
                              buffer),
                 std::runtime_error);

    // The same material with a frame to refract against is accepted.
    Texture2D frame(gd, 2, 2);
    const std::vector<Color> pixels(4, Color(0, 0, 255, 255));
    frame.SetData(pixels.data(), 4);
    effect.setOpaqueFrame(&frame);
    EXPECT_NO_THROW(effect.begin(Matrix::getIdentityProperty(), View(), Projection(),
                                 Vector3::Zero, buffer));

    // And withdrawing the frame refuses it again, rather than remembering the old one.
    effect.setOpaqueFrame(nullptr);
    EXPECT_THROW(effect.begin(Matrix::getIdentityProperty(), View(), Projection(), Vector3::Zero,
                              buffer),
                 std::runtime_error);
}

TEST(ClusteredForwardEffectTest, TheIndexOfRefractionRefusesToGoBelowAVacuum)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    EXPECT_FLOAT_EQ(effect.getIor(), 1.5f) << "glass is the default";
    effect.setIor(1.33f);
    EXPECT_FLOAT_EQ(effect.getIor(), 1.33f);
    effect.setIor(0.5f);
    EXPECT_FLOAT_EQ(effect.getIor(), 1.33f) << "below 1 a surface would refract the wrong way";
}

TEST(ClusteredForwardEffectTest, ATransmissiveWallShowsWhatIsBehindIt)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    // A frame with nothing in it but blue, so anything blue in the result came through the wall.
    Texture2D behind(gd, 4, 4);
    const std::vector<Color> blue(16, Color(0, 0, 220, 255));
    behind.SetData(blue.data(), 16);

    ClusteredLightSetEXT lights;
    lights.add(MakePoint(Vector3(0.0f, 0.0f, kWallZ + 3.0f), 12.0f, 3.0f));
    effect.setBaseColor(Vector3(0.6f, 0.0f, 0.0f));    // red, so the two are told apart
    effect.setMetallic(0.0f);
    effect.setRoughness(0.6f);
    effect.setOpaqueFrame(&behind);

    const std::vector<Color> opaque = RenderWall(gd, effect, lights);
    const Color opaqueMiddle = opaque[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];
    EXPECT_LT(static_cast<int>(opaqueMiddle.getBProperty()), 10)
        << "the wall was already blue before anything was transmitted through it";

    PbrMaterialExtensions glass;
    glass.setTransmissionFactor(1.0f);
    effect.setMaterialExtensions(glass);

    const std::vector<Color> clear = RenderWall(gd, effect, lights);
    const Color clearMiddle = clear[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];
    EXPECT_GT(static_cast<int>(clearMiddle.getBProperty()), 150)
        << "the frame behind the wall did not come through it";
    EXPECT_LT(static_cast<int>(clearMiddle.getRProperty()),
              static_cast<int>(opaqueMiddle.getRProperty()))
        << "the surface's own diffuse colour survived being transmitted through";
}

TEST(ClusteredForwardEffectTest, AThickVolumeAbsorbsWhatPassesThroughIt)
{
    GraphicsDevice gd;
    ClusteredForwardEffect effect(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!effect.isSupported()) GTEST_SKIP() << "this renderer cannot run the clustered effect";

    Texture2D behind(gd, 4, 4);
    const std::vector<Color> white(16, Color(255, 255, 255, 255));
    behind.SetData(white.data(), 16);

    ClusteredLightSetEXT lights;
    effect.setBaseColor(Vector3(0.0f, 0.0f, 0.0f));
    effect.setOpaqueFrame(&behind);

    PbrMaterialExtensions thin;
    thin.setTransmissionFactor(1.0f);
    effect.setMaterialExtensions(thin);
    const Color unabsorbed =
        RenderWall(gd, effect, lights)[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];

    PbrMaterialExtensions thick = thin;
    thick.setThicknessFactor(2.0f);
    thick.setAttenuationDistance(1.0f);
    thick.setAttenuationColor(Vector3(0.9f, 0.2f, 0.2f));   // absorbs green and blue hard
    effect.setMaterialExtensions(thick);
    const Color absorbed =
        RenderWall(gd, effect, lights)[static_cast<std::size_t>(kSize) * (kSize / 2) + kSize / 2];

    EXPECT_GT(static_cast<int>(unabsorbed.getGProperty()), 200)
        << "white did not come through a volume that absorbs nothing";
    EXPECT_LT(static_cast<int>(absorbed.getGProperty()),
              static_cast<int>(unabsorbed.getGProperty()) / 2)
        << "the volume did not absorb the green it was told to";
    EXPECT_GT(static_cast<int>(absorbed.getRProperty()),
              static_cast<int>(absorbed.getGProperty()) * 2)
        << "the volume absorbed every channel equally, so its colour did nothing";
}

} // namespace

#endif // CNA_CNAEXT
