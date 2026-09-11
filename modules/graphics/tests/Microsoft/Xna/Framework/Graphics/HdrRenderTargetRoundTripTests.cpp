// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-131: the end-to-end proof that a float render target keeps values above 1.0.
//
// Everything else in Phase 1 is a promise about this one property -- the capability query, the
// per-format verdict, the GL storage, the readback. A test that only checked those parts
// individually could pass while the values were still clamped somewhere in between, which is
// exactly the failure the whole phase exists to remove. So this renders into the target and reads
// the numbers back.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <array>
#include <vector>

#include "CNA/GraphicsCapability.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::ClearOptions;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfSingle;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector2;

namespace {

constexpr int kSize = 4;

/// The scene-referred colour under test: brighter than white in two channels, which is the entire
/// point of rendering to a float target and the entire thing an 8-bit one cannot represent.
constexpr float kRed   = 4.0f;
constexpr float kGreen = 2.0f;
constexpr float kBlue  = 1.0f;

/// Whether this renderer can read a render target's pixels back at all.
///
/// The float tests above are gated by `SupportsSurfaceFormatAsRenderTargetEXT`, which happens to
/// exclude the renderers that cannot read back either. The 8-bit control below is not, so it needs
/// the question asked directly: a renderer with ordinary colour targets and no readback path --
/// Headless is one -- would otherwise fail the control while supporting everything it claims to.
[[nodiscard]] bool CanReadRenderTargetsBack(GraphicsDevice& device)
{
    try
    {
        RenderTarget2D probe(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None);
        device.SetRenderTarget(&probe);
        device.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        device.SetRenderTarget(nullptr);
        Color pixel = Color::White;
        probe.GetData(&pixel, 1);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

TEST(HdrRenderTargetRoundTripTest, AFloatTargetKeepsValuesAboveOne)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<Vector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const Vector4& texel : pixels)
    {
        EXPECT_FLOAT_EQ(texel.X, kRed);
        EXPECT_FLOAT_EQ(texel.Y, kGreen);
        EXPECT_FLOAT_EQ(texel.Z, kBlue);
        EXPECT_FLOAT_EQ(texel.W, 1.0f);
    }
}

TEST(HdrRenderTargetRoundTripTest, ClassicVectorClearQuantizesThroughColor)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D target(gd, 2, 2, false, SurfaceFormat::Vector4, DepthFormat::None);
    const Vector4 clearValue(-2.0f, 3.0f, 0.5f, 0.25f);
    const Color packedValue(clearValue);
    ASSERT_EQ(packedValue, Color(0, 255, 128, 64));
    const Vector4 expected = packedValue.ToVector4();

    gd.SetRenderTarget(&target);
    gd.Clear(ClearOptions::Target, clearValue, 1.0f, 0);
    gd.SetRenderTarget(nullptr);

    std::array<Vector4, 4> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (const Vector4& texel : pixels)
    {
        EXPECT_FLOAT_EQ(texel.X, expected.X);
        EXPECT_FLOAT_EQ(texel.Y, expected.Y);
        EXPECT_FLOAT_EQ(texel.Z, expected.Z);
        EXPECT_FLOAT_EQ(texel.W, expected.W);
    }
}

TEST(HdrRenderTargetRoundTripTest, AColourTargetClampsTheSameRender)
{
    // The control. Without it, a passing test above could mean "float targets work" or merely
    // "this driver never clamped anything anyway"; this shows the two formats genuinely differ,
    // and documents what an 8-bit target does with the same draw.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!CanReadRenderTargetsBack(gd))
        GTEST_SKIP() << "this renderer cannot read a render target back to the CPU";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    // Color has no default constructor (XNA's own struct has no parameterless one either), so the
    // buffer is filled with a value that would survive the readback unnoticed if it never ran.
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const Color& texel : pixels)
    {
        EXPECT_EQ(texel.getRProperty(), 255);
        EXPECT_EQ(texel.getGProperty(), 255);
        EXPECT_EQ(texel.getBProperty(), 255);
        EXPECT_EQ(texel.getAProperty(), 255);
    }
}

TEST(HdrRenderTargetRoundTripTest, AHalfFloatTargetKeepsValuesAboveOne)
{
    // HdrBlendable is the format the CNAEXT engine layer's HDR scene target actually allocates, so
    // it gets its own round trip rather than riding on the RGBA32F case. Its readback element is
    // HalfVector4 -- the shared layer pairs each float format with the element type that matches
    // its storage exactly, and refuses a mismatched one rather than widening behind the caller.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<HalfVector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const HalfVector4& packed : pixels)
    {
        const Vector4 texel = packed.ToVector4();
        // Half precision: 4.0, 2.0 and 1.0 are all exactly representable, so this is not a
        // tolerance dodge -- an off-by-a-little result here would mean a real conversion bug.
        EXPECT_FLOAT_EQ(texel.X, kRed);
        EXPECT_FLOAT_EQ(texel.Y, kGreen);
        EXPECT_FLOAT_EQ(texel.Z, kBlue);
        EXPECT_FLOAT_EQ(texel.W, 1.0f);
    }
}

TEST(HdrRenderTargetRoundTripTest, EveryClassicFloatLayoutKeepsItsDeclaredChannels)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4) ||
        !gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver lacks the classic float target matrix";

    RenderTarget2D single(gd, 1, 1, false, SurfaceFormat::Single, DepthFormat::None);
    RenderTarget2D vector2(gd, 1, 1, false, SurfaceFormat::Vector2, DepthFormat::None);
    RenderTarget2D halfSingle(gd, 1, 1, false, SurfaceFormat::HalfSingle, DepthFormat::None);
    RenderTarget2D halfVector2(gd, 1, 1, false, SurfaceFormat::HalfVector2, DepthFormat::None);
    RenderTarget2D halfVector4(gd, 1, 1, false, SurfaceFormat::HalfVector4, DepthFormat::None);

    for (RenderTarget2D* target : {&single, &vector2, &halfSingle, &halfVector2, &halfVector4})
    {
        gd.SetRenderTarget(target);
        gd.Clear(-2.0f, 3.0f, 0.5f, 0.25f);
    }
    gd.SetRenderTarget(nullptr);

    float r = 0.0f;
    single.GetData(&r, 1);
    EXPECT_FLOAT_EQ(r, -2.0f);

    Vector2 rg;
    vector2.GetData(&rg, 1);
    EXPECT_FLOAT_EQ(rg.X, -2.0f);
    EXPECT_FLOAT_EQ(rg.Y, 3.0f);

    HalfSingle halfR;
    halfSingle.GetData(&halfR, 1);
    EXPECT_FLOAT_EQ(halfR.ToSingle(), -2.0f);

    HalfVector2 halfRg;
    halfVector2.GetData(&halfRg, 1);
    EXPECT_FLOAT_EQ(halfRg.ToVector2().X, -2.0f);
    EXPECT_FLOAT_EQ(halfRg.ToVector2().Y, 3.0f);

    HalfVector4 halfRgba;
    halfVector4.GetData(&halfRgba, 1);
    EXPECT_FLOAT_EQ(halfRgba.ToVector4().X, -2.0f);
    EXPECT_FLOAT_EQ(halfRgba.ToVector4().Y, 3.0f);
    EXPECT_FLOAT_EQ(halfRgba.ToVector4().Z, 0.5f);
    EXPECT_FLOAT_EQ(halfRgba.ToVector4().W, 0.25f);
}

TEST(HdrRenderTargetRoundTripTest, HdrBlendableRasterAndAdditiveBlendRemainInTheFloatDomain)
{
    GraphicsDevice gd(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                      PresentationParameters());
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no blendable RGBA16F render targets";

    Texture2D source(gd, 1, 1, false, SurfaceFormat::HdrBlendable);
    const HalfVector4 sourceTexel(0.75f, 0.25f, -0.5f, 1.0f);
    source.SetData(&sourceTexel, 1);
    RenderTarget2D target(
        gd, kSize, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);
    SpriteBatch batch(gd);
    SamplerState point = SamplerState::PointClamp;

    gd.SetRenderTarget(&target);
    gd.Clear(0.75f, 0.25f, 2.0f, 1.0f);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, nullptr);
    batch.Draw(source, Microsoft::Xna::Framework::Rectangle(0, 0, kSize, kSize), Color::White);
    batch.End();
    gd.SetRenderTarget(nullptr);

    std::vector<HalfVector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Vector4 centre =
        pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2].ToVector4();
    EXPECT_FLOAT_EQ(centre.X, 1.5f);
    EXPECT_FLOAT_EQ(centre.Y, 0.5f);
    EXPECT_FLOAT_EQ(centre.Z, 1.5f);
    EXPECT_FLOAT_EQ(centre.W, 2.0f);
}

TEST(HdrRenderTargetRoundTripTest, AFloatTargetSamplesWithoutAnRgba8Intermediate)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D source(gd, 2, 2, false, SurfaceFormat::Vector4, DepthFormat::None);
    RenderTarget2D destination(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
    gd.SetRenderTarget(&source);
    gd.Clear(2.0f, 0.5f, -1.0f, 1.0f);
    gd.SetRenderTarget(&destination);
    gd.Clear(Color::Black);

    SpriteBatch batch(gd);
    SamplerState point = SamplerState::PointClamp;
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
    batch.Draw(source, Microsoft::Xna::Framework::Rectangle(0, 0, kSize, kSize),
               Color(64, 255, 255, 255));
    batch.End();
    gd.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Transparent);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color& centre = pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
    EXPECT_NEAR(centre.getRProperty(), 128, 1);
    EXPECT_NEAR(centre.getGProperty(), 127, 1);
    EXPECT_EQ(centre.getBProperty(), 0);
}

TEST(HdrRenderTargetRoundTripTest, FloatTargetPartialAndMipTransfersKeepExactTypedValues)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D target(gd, 4, 4, true, SurfaceFormat::Vector4, DepthFormat::None);
    gd.SetRenderTarget(&target);
    gd.Clear(0.0f, 0.0f, 0.0f, 0.0f);
    gd.SetRenderTarget(nullptr);

    const std::array<Vector4, 4> mipValues{
        Vector4(-1.0f, 2.0f, 3.0f, 4.0f), Vector4(5.0f, 6.0f, 7.0f, 8.0f),
        Vector4(9.0f, 10.0f, 11.0f, 12.0f), Vector4(13.0f, 14.0f, 15.0f, 16.0f)};
    target.SetData(1, nullptr, mipValues.data(), 0, static_cast<int>(mipValues.size()));
    std::array<Vector4, 4> mipRead{};
    target.GetData(1, nullptr, mipRead.data(), 0, static_cast<int>(mipRead.size()));
    for (std::size_t i = 0; i < mipValues.size(); ++i)
    {
        EXPECT_FLOAT_EQ(mipRead[i].X, mipValues[i].X);
        EXPECT_FLOAT_EQ(mipRead[i].Y, mipValues[i].Y);
        EXPECT_FLOAT_EQ(mipRead[i].Z, mipValues[i].Z);
        EXPECT_FLOAT_EQ(mipRead[i].W, mipValues[i].W);
    }

    RenderTarget2D partialTarget(gd, 4, 4, false, SurfaceFormat::Vector4, DepthFormat::None);
    gd.SetRenderTarget(&partialTarget);
    gd.Clear(0.0f, 0.0f, 0.0f, 0.0f);
    gd.SetRenderTarget(nullptr);
    const Microsoft::Xna::Framework::Rectangle oneTexel(2, 1, 1, 1);
    const Vector4 partial(-4.0f, 0.25f, 8.0f, 0.5f);
    partialTarget.SetData(0, &oneTexel, &partial, 0, 1);
    Vector4 partialRead;
    partialTarget.GetData(0, &oneTexel, &partialRead, 0, 1);
    EXPECT_FLOAT_EQ(partialRead.X, partial.X);
    EXPECT_FLOAT_EQ(partialRead.Y, partial.Y);
    EXPECT_FLOAT_EQ(partialRead.Z, partial.Z);
    EXPECT_FLOAT_EQ(partialRead.W, partial.W);
}

TEST(HdrRenderTargetRoundTripTest, AFloatCubeTargetIsCreatedInTheRequestedFormat)
{
    // MOD-107: the cube path carried the same silent substitution the 2D one did -- and it is the
    // path image-based lighting needs, since an irradiance or prefiltered-specular cube is rendered
    // face by face into float storage. A cube that reported HdrBlendable while holding 8-bit texels
    // would make every IBL product quietly wrong.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTargetCube cube(gd, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);

    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::HdrBlendable);
    EXPECT_EQ(cube.getSizeProperty(), kSize);

    // Every face must be bindable and clearable in that format, not just face +X.
    for (const CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                                   CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                                   CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
    {
        gd.SetRenderTarget(&cube, face);
        gd.Clear(kRed, kGreen, kBlue, 1.0f);
    }
    gd.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
}

TEST(HdrRenderTargetRoundTripTest, AFloatCubeTargetSamplesWithoutAnRgba8Intermediate)
{
    GraphicsDevice gd(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                      PresentationParameters());
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTargetCube cube(gd, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);
    for (const CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                                   CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                                   CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
    {
        gd.SetRenderTarget(&cube, face);
        gd.Clear(2.0f, 0.0f, 0.0f, 1.0f);
    }

    constexpr int outputSize = 8;
    const VertexPositionNormalTexture vertices[6] = {
        {Vector3(-1.0f,  1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f)},
        {Vector3(-1.0f, -1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f)},
        {Vector3( 1.0f,  1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f)},
        {Vector3( 1.0f,  1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f)},
        {Vector3(-1.0f, -1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f)},
        {Vector3( 1.0f, -1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f)},
    };
    VertexBuffer buffer(gd, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                        6, BufferUsage::None);
    buffer.SetData(vertices, 6);
    Texture2D white(gd, 1, 1, false, SurfaceFormat::Color);
    const Color whitePixel = Color::White;
    white.SetData(&whitePixel, 1);
    RenderTarget2D output(gd, outputSize, outputSize, false, SurfaceFormat::Color,
                          DepthFormat::None);
    gd.SetRenderTarget(&output);
    gd.Clear(Color::Black);
    gd.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
    gd.setBlendStateProperty(BlendState::Opaque);
    gd.setRasterizerStateProperty(RasterizerState::CullNone);

    EnvironmentMapEffect effect(gd);
    effect.setTextureProperty(&white);
    effect.setEnvironmentMapProperty(&cube);
    effect.setEnvironmentMapAmountProperty(0.25f);
    effect.setFresnelFactorProperty(0.0f);
    effect.setDiffuseColorProperty(Vector3::Zero);
    effect.setAmbientLightColorProperty(Vector3::Zero);
    effect.setEmissiveColorProperty(Vector3::Zero);
    effect.getDirectionalLight0Property().setEnabledProperty(false);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    effect.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
        1.5707963f, 1.0f, 0.1f, 100.0f));
    effect.Apply();
    gd.SetVertexBuffer(&buffer);
    gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    gd.SetVertexBuffer(nullptr);
    gd.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(outputSize) * outputSize,
                              Color::Transparent);
    output.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color& centre =
        pixels[static_cast<std::size_t>(outputSize / 2) * outputSize + outputSize / 2];
    // 2.0 * 0.25 becomes 0.5. An RGBA8 intermediary would clamp 2.0 to 1.0 and produce 0.25.
    EXPECT_NEAR(centre.getRProperty(), 127, 2);
    EXPECT_EQ(centre.getGProperty(), 0);
    EXPECT_EQ(centre.getBProperty(), 0);
    EXPECT_EQ(centre.getAProperty(), 255);
}

TEST(HdrRenderTargetRoundTripTest, AnUnsupportedCubePreferredFormatIsSubstituted)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt1))
        GTEST_SKIP() << "this renderer claims a compressed render-target format; test not applicable";

    RenderTargetCube cube(gd, kSize, false, SurfaceFormat::Dxt1, DepthFormat::None);
    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Color);
}

TEST(HdrRenderTargetRoundTripTest, AFloatTargetCarriesARealDepthBuffer)
{
    // MOD-120: an HDR scene target is useless without depth -- the pipeline renders a depth-tested
    // 3D scene into it. A driver may accept the colour attachment and then refuse the assembled
    // framebuffer once depth joins it, which MOD-119's completeness check now reports as a throw;
    // this asserts the combination is accepted and the depth attachment is real.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::HdrBlendable,
                          DepthFormat::Depth24Stencil8);

    EXPECT_EQ(target.getDepthStencilFormatProperty(), DepthFormat::Depth24Stencil8);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<HalfVector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_FLOAT_EQ(pixels.front().ToVector4().X, kRed);
}

TEST(HdrRenderTargetRoundTripTest, AMultisampledFloatTargetResolvesWithoutClamping)
{
    // MOD-121: the multisample colour buffer and the resolve texture must share a format, or the
    // resolve blit either fails or lands in 8-bit storage. Reading back after the unbind exercises
    // the resolve path, so a clamped value here would mean the multisample side stayed RGBA8.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::HdrBlendable,
                          DepthFormat::None, 4);
    if (target.getMultiSampleCountProperty() <= 0)
        GTEST_SKIP() << "this device clamped the request to single-sample; nothing to resolve";

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<HalfVector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const HalfVector4& packed : pixels)
    {
        const Vector4 texel = packed.ToVector4();
        EXPECT_FLOAT_EQ(texel.X, kRed);
        EXPECT_FLOAT_EQ(texel.Y, kGreen);
    }
}

TEST(HdrRenderTargetRoundTripTest, AFloatTargetGeneratesAMipChain)
{
    // MOD-122: IBL prefiltering stores each roughness level in a mip of a float target, so the mip
    // chain has to exist and carry the same unclamped values. A uniform clear makes every level's
    // expected content identical, which is what lets level 1 be asserted at all.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    constexpr int kMippedSize = 8;
    RenderTarget2D target(gd, kMippedSize, kMippedSize, true, SurfaceFormat::HdrBlendable,
                          DepthFormat::None);
    ASSERT_GT(target.getLevelCountProperty(), 1);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<HalfVector4> level1(static_cast<std::size_t>(kMippedSize / 2) * (kMippedSize / 2));
    target.GetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));

    for (const HalfVector4& packed : level1)
    {
        const Vector4 texel = packed.ToVector4();
        EXPECT_FLOAT_EQ(texel.X, kRed);
        EXPECT_FLOAT_EQ(texel.Y, kGreen);
    }
}

TEST(HdrRenderTargetRoundTripTest, AMixedFloatAndColourTargetSetBinds)
{
    // MOD-125: the depth/normal prepass SSAO needs writes linear depth to a float attachment and
    // encoded normals to a Color one in a single pass, so a mixed-format set has to bind and draw.
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!gd.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        GTEST_SKIP() << "this renderer has no MRT";
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector2))
        GTEST_SKIP() << "this renderer/driver has no RG16F render targets";

    RenderTarget2D depthLike(gd, kSize, kSize, false, SurfaceFormat::HalfVector2,
                             DepthFormat::None);
    RenderTarget2D normals(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);

    const std::vector<RenderTargetBinding> bindings{
        RenderTargetBinding(&depthLike), RenderTargetBinding(&normals)};

    gd.SetRenderTargets(bindings);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTargets({});

    std::vector<Color> normalTexels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    normals.GetData(normalTexels.data(), static_cast<int>(normalTexels.size()));
    EXPECT_EQ(normalTexels.front().getRProperty(), 255);
}

} // namespace
