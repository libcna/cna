// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2052: participating media that receives shadow.
//
// The claim that separates this from `HeightFogPass` is not "there is more fog" -- it is that the
// fog knows where the light is *blocked*. So the test that matters puts a shadow map in and takes
// it out again, changing nothing else.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/VolumetricFogPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::VolumetricFogPass;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kSize     = 64;
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

/// Everything at a constant, fairly distant depth, so every pixel's march covers the same volume.
std::unique_ptr<RenderTarget2D> MakeFlatDepth(GraphicsDevice& gd, const float world)
{
    const int value = static_cast<int>((world / kFarPlane) * 255.0f + 0.5f);
    return MakeImage(gd, [value](int, int) { return Color(value, value, value, 255); });
}

double MeanBrightness(const std::vector<Color>& pixels)
{
    double sum = 0.0;
    for (const Color& p : pixels) sum += p.getRProperty();
    return sum / static_cast<double>(pixels.size());
}

PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination)
{
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 1.0f, kFarPlane);
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 2.0f, 20.0f), Vector3(0.0f, 2.0f, 0.0f),
                                             Vector3::Up);
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

std::array<VertexPositionNormalTexture, 6> Slab()
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    const auto vertex = [&](const float x, const float z) {
        return VertexPositionNormalTexture(Vector3(x, 8.0f, z), up, Vector2(0.0f, 0.0f));
    };
    return {vertex(-10.0f, -10.0f), vertex(10.0f, -10.0f), vertex(10.0f, 10.0f),
            vertex(-10.0f, -10.0f), vertex(10.0f, 10.0f),  vertex(-10.0f, 10.0f)};
}

/// A lid above the scene, so the sun overhead reaches nothing below it.
void FillShadowMap(GraphicsDevice& gd, ShadowMap& shadowMap, const Vector3& sunDirection)
{
    CNA::Graphics::DirectionalLightEXT sun;
    sun.Direction = sunDirection;
    const auto slab = Slab();
    gd.setRasterizerStateProperty(RasterizerState::CullNone);
    gd.setDepthStencilStateProperty(DepthStencilState::Default);
    gd.setBlendStateProperty(BlendState::Opaque);
    gd.SetVertexBuffer(nullptr);
    shadowMap.begin(sun, BoundingBox(Vector3(-12.0f, -1.0f, -12.0f), Vector3(12.0f, 10.0f, 12.0f)));
    gd.DrawUserPrimitives(PrimitiveType::TriangleList, slab.data(), 0, 2);
    shadowMap.end();
}

TEST(VolumetricFogTest, TheMediumScattersLightIntoTheFrame)
{
    GraphicsDevice gd;
    VolumetricFogPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    // A black scene, so everything the frame comes back with is light the medium put there. Against
    // a lit scene this would be ambiguous: a dense medium extinguishes the source faster than it
    // scatters into it, so "brighter than before" is the wrong question to ask of fog.
    auto depth  = MakeFlatDepth(gd, 40.0f);
    auto source = MakeImage(gd, [](int, int) { return Color(0, 0, 0, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setLight(nullptr, Vector3(0.0f, -1.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    pass.setDensity(0.08f);
    pass.setRange(60.0f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    EXPECT_GT(MeanBrightness(ReadTarget(destination)), 4.0)
        << "the medium scattered no light into a black frame at all";
}

TEST(VolumetricFogTest, AShadowMapDarkensTheMediumItBlocks)
{
    // The whole point of the pass. Same scene, same density, same light -- the only change is
    // whether the medium is told what stands between it and the sun. Without the map the fog is
    // lit everywhere the light points, which is a haze; with it, the lid's shadow is in the air.
    GraphicsDevice gd;
    VolumetricFogPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    ShadowMap shadowMap(gd, ShadowQuality::Low);
    if (!shadowMap.isSupported())
        GTEST_SKIP() << "this renderer cannot generate a shadow map";
    const Vector3 sun(0.0f, -1.0f, 0.0f);
    FillShadowMap(gd, shadowMap, sun);

    auto depth  = MakeFlatDepth(gd, 40.0f);
    auto source = MakeImage(gd, [](int, int) { return Color(0, 0, 0, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setDensity(0.08f);
    pass.setRange(60.0f);

    const auto brightnessWith = [&](ShadowMap* map) {
        pass.setLight(map, sun, Vector3(1.0f, 1.0f, 1.0f));
        PostProcessContext context = MakeContext(*source, destination);
        context.sourceDepth = depth.get();
        pass.apply(context);
        return MeanBrightness(ReadTarget(destination));
    };

    const double unshadowed = brightnessWith(nullptr);
    const double shadowed   = brightnessWith(&shadowMap);

    ASSERT_GT(unshadowed, 4.0) << "the unshadowed medium scattered nothing, so nothing was compared";
    EXPECT_LT(shadowed, unshadowed)
        << "the shadow map changed nothing in the air: " << shadowed << " against " << unshadowed;
}

TEST(VolumetricFogTest, LookingTowardsTheLightScattersMoreThanLookingAway)
{
    // Forward-biased scattering, and the one cue that separates airlight from a grey wash. Asserted
    // through the anisotropy setting rather than by moving the camera, so nothing else changes.
    GraphicsDevice gd;
    VolumetricFogPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeFlatDepth(gd, 40.0f);
    auto source = MakeImage(gd, [](int, int) { return Color(0, 0, 0, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setDensity(0.08f);
    pass.setRange(60.0f);
    // The light travels towards the camera, so the view ray looks into it.
    pass.setLight(nullptr, Vector3(0.0f, 0.0f, 1.0f), Vector3(1.0f, 1.0f, 1.0f));

    const auto brightnessAt = [&](const float anisotropy) {
        pass.setAnisotropy(anisotropy);
        PostProcessContext context = MakeContext(*source, destination);
        context.sourceDepth = depth.get();
        pass.apply(context);
        return MeanBrightness(ReadTarget(destination));
    };

    const double isotropic = brightnessAt(0.0f);
    const double forward   = brightnessAt(0.9f);
    EXPECT_GT(forward, isotropic)
        << "forward scattering did not brighten a view into the light: " << forward
        << " against " << isotropic;
}

TEST(VolumetricFogTest, ZeroDensityLeavesTheFrameAlone)
{
    GraphicsDevice gd;
    VolumetricFogPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeFlatDepth(gd, 40.0f);
    auto source = MakeImage(gd, [](int, int) { return Color(90, 90, 90, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    for (const Color& pixel : ReadTarget(destination))
        ASSERT_NEAR(pixel.getRProperty(), 90, 3) << "a disabled pass still changed the frame";
}

TEST(VolumetricFogTest, WithoutDepthOrACameraTheFrameIsPassedThrough)
{
    GraphicsDevice gd;
    VolumetricFogPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(90, 40, 20, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setDensity(0.2f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = nullptr;
    EXPECT_NO_THROW(pass.apply(context));
    EXPECT_NEAR(ReadTarget(destination)[0].getRProperty(), 90, 4);
}

TEST(VolumetricFogTest, TheSettingsAreClampedAndTheNameIsStable)
{
    GraphicsDevice gd;
    VolumetricFogPass pass(gd);
    EXPECT_EQ(pass.getName(), "VolumetricFog");
    EXPECT_FLOAT_EQ(pass.getDensity(), 0.0f) << "the effect must be off by default";

    pass.setDensity(0.3f);
    pass.setRange(25.0f);
    pass.setAnisotropy(0.4f);
    EXPECT_FLOAT_EQ(pass.getDensity(), 0.3f);
    EXPECT_FLOAT_EQ(pass.getRange(), 25.0f);
    EXPECT_FLOAT_EQ(pass.getAnisotropy(), 0.4f);

    pass.setDensity(-1.0f);
    pass.setRange(0.0f);
    EXPECT_FLOAT_EQ(pass.getDensity(), 0.3f);
    EXPECT_FLOAT_EQ(pass.getRange(), 25.0f);

    // The phase function divides by zero at exactly +/-1, so the ends are excluded rather than
    // allowed and guarded downstream.
    pass.setAnisotropy(1.0f);
    EXPECT_FLOAT_EQ(pass.getAnisotropy(), 0.95f);
    pass.setAnisotropy(-1.0f);
    EXPECT_FLOAT_EQ(pass.getAnisotropy(), -0.95f);
}

} // namespace

#endif // CNA_CNAEXT
