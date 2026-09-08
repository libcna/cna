// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/NotSupportedException.hpp"

using namespace CNA::Testing::Renderers;
using CNA::Internal::Renderers::RendererFormatVerdict;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    template<typename T, typename Factory>
    void VerifyExactTransfers(GraphicsDevice& device, SurfaceFormat format, Factory makeValue)
    {
        const RendererFormatVerdict verdict =
            device.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(format));
        if (verdict != RendererFormatVerdict::Supported)
        {
            if (CNA_RENDERER_IS(Software))
                ADD_FAILURE() << "Software must support classic SurfaceFormat ordinal "
                              << static_cast<int>(format);
            return;
        }

        SCOPED_TRACE(static_cast<int>(format));
        Texture2D texture(device, 4, 4, true, format);

        std::vector<T> expected;
        expected.reserve(16);
        for (int i = 0; i < 16; ++i) expected.push_back(makeValue(i));
        texture.SetData(expected.data(), static_cast<int>(expected.size()));

        std::vector<T> actual(16, makeValue(90));
        texture.GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);

        const Rectangle patchRectangle(1, 1, 2, 2);
        std::vector<T> patch(6, makeValue(91));
        for (int i = 0; i < 4; ++i) patch[static_cast<std::size_t>(i + 1)] = makeValue(20 + i);
        texture.SetData(0, &patchRectangle, patch.data(), 1, static_cast<int>(patch.size()));
        expected[5] = patch[1];
        expected[6] = patch[2];
        expected[9] = patch[3];
        expected[10] = patch[4];

        std::vector<T> afterPatch(16, makeValue(92));
        texture.GetData(afterPatch.data(), static_cast<int>(afterPatch.size()));
        EXPECT_EQ(afterPatch, expected);

        std::vector<T> rectangleRead(6, makeValue(93));
        texture.GetData(0, &patchRectangle, rectangleRead.data(), 1,
                        static_cast<int>(rectangleRead.size()));
        EXPECT_EQ(rectangleRead[0], makeValue(93));
        EXPECT_EQ(rectangleRead[1], patch[1]);
        EXPECT_EQ(rectangleRead[2], patch[2]);
        EXPECT_EQ(rectangleRead[3], patch[3]);
        EXPECT_EQ(rectangleRead[4], patch[4]);
        EXPECT_EQ(rectangleRead[5], makeValue(93));

        const T mipValue = makeValue(40);
        texture.SetData(2, nullptr, &mipValue, 0, 1);
        T mipRead = makeValue(94);
        texture.GetData(2, nullptr, &mipRead, 0, 1);
        EXPECT_EQ(mipRead, mipValue);
    }

    [[nodiscard]] Color DrawWithSpriteBatch(GraphicsDevice& device, Texture2D& texture)
    {
        constexpr int size = 8;
        RenderTarget2D target(device, size, size, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        SpriteBatch batch(device);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        SamplerState point = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        batch.Draw(texture, Rectangle(0, 0, size, size), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(size) * size);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2];
    }

    template<typename T>
    void VerifySpriteSample(GraphicsDevice& device, SurfaceFormat format, const T& value,
                            int r, int g, int b, int a, int tolerance = 2)
    {
        if (device.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(format)) !=
            RendererFormatVerdict::Supported)
            return;
        SCOPED_TRACE(static_cast<int>(format));
        Texture2D texture(device, 1, 1, false, format);
        texture.SetData(&value, 1);
        const Color pixel = DrawWithSpriteBatch(device, texture);
        EXPECT_NEAR(pixel.getRProperty(), r, tolerance);
        EXPECT_NEAR(pixel.getGProperty(), g, tolerance);
        EXPECT_NEAR(pixel.getBProperty(), b, tolerance);
        EXPECT_NEAR(pixel.getAProperty(), a, tolerance);
    }

    [[nodiscard]] Color DrawWithBasicEffect(GraphicsDevice& device, Texture2D& texture,
                                            const Vector3& diffuse)
    {
        constexpr int size = 8;
        const VertexPositionTexture vertices[6] = {
            {Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f)},
        };
        VertexBuffer buffer(device, VertexPositionTexture::getVertexDeclarationStatic(),
                            6, BufferUsage::None);
        buffer.SetData(vertices, 6);
        RenderTarget2D target(device, size, size, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect effect(device);
        effect.setTextureProperty(&texture);
        effect.setTextureEnabledProperty(true);
        effect.setDiffuseColorProperty(diffuse);
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(size) * size);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2];
    }

    [[nodiscard]] Color DrawWithEnvironmentMap(GraphicsDevice& device, TextureCube& cube)
    {
        constexpr int size = 8;
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1.0f,  1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f,  1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, -2.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(1.0f, 1.0f)},
        };
        VertexBuffer buffer(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                            6, BufferUsage::None);
        buffer.SetData(vertices, 6);
        Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        RenderTarget2D target(device, size, size, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        EnvironmentMapEffect effect(device);
        effect.setTextureProperty(&white);
        effect.setEnvironmentMapProperty(&cube);
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setFresnelFactorProperty(0.0f);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::One);
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
        effect.setProjectionProperty(Microsoft::Xna::Framework::Matrix::CreatePerspectiveFieldOfView(
            1.5707963f, 1.0f, 0.1f, 100.0f));
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(size) * size);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2];
    }
}

TEST(ClassicTextureFormat, EveryPromotedFormatPreservesFullPartialAndMipBytesExactly)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());

    VerifyExactTransfers<NormalizedByte2>(device, SurfaceFormat::NormalizedByte2, [](int i) {
        return NormalizedByte2((i % 5 - 2) * 0.25f, (i % 7 - 3) * 0.2f);
    });
    VerifyExactTransfers<NormalizedByte4>(device, SurfaceFormat::NormalizedByte4, [](int i) {
        return NormalizedByte4((i % 5 - 2) * 0.25f, (i % 7 - 3) * 0.2f,
                               (i % 3 - 1) * 0.5f, (i % 9 - 4) * 0.125f);
    });
    VerifyExactTransfers<Rgba1010102>(device, SurfaceFormat::Rgba1010102, [](int i) {
        return Rgba1010102((i % 11) / 10.0f, (i % 7) / 6.0f,
                           (i % 5) / 4.0f, (i % 4) / 3.0f);
    });
    VerifyExactTransfers<Rg32>(device, SurfaceFormat::Rg32, [](int i) {
        return Rg32((i % 13) / 12.0f, (i % 9) / 8.0f);
    });
    VerifyExactTransfers<Rgba64>(device, SurfaceFormat::Rgba64, [](int i) {
        return Rgba64((i % 13) / 12.0f, (i % 11) / 10.0f,
                      (i % 7) / 6.0f, (i % 5) / 4.0f);
    });
    VerifyExactTransfers<Alpha8>(device, SurfaceFormat::Alpha8, [](int i) {
        return Alpha8((i % 17) / 16.0f);
    });
    VerifyExactTransfers<float>(device, SurfaceFormat::Single, [](int i) {
        return static_cast<float>(i - 8) * 0.375f;
    });
    VerifyExactTransfers<Vector2>(device, SurfaceFormat::Vector2, [](int i) {
        return Vector2(static_cast<float>(i - 8) * 0.25f,
                       static_cast<float>(7 - i) * 0.125f);
    });
    VerifyExactTransfers<Vector4>(device, SurfaceFormat::Vector4, [](int i) {
        return Vector4(static_cast<float>(i - 8) * 0.25f,
                       static_cast<float>(7 - i) * 0.125f,
                       static_cast<float>(i % 5) * 0.5f,
                       static_cast<float>(i % 3) * 0.25f);
    });
    VerifyExactTransfers<HalfSingle>(device, SurfaceFormat::HalfSingle, [](int i) {
        return HalfSingle(static_cast<float>(i - 8) * 0.25f);
    });
    VerifyExactTransfers<HalfVector2>(device, SurfaceFormat::HalfVector2, [](int i) {
        return HalfVector2(static_cast<float>(i - 8) * 0.25f,
                           static_cast<float>(7 - i) * 0.125f);
    });
    VerifyExactTransfers<HalfVector4>(device, SurfaceFormat::HalfVector4, [](int i) {
        return HalfVector4(static_cast<float>(i - 8) * 0.25f,
                           static_cast<float>(7 - i) * 0.125f,
                           static_cast<float>(i % 5) * 0.5f,
                           static_cast<float>(i % 3) * 0.25f);
    });
    VerifyExactTransfers<HalfVector4>(device, SurfaceFormat::HdrBlendable, [](int i) {
        return HalfVector4(static_cast<float>(i - 8) * 0.5f,
                           static_cast<float>(7 - i) * 0.25f,
                           static_cast<float>(i % 5), 1.0f);
    });
}

TEST(ClassicTextureFormat, PointSamplingExpandsChannelsAndPreservesDeclaredRanges)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());

    VerifySpriteSample(device, SurfaceFormat::NormalizedByte2,
                       NormalizedByte2(0.5f, 0.25f), 129, 64, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::NormalizedByte4,
                       NormalizedByte4(0.5f, 0.25f, 1.0f, 0.5f), 129, 64, 255, 129);
    VerifySpriteSample(device, SurfaceFormat::Rgba1010102,
                       Rgba1010102(0.5f, 0.25f, 1.0f, 1.0f), 128, 64, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::Rg32,
                       Rg32(0.5f, 0.25f), 128, 64, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::Rgba64,
                       Rgba64(0.5f, 0.25f, 1.0f, 0.5f), 128, 64, 255, 128);
    VerifySpriteSample(device, SurfaceFormat::Alpha8,
                       Alpha8(0.5f), 0, 0, 0, 128);
    VerifySpriteSample(device, SurfaceFormat::Single, 0.25f, 64, 255, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::Vector2,
                       Vector2(0.25f, 0.5f), 64, 128, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::Vector4,
                       Vector4(0.25f, 0.5f, 1.0f, 0.75f), 64, 128, 255, 191);
    VerifySpriteSample(device, SurfaceFormat::HalfSingle,
                       HalfSingle(0.25f), 64, 255, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::HalfVector2,
                       HalfVector2(0.25f, 0.5f), 64, 128, 255, 255);
    VerifySpriteSample(device, SurfaceFormat::HalfVector4,
                       HalfVector4(0.25f, 0.5f, 1.0f, 0.75f), 64, 128, 255, 191);
    VerifySpriteSample(device, SurfaceFormat::HdrBlendable,
                       HalfVector4(0.25f, 0.5f, 1.0f, 0.75f), 64, 128, 255, 191);

    if (device.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(SurfaceFormat::Single)) ==
        RendererFormatVerdict::Supported)
    {
        Texture2D hdr(device, 1, 1, false, SurfaceFormat::Single);
        const float two = 2.0f;
        hdr.SetData(&two, 1);
        const Color hdrPixel = DrawWithBasicEffect(device, hdr, Vector3(0.25f, 0.0f, 0.0f));
        EXPECT_NEAR(hdrPixel.getRProperty(), 128, 3);
        EXPECT_NEAR(hdrPixel.getGProperty(), 0, 2);
        EXPECT_NEAR(hdrPixel.getBProperty(), 0, 2);
    }

    if (device.GetRenderer().ClassifySurfaceFormatEXT(
            static_cast<int>(SurfaceFormat::NormalizedByte4)) == RendererFormatVerdict::Supported)
    {
        Texture2D signedTexture(device, 1, 1, false, SurfaceFormat::NormalizedByte4);
        const NormalizedByte4 negative(-0.5f, 0.0f, 0.0f, 1.0f);
        signedTexture.SetData(&negative, 1);
        const Color signedPixel =
            DrawWithBasicEffect(device, signedTexture, Vector3(-1.0f, 0.0f, 0.0f));
        EXPECT_NEAR(signedPixel.getRProperty(), 129, 3);
        EXPECT_NEAR(signedPixel.getGProperty(), 0, 2);
        EXPECT_NEAR(signedPixel.getBProperty(), 0, 2);
    }
}

TEST(ClassicTextureFormat, DxtCubeBlocksFeedThePublicEnvironmentMapSampler)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!device.GetRenderer().IsCompressedCubeTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt1)))
        GTEST_SKIP() << "this renderer has no compressed cube transfer route";

    TextureCube cube(device, 4, false, SurfaceFormat::Dxt1);
    const std::uint8_t redBlock[8] = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
    cube.SetData(CubeMapFace::PositiveZ, redBlock, 8);

    const Color sampled = DrawWithEnvironmentMap(device, cube);
    EXPECT_NEAR(sampled.getRProperty(), 255, 2);
    EXPECT_NEAR(sampled.getGProperty(), 0, 2);
    EXPECT_NEAR(sampled.getBProperty(), 0, 2);
    EXPECT_NEAR(sampled.getAProperty(), 255, 2);
}

TEST(ClassicTextureFormat, PlainCubeCapabilityDoesNotInheritTexture2DFormatClaims)
{
    if (!CNA_RENDERER_IS(Software, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2))
        GTEST_SKIP() << "the audited cube capability belongs to Software and EasyGL";

    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    const SurfaceFormat unfinishedFormats[] = {
        SurfaceFormat::Bgr565,
        SurfaceFormat::Bgra5551,
        SurfaceFormat::Bgra4444,
        SurfaceFormat::NormalizedByte2,
        SurfaceFormat::NormalizedByte4,
        SurfaceFormat::Rgba1010102,
        SurfaceFormat::Rg32,
        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable,
    };
    for (const SurfaceFormat format : unfinishedFormats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        EXPECT_EQ(device.GetRenderer().ClassifyTextureCubeFormatEXT(
                      static_cast<int>(format)),
                  RendererFormatVerdict::Unsupported);
        EXPECT_THROW(TextureCube(device, 4, false, format), System::NotSupportedException);
    }

    for (const SurfaceFormat format : {
             SurfaceFormat::Color, SurfaceFormat::Dxt1,
             SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
    {
        SCOPED_TRACE(static_cast<int>(format));
        EXPECT_EQ(device.GetRenderer().ClassifyTextureCubeFormatEXT(
                      static_cast<int>(format)),
                  RendererFormatVerdict::Supported);
        EXPECT_NO_THROW(TextureCube(device, 4, false, format));
    }
}
