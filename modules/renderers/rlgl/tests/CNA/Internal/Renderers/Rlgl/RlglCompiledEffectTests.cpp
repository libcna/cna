// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-047/RLGL-048/RLGL-049/RLGL-051: RLGL runs the same public
// compiled-Effect runtime, draw, sampler, and SpriteBatch contracts as established renderers.

#if defined(CNA_RLGL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    TEST(RlglCompiledEffectTest, SharedBackendConformanceContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedOrdinaryDrawContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectDrawContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedMultiStreamDrawContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedInstancingDrawContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
    }

    TEST(RlglCompiledEffectTest, VertexStageSamplersMoveGeometryFromTheirOwnTextures)
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SyntheticEffectOptions;
        using CNA::TestSupport::SyntheticSamplerState;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector4;
        using namespace Microsoft::Xna::Framework::Graphics;

        GraphicsDevice device;
        SyntheticEffectOptions options;
        options.includeSampler = true;
        options.includeDrawableProgram = true;
        options.vertexShaderSamplesTexture = true;
        options.samplerStates = {
            SyntheticSamplerState{Fx::SampMagFilter, Fx::FilterPoint},
            SyntheticSamplerState{Fx::SampMinFilter, Fx::FilterPoint},
            SyntheticSamplerState{Fx::SampMipFilter, Fx::FilterPoint},
            SyntheticSamplerState{Fx::SampAddressU, Fx::AddressClamp},
            SyntheticSamplerState{Fx::SampAddressV, Fx::AddressClamp},
        };
        Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        effect.getParametersProperty()["Tint"]->SetValue(
            Vector4(0.5f, 0.25f, 0.75f, 1.0f));

        Texture2D black(device, 1, 1);
        Texture2D red(device, 1, 1);
        const Color blackPixel[1] = {Color(0, 0, 0, 255)};
        const Color redPixel[1] = {Color(255, 0, 0, 255)};
        black.SetData(blackPixel, 1);
        red.SetData(redPixel, 1);

        struct Vertex
        {
            float x, y, z;
            float u, v, lod, pad;
        };
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const Vertex leftQuad[6] = {
            {-1,  1, 0, 0, 0, 0, 0}, {-1, -1, 0, 0, 0, 0, 0},
            { 0, -1, 0, 0, 0, 0, 0}, {-1,  1, 0, 0, 0, 0, 0},
            { 0, -1, 0, 0, 0, 0, 0}, { 0,  1, 0, 0, 0, 0, 0},
        };

        const auto draw = [&](Texture2D& vertexTexture, int x) {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getParametersProperty()["FxTexture"]->SetValue(&vertexTexture);
            effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, static_cast<const void*>(leftQuad), 0, 2,
                declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel;
            const Rectangle probe(x, 4, 1, 1);
            target.GetData(0, &probe, &pixel, 0, 1);
            return pixel;
        };

        EXPECT_NEAR(draw(black, 2).getRProperty(), 128, 3)
            << "a black vertex sample must retain the left-half geometry";
        EXPECT_EQ(draw(black, 6).getRProperty(), 9);
        EXPECT_EQ(draw(red, 2).getRProperty(), 9);
        EXPECT_NEAR(draw(red, 6).getRProperty(), 128, 3)
            << "a red vertex sample must move the geometry into the right half";

        auto& renderer = dynamic_cast<
            CNA::Internal::Renderers::Rlgl::RlglRenderer&>(device.GetRenderer());
        const auto beforeLoss = renderer.GetContextRecoverySnapshotForTesting();
        EXPECT_EQ(beforeLoss.realizedVertexSamplers, 1u);
        EXPECT_EQ(beforeLoss.liveVertexSamplers, 1u);
        EXPECT_NE(beforeLoss.vertexSamplerIds[0], 0u);

        renderer.DebugSimulateContextLoss();
        const auto lost = renderer.GetContextRecoverySnapshotForTesting();
        EXPECT_EQ(lost.realizedVertexSamplers, 1u);
        EXPECT_EQ(lost.liveVertexSamplers, 0u);
        EXPECT_EQ(lost.vertexSamplerIds[0], 0u);

        renderer.DebugRestoreContext();
        const auto restored = renderer.GetContextRecoverySnapshotForTesting();
        EXPECT_EQ(restored.realizedVertexSamplers, 1u);
        EXPECT_EQ(restored.liveVertexSamplers, 1u);
        EXPECT_NE(restored.vertexSamplerIds[0], 0u);
        EXPECT_EQ(draw(black, 6).getRProperty(), 9)
            << "the restored sampler must still retain black-texture geometry on the left";
        EXPECT_NEAR(draw(red, 6).getRProperty(), 128, 3)
            << "the restored effect, texture, and sampler must move geometry right again";

        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::SamplerCube;
        options.samplerRegister = 3;
        Effect cubeEffect(device, CNA::TestSupport::BuildSyntheticEffect(options));
        cubeEffect.getParametersProperty()["Transform"]->SetValue(
            Matrix::getIdentityProperty());
        cubeEffect.getParametersProperty()["Tint"]->SetValue(
            Vector4(0.5f, 0.25f, 0.75f, 1.0f));
        TextureCube cube(device, 2, false, SurfaceFormat::Color);
        const Color redFace[4] = {Color::Red, Color::Red, Color::Red, Color::Red};
        const Color blackFace[4] = {Color::Black, Color::Black, Color::Black, Color::Black};
        for (int face = 0; face < 6; ++face)
        {
            cube.SetData(
                static_cast<CubeMapFace>(face),
                face == static_cast<int>(CubeMapFace::PositiveX) ? redFace : blackFace, 4);
        }
        std::array<Vertex, 6> cubeQuad{};
        std::copy(std::begin(leftQuad), std::end(leftQuad), cubeQuad.begin());
        for (Vertex& vertex : cubeQuad)
        {
            vertex.u = 1.0f;
            vertex.v = 0.0f;
            vertex.lod = 0.0f;
        }
        RenderTarget2D cubeTarget(device, 8, 8);
        device.SetRenderTarget(&cubeTarget);
        device.Clear(Color(9, 19, 29, 255));
        cubeEffect.getParametersProperty()["FxTexture"]->SetValue(&cube);
        cubeEffect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(cubeQuad.data()), 0, 2,
            declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color cubeLeft;
        Color cubeRight;
        const Rectangle cubeLeftProbe(2, 4, 1, 1);
        const Rectangle cubeRightProbe(6, 4, 1, 1);
        cubeTarget.GetData(0, &cubeLeftProbe, &cubeLeft, 0, 1);
        cubeTarget.GetData(0, &cubeRightProbe, &cubeRight, 0, 1);
        EXPECT_EQ(cubeLeft.getRProperty(), 9);
        EXPECT_NEAR(cubeRight.getRProperty(), 128, 3)
            << "a +X vertex-stage cube sample must select the red cube face";
        const auto afterCube = renderer.GetContextRecoverySnapshotForTesting();
        EXPECT_EQ(afterCube.realizedVertexSamplers, 2u);
        EXPECT_EQ(afterCube.liveVertexSamplers, 2u);
        EXPECT_NE(afterCube.vertexSamplerIds[0], 0u);
        EXPECT_NE(afterCube.vertexSamplerIds[3], 0u)
            << "logical vertex sampler 3 must occupy the final XNA/MojoShader slot";
    }

    TEST(RlglCompiledEffectTest, SharedOrientationContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectOrientationContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedEffectSwitchingContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedSamplerPixelContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedPassSelectionContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedStockDrawIsolationContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedRenderTargetSourceContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedSpriteBatchContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedSpriteBatchMultiPassContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedSpriteBatchTextureSlotContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedSpriteBatchRenderTargetSourceContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedCubeAndVolumeSamplerContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
    }

    TEST(RlglCompiledEffectTest, SpriteBatchUsesDeviceFallbackSlotsAndRestoresStockDraws)
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SyntheticSamplerState;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector4;
        using namespace Microsoft::Xna::Framework::Graphics;

        GraphicsDevice device;
        const std::vector<SyntheticSamplerState> pointClamp = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            pointClamp, 1));
        effect.getParametersProperty()["Tint"]->SetValue(
            Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        effect.getParametersProperty()["Transform"]->SetValue(
            Matrix::CreateOrthographicOffCenter(
                0.0f, 8.0f, 8.0f, 0.0f, -1.0f, 1.0f));

        Texture2D sprite(device, 1, 1);
        Texture2D fallback(device, 1, 1);
        const Color white[1] = {Color::White};
        const Color green[1] = {Color(0, 255, 0, 255)};
        sprite.SetData(white, 1);
        fallback.SetData(green, 1);
        device.getTexturesProperty()(1, &fallback);
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        RenderTarget2D compiledTarget(device, 8, 8);
        device.SetRenderTarget(&compiledTarget);
        device.Clear(Color(9, 19, 29, 255));
        SpriteBatch compiledBatch(device);
        compiledBatch.Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            &SamplerState::PointClamp, nullptr, nullptr, &effect);
        compiledBatch.Draw(sprite, Rectangle(0, 0, 8, 8), Color::White);
        compiledBatch.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color compiledPixel(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        compiledTarget.GetData(0, &centre, &compiledPixel, 0, 1);
        EXPECT_NEAR(compiledPixel.getRProperty(), 0, 3);
        EXPECT_NEAR(compiledPixel.getGProperty(), 255, 3)
            << "a null effect texture must fall back to GraphicsDevice.Textures[1]";
        EXPECT_NEAR(compiledPixel.getBProperty(), 0, 3);

        Texture2D blue(device, 1, 1);
        const Color bluePixel[1] = {Color(0, 0, 255, 255)};
        blue.SetData(bluePixel, 1);
        RenderTarget2D stockTarget(device, 8, 8);
        device.SetRenderTarget(&stockTarget);
        device.Clear(Color(9, 19, 29, 255));
        SpriteBatch stockBatch(device);
        stockBatch.Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            &SamplerState::PointClamp, nullptr, nullptr);
        stockBatch.Draw(blue, Rectangle(0, 0, 8, 8), Color::White);
        stockBatch.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color stockPixel(0, 0, 0, 0);
        stockTarget.GetData(0, &centre, &stockPixel, 0, 1);
        EXPECT_NEAR(stockPixel.getRProperty(), 0, 3);
        EXPECT_NEAR(stockPixel.getGProperty(), 0, 3);
        EXPECT_NEAR(stockPixel.getBProperty(), 255, 3)
            << "a stock SpriteBatch after a compiled one must restore its own VAO and program";
    }

    TEST(RlglCompiledEffectTest, SpriteBatchPixelOnlyPassInheritsStockVertexShader)
    {
        using CNA::TestSupport::SyntheticEffectOptions;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector4;
        using namespace Microsoft::Xna::Framework::Graphics;

        GraphicsDevice device;
        SyntheticEffectOptions options;
        options.includeSampler = true;
        Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
        effect.getParametersProperty()["Tint"]->SetValue(
            Vector4(0.25f, 0.5f, 0.75f, 1.0f));

        Texture2D sprite(device, 1, 1);
        const Color white[1] = {Color::White};
        sprite.SetData(white, 1);

        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        SpriteBatch batch(device);
        batch.Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            &SamplerState::PointClamp, nullptr, nullptr, &effect);
        batch.Draw(sprite, Rectangle(0, 0, 8, 8), Color::White);
        batch.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &actual, 0, 1);
        EXPECT_NEAR(actual.getRProperty(), 64, 3);
        EXPECT_NEAR(actual.getGProperty(), 128, 3);
        EXPECT_NEAR(actual.getBProperty(), 191, 3)
            << "a pixel-only pass must inherit CNA's embedded stock sprite vertex shader";
    }

    TEST(RlglCompiledEffectTest, SpriteBatchExceptionRestoresStockDrawState)
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SyntheticSamplerKind;
        using CNA::TestSupport::SyntheticSamplerState;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector4;
        using namespace Microsoft::Xna::Framework::Graphics;

        GraphicsDevice device;
        const std::vector<SyntheticSamplerState> pointClamp = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };
        Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            pointClamp, 0, SyntheticSamplerKind::SamplerCube));
        effect.getParametersProperty()["Tint"]->SetValue(
            Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        Texture2D flat(device, 1, 1);
        const Color white[1] = {Color::White};
        flat.SetData(white, 1);
        RenderTarget2D failedTarget(device, 8, 8);
        device.SetRenderTarget(&failedTarget);
        SpriteBatch failedBatch(device);
        failedBatch.Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            &SamplerState::PointClamp, nullptr, nullptr, &effect);
        failedBatch.Draw(flat, Rectangle(0, 0, 8, 8), Color::White);
        EXPECT_THROW(failedBatch.End(), System::NotSupportedException);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Texture2D blue(device, 1, 1);
        const Color bluePixel[1] = {Color(0, 0, 255, 255)};
        blue.SetData(bluePixel, 1);
        RenderTarget2D stockTarget(device, 8, 8);
        device.SetRenderTarget(&stockTarget);
        device.Clear(Color(9, 19, 29, 255));
        SpriteBatch stockBatch(device);
        stockBatch.Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            &SamplerState::PointClamp, nullptr, nullptr);
        stockBatch.Draw(blue, Rectangle(0, 0, 8, 8), Color::White);
        stockBatch.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        stockTarget.GetData(0, &centre, &actual, 0, 1);
        EXPECT_NEAR(actual.getRProperty(), 0, 3);
        EXPECT_NEAR(actual.getGProperty(), 0, 3);
        EXPECT_NEAR(actual.getBProperty(), 255, 3)
            << "a failed compiled sprite draw must restore the stock VAO and program";
    }

    TEST(RlglCompiledEffectTest, CubeSamplerDrawsTheSelectedFace)
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SamplingQuadDeclarationXYZ;
        using CNA::TestSupport::SamplingQuadVertexXYZ;
        using CNA::TestSupport::SyntheticSamplerKind;
        using CNA::TestSupport::SyntheticSamplerState;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector4;
        using Microsoft::Xna::Framework::Graphics::BlendState;
        using Microsoft::Xna::Framework::Graphics::CubeMapFace;
        using Microsoft::Xna::Framework::Graphics::DepthStencilState;
        using Microsoft::Xna::Framework::Graphics::Effect;
        using Microsoft::Xna::Framework::Graphics::PrimitiveType;
        using Microsoft::Xna::Framework::Graphics::RasterizerState;
        using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        using Microsoft::Xna::Framework::Graphics::TextureCube;

        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        const std::vector<SyntheticSamplerState> pointClamp = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };

        const Color faceColors[6] = {
            Color(255, 0, 0, 255), Color(0, 255, 0, 255),
            Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            Color(255, 0, 255, 255), Color(0, 255, 255, 255),
        };
        TextureCube cube(device, 2, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const Color texels[4] = {
                faceColors[face], faceColors[face], faceColors[face], faceColors[face],
            };
            cube.SetData(static_cast<CubeMapFace>(face), texels, 4);
        }

        Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            pointClamp, 0, SyntheticSamplerKind::SamplerCube));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        parameters["FxTexture"]->SetValue(&cube);

        SamplingQuadVertexXYZ quad[6];
        CNA::TestSupport::FillSamplingQuadXYZ(quad, 1.0f, 0.0f, 0.0f);
        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
            SamplingQuadDeclarationXYZ());
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &actual, 0, 1);
        EXPECT_NEAR(actual.getRProperty(), faceColors[0].getRProperty(), 3);
        EXPECT_NEAR(actual.getGProperty(), faceColors[0].getGProperty(), 3);
        EXPECT_NEAR(actual.getBProperty(), faceColors[0].getBProperty(), 3);
    }

    TEST(RlglCompiledEffectTest, TextureDimensionMismatchIsNamedAndTheNextDrawRecovers)
    {
        namespace Fx = CNA::TestSupport::EffectFormat;
        using CNA::TestSupport::SamplingQuadDeclarationXYZ;
        using CNA::TestSupport::SamplingQuadVertexXYZ;
        using CNA::TestSupport::SyntheticSamplerKind;
        using CNA::TestSupport::SyntheticSamplerState;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector4;
        using namespace Microsoft::Xna::Framework::Graphics;

        GraphicsDevice device;
        const std::vector<SyntheticSamplerState> pointClamp = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };
        Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            pointClamp, 0, SyntheticSamplerKind::SamplerCube));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        Texture2D flat(device, 1, 1);
        const Color white[1] = {Color::White};
        flat.SetData(white, 1);
        parameters["FxTexture"]->SetValue(&flat);
        SamplingQuadVertexXYZ quad[6];
        CNA::TestSupport::FillSamplingQuadXYZ(quad, 1.0f, 0.0f, 0.0f);
        effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();

        std::string refusal;
        try
        {
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
                SamplingQuadDeclarationXYZ());
        }
        catch (const std::exception& error)
        {
            refusal = error.what();
        }
        EXPECT_NE(refusal.find("different texture dimension"), std::string::npos)
            << "a samplerCube/Texture2D mismatch must be refused by name";

        TextureCube cube(device, 2, false, SurfaceFormat::Color);
        const Color red[4] = {
            Color(255, 0, 0, 255), Color(255, 0, 0, 255),
            Color(255, 0, 0, 255), Color(255, 0, 0, 255),
        };
        cube.SetData(CubeMapFace::PositiveX, red, 4);
        parameters["FxTexture"]->SetValue(&cube);

        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
            SamplingQuadDeclarationXYZ());
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &actual, 0, 1);
        EXPECT_NEAR(actual.getRProperty(), 255, 3);
        EXPECT_NEAR(actual.getGProperty(), 0, 3);
        EXPECT_NEAR(actual.getBProperty(), 0, 3);
    }

    TEST(RlglCompiledEffectTest, CompiledDepthConventionMatchesAndRestoresStockDraws)
    {
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector3;
        using Microsoft::Xna::Framework::Vector4;
        using namespace Microsoft::Xna::Framework::Graphics;

        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);

        struct ClipVertex { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const ClipVertex compiledQuad[6] = {
            {-1.0f,  1.0f, 0.5f}, {-1.0f, -1.0f, 0.5f}, { 1.0f, -1.0f, 0.5f},
            {-1.0f,  1.0f, 0.5f}, { 1.0f, -1.0f, 0.5f}, { 1.0f,  1.0f, 0.5f},
        };
        Effect compiled(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
        compiled.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        compiled.getParametersProperty()["Tint"]->SetValue(Vector4(1.0f, 0.0f, 0.0f, 1.0f));
        compiled.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(compiledQuad), 0, 2,
            declaration);

        VertexPositionColor stockQuad[6];
        for (int i = 0; i < 6; ++i)
        {
            stockQuad[i] = VertexPositionColor(
                Vector3(compiledQuad[i].x, compiledQuad[i].y, 0.4f),
                Color(0, 255, 0, 255));
        }
        BasicEffect stock(device);
        stock.VertexColorEnabled = true;
        stock.setLightingEnabledProperty(false);
        stock.setTextureEnabledProperty(false);
        stock.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, stockQuad, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color actual(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &actual, 0, 1);
        EXPECT_NEAR(actual.getRProperty(), 0, 3);
        EXPECT_NEAR(actual.getGProperty(), 255, 3)
            << "stock z=0.4 must remain nearer than compiled z=0.5";
        EXPECT_NEAR(actual.getBProperty(), 0, 3);
    }

    TEST(RlglCompiledEffectTest, SharedManyDrawsContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
    }

    TEST(RlglCompiledEffectTest, SharedTruncationContract)
    {
        GraphicsDevice device;
        ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
        CNA::TestSupport::RunCompiledEffectTruncationContract(device);
    }
}

#endif
