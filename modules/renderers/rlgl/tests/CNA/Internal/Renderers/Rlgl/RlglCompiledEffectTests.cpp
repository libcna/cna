// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-047/RLGL-048: RLGL runs the same public compiled-Effect contracts as
// the established renderers, with later draw families remaining assigned to RLGL-049.

#if defined(CNA_RLGL_COMPILED_EFFECTS)

#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <gtest/gtest.h>

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
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
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
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();

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
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
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
        compiled.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
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
