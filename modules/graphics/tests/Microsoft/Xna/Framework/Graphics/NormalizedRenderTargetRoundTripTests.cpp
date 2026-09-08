// SPDX-License-Identifier: MS-PL

#include <array>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    template<typename T, typename Factory>
    void VerifyTarget2DTransfers(GraphicsDevice& device, SurfaceFormat format, Factory makeValue)
    {
        SCOPED_TRACE(static_cast<int>(format));
        RenderTarget2D target(device, 4, 4, true, format, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        std::vector<T> expected;
        expected.reserve(16);
        for (int index = 0; index < 16; ++index)
            expected.push_back(makeValue(index));
        target.SetData(expected.data(), static_cast<int>(expected.size()));

        std::vector<T> actual(16, makeValue(90));
        target.GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);

        const Rectangle rectangle(1, 1, 2, 2);
        std::vector<T> patch(6, makeValue(91));
        for (int index = 0; index < 4; ++index)
            patch[static_cast<std::size_t>(index + 1)] = makeValue(20 + index);
        target.SetData(0, &rectangle, patch.data(), 1, 4);
        expected[5] = patch[1];
        expected[6] = patch[2];
        expected[9] = patch[3];
        expected[10] = patch[4];
        target.GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);

        const T mip = makeValue(40);
        target.SetData(2, nullptr, &mip, 0, 1);
        T mipRead = makeValue(92);
        target.GetData(2, nullptr, &mipRead, 0, 1);
        EXPECT_EQ(mipRead, mip);
    }

    template<typename T, typename Factory>
    void VerifyTargetCubeTransfers(GraphicsDevice& device, SurfaceFormat format, Factory makeValue)
    {
        SCOPED_TRACE(static_cast<int>(format));
        RenderTargetCube target(device, 4, true, format, DepthFormat::None, 0,
                                RenderTargetUsage::PreserveContents);
        std::vector<T> expected;
        expected.reserve(16);
        for (int index = 0; index < 16; ++index)
            expected.push_back(makeValue(index));
        target.SetData(CubeMapFace::PositiveZ, expected.data(), 16);

        std::vector<T> actual(16, makeValue(90));
        target.GetData(CubeMapFace::PositiveZ, actual.data(), 16);
        EXPECT_EQ(actual, expected);

        const Rectangle rectangle(1, 1, 2, 2);
        std::vector<T> patch(6, makeValue(91));
        for (int index = 0; index < 4; ++index)
            patch[static_cast<std::size_t>(index + 1)] = makeValue(20 + index);
        target.SetData(CubeMapFace::PositiveZ, 0, &rectangle, patch.data(), 1, 4);
        expected[5] = patch[1];
        expected[6] = patch[2];
        expected[9] = patch[3];
        expected[10] = patch[4];
        target.GetData(CubeMapFace::PositiveZ, actual.data(), 16);
        EXPECT_EQ(actual, expected);

        const T mip = makeValue(40);
        target.SetData(CubeMapFace::NegativeX, 2, nullptr, &mip, 0, 1);
        T mipRead = makeValue(92);
        target.GetData(CubeMapFace::NegativeX, 2, nullptr, &mipRead, 0, 1);
        EXPECT_EQ(mipRead, mip);
    }

    [[nodiscard]] Color SampleTarget2DWithBasicEffect(GraphicsDevice& device, Texture2D& source)
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
        RenderTarget2D output(device, size, size, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&output);
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect effect(device);
        effect.setTextureProperty(&source);
        effect.setTextureEnabledProperty(true);
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(size) * size, Color::Transparent);
        output.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2];
    }

    [[nodiscard]] Color SampleTargetCubeWithEnvironmentMap(
        GraphicsDevice& device, TextureCube& source)
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
        RenderTarget2D output(device, size, size, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&output);
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        EnvironmentMapEffect effect(device);
        effect.setTextureProperty(&white);
        effect.setEnvironmentMapProperty(&source);
        effect.setEnvironmentMapAmountProperty(1.0f);
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
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(size) * size, Color::Transparent);
        output.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2];
    }

    [[nodiscard]] bool IsCampaignRenderer()
    {
        return CNA_RENDERER_IS(Software, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2);
    }

    [[nodiscard]] bool HasCompleteNormalizedTargetMatrix(const GraphicsDevice& device)
    {
        return device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rgba1010102) &&
               device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rg32) &&
               device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rgba64);
    }
}

TEST(NormalizedRenderTargetRoundTrip, CapabilityAndConstructionUseTheRequestedFormats)
{
    if (!IsCampaignRenderer()) GTEST_SKIP() << "Software/EasyGL parity contract";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    for (SurfaceFormat format : {SurfaceFormat::Rgba1010102, SurfaceFormat::Rg32,
                                 SurfaceFormat::Rgba64})
    {
        SCOPED_TRACE(static_cast<int>(format));
        const bool supported = device.SupportsSurfaceFormatAsRenderTargetEXT(format);
        if (!supported)
        {
            EXPECT_FALSE(CNA_RENDERER_IS(Software));
            EXPECT_ANY_THROW(RenderTarget2D(device, 2, 2, false, format, DepthFormat::None));
            EXPECT_ANY_THROW(RenderTargetCube(device, 2, false, format, DepthFormat::None));
            continue;
        }
        RenderTarget2D target2D(device, 2, 2, false, format, DepthFormat::None);
        RenderTargetCube targetCube(device, 2, false, format, DepthFormat::None);
        EXPECT_EQ(target2D.getFormatProperty(), format);
        EXPECT_EQ(targetCube.getFormatProperty(), format);
    }
}

TEST(NormalizedRenderTargetRoundTrip, Target2DPreservesExactTypedRegionsAndMips)
{
    if (!IsCampaignRenderer()) GTEST_SKIP() << "Software/EasyGL parity contract";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!HasCompleteNormalizedTargetMatrix(device))
        GTEST_SKIP() << "Active EasyGL context cannot represent the exact normalized target matrix";
    VerifyTarget2DTransfers<Rgba1010102>(device, SurfaceFormat::Rgba1010102, [](int i) {
        return Rgba1010102((i % 11) / 10.0f, (i % 7) / 6.0f,
                           (i % 5) / 4.0f, (i % 4) / 3.0f);
    });
    VerifyTarget2DTransfers<Rg32>(device, SurfaceFormat::Rg32, [](int i) {
        return Rg32((i % 13) / 12.0f, (i % 9) / 8.0f);
    });
    VerifyTarget2DTransfers<Rgba64>(device, SurfaceFormat::Rgba64, [](int i) {
        return Rgba64((i % 13) / 12.0f, (i % 11) / 10.0f,
                      (i % 7) / 6.0f, (i % 5) / 4.0f);
    });
}

TEST(NormalizedRenderTargetRoundTrip, ClearMipMsaaAndMissingChannelsStayFormatCorrect)
{
    if (!IsCampaignRenderer()) GTEST_SKIP() << "Software/EasyGL parity contract";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!HasCompleteNormalizedTargetMatrix(device))
        GTEST_SKIP() << "Active EasyGL context cannot represent the exact normalized target matrix";
    RenderTarget2D rgba10(device, 4, 4, true, SurfaceFormat::Rgba1010102,
                          DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&rgba10);
    device.Clear(0.25f, 0.5f, 0.75f, 0.75f);
    device.SetRenderTarget(nullptr);
    std::array<Rgba1010102, 4> mip{};
    rgba10.GetData(1, nullptr, mip.data(), 0, 4);
    const Rgba1010102 expected10(0.25f, 0.5f, 0.75f, 0.75f);
    for (const Rgba1010102& texel : mip)
        EXPECT_EQ(texel, expected10);
    if (CNA_RENDERER_IS(Software))
        EXPECT_EQ(rgba10.getMultiSampleCountProperty(), 4);

    RenderTarget2D rg(device, 2, 2, false, SurfaceFormat::Rg32, DepthFormat::None, 0,
                      RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&rg);
    device.Clear(0.25f, 0.5f, 0.0f, 0.0f);
    device.SetRenderTarget(nullptr);
    const Color sampled = SampleTarget2DWithBasicEffect(device, rg);
    EXPECT_NEAR(sampled.getRProperty(), 64, 1);
    EXPECT_NEAR(sampled.getGProperty(), 128, 1);
    EXPECT_EQ(sampled.getBProperty(), 255);
    EXPECT_EQ(sampled.getAProperty(), 255);
}

TEST(NormalizedRenderTargetRoundTrip, TargetCubePreservesExactTypedFacesRegionsAndMips)
{
    if (!IsCampaignRenderer()) GTEST_SKIP() << "Software/EasyGL parity contract";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!HasCompleteNormalizedTargetMatrix(device))
        GTEST_SKIP() << "Active EasyGL context cannot represent the exact normalized target matrix";
    VerifyTargetCubeTransfers<Rgba1010102>(device, SurfaceFormat::Rgba1010102, [](int i) {
        return Rgba1010102((i % 11) / 10.0f, (i % 7) / 6.0f,
                           (i % 5) / 4.0f, (i % 4) / 3.0f);
    });
    VerifyTargetCubeTransfers<Rg32>(device, SurfaceFormat::Rg32, [](int i) {
        return Rg32((i % 13) / 12.0f, (i % 9) / 8.0f);
    });
    VerifyTargetCubeTransfers<Rgba64>(device, SurfaceFormat::Rgba64, [](int i) {
        return Rgba64((i % 13) / 12.0f, (i % 11) / 10.0f,
                      (i % 7) / 6.0f, (i % 5) / 4.0f);
    });
}

TEST(NormalizedRenderTargetRoundTrip, RenderedCubeMipsResolveAndSampleWithoutRgba8Substitution)
{
    if (!IsCampaignRenderer()) GTEST_SKIP() << "Software/EasyGL parity contract";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!HasCompleteNormalizedTargetMatrix(device))
        GTEST_SKIP() << "Active EasyGL context cannot represent the exact normalized target matrix";
    RenderTargetCube cube(device, 4, true, SurfaceFormat::Rgba64, DepthFormat::None, 4,
                          RenderTargetUsage::PreserveContents);
    for (CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                             CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                             CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
    {
        device.SetRenderTarget(&cube, face);
        device.Clear(0.25f, 0.5f, 0.75f, 1.0f);
    }
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    std::array<Rgba64, 16> levelZero{};
    cube.GetData(CubeMapFace::PositiveZ, 0, nullptr, levelZero.data(), 0, 16);
    EXPECT_EQ(levelZero[0], Rgba64(0.25f, 0.5f, 0.75f, 1.0f));
    std::array<Rgba64, 4> mip{};
    cube.GetData(CubeMapFace::PositiveZ, 1, nullptr, mip.data(), 0, 4);
    const Rgba64 expected(0.25f, 0.5f, 0.75f, 1.0f);
    for (const Rgba64& texel : mip)
        EXPECT_EQ(texel, expected);
    if (CNA_RENDERER_IS(Software))
        EXPECT_EQ(cube.getMultiSampleCountProperty(), 4);

    const Color sampled = SampleTargetCubeWithEnvironmentMap(device, cube);
    EXPECT_NEAR(sampled.getRProperty(), 64, 2);
    EXPECT_NEAR(sampled.getGProperty(), 128, 2);
    EXPECT_NEAR(sampled.getBProperty(), 191, 2);
    EXPECT_EQ(sampled.getAProperty(), 255);
}
