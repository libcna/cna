// SPDX-License-Identifier: MS-PL

#if defined(CNA_RENDERER_SOFTWARE) && defined(CNA_SOFTWARE_COMPILED_EFFECTS)

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using Microsoft::Xna::Framework::Graphics::PresentationParameters;

    template<typename Callback>
    void RunOnFreshSoftwareDevice(const char* name, Callback&& callback)
    {
        SCOPED_TRACE(name);
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(800, 480));
        ASSERT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
        callback(device);
    }
}

TEST(SoftwareCompiledEffectConformanceTest, RunsEverySharedCompiledEffectContract)
{
    using namespace CNA::TestSupport;

    RunOnFreshSoftwareDevice("reflection/state/parameter/lifecycle", [](GraphicsDevice& device)
    {
        RunCompiledEffectContract(device);
    });
    RunOnFreshSoftwareDevice("ordinary/indexed draw", [](GraphicsDevice& device)
    {
        RunCompiledEffectDrawContract(device);
    });
    RunOnFreshSoftwareDevice("loop", [](GraphicsDevice& device)
    {
        RunCompiledEffectLoopContract(device);
    });
    RunOnFreshSoftwareDevice("signed LOG", [](GraphicsDevice& device)
    {
        RunCompiledEffectSignedLogContract(device);
    });
    RunOnFreshSoftwareDevice("NRM write mask", [](GraphicsDevice& device)
    {
        RunCompiledEffectNrmWriteMaskContract(device);
    });
    for (const auto probe : {SyntheticCompositeWriteMaskProbe::VertexDst,
                             SyntheticCompositeWriteMaskProbe::VertexCrs})
    {
        RunOnFreshSoftwareDevice("composite write mask", [probe](GraphicsDevice& device)
        {
            RunCompiledEffectCompositeWriteMaskContract(device, probe);
        });
    }
    RunOnFreshSoftwareDevice("Shader Model 1.1 inputs", [](GraphicsDevice& device)
    {
        RunCompiledEffectShaderModel11InputContract(device);
        RunCompiledEffectShaderModel11ExtendedInputContract(device);
        RunCompiledEffectLegacyExppContract(device);
    });
    RunOnFreshSoftwareDevice("relative/dependent coordinates", [](GraphicsDevice& device)
    {
        RunCompiledEffectRelativeInputTextureCoordinateContract(device);
        RunCompiledEffectDependentTemporaryTextureCoordinateContract(device);
        RunCompiledEffectDependentTemporaryTextureCoordinate3DContract(device);
    });
    RunOnFreshSoftwareDevice("subroutines/derivatives/raster inputs", [](GraphicsDevice& device)
    {
        RunCompiledEffectSubroutineContract(device);
        RunCompiledEffectDerivativeContract(device);
        RunCompiledEffectRasterInputContract(device);
    });
    RunOnFreshSoftwareDevice("predication", [](GraphicsDevice& device)
    {
        RunCompiledEffectPredicationContract(device);
        RunCompiledEffectPredicatedTexkillContract(device);
    });
    RunOnFreshSoftwareDevice("Shader Model 1.4", [](GraphicsDevice& device)
    {
        RunCompiledEffectShaderModel14TexcrdDwContract(device);
        RunCompiledEffectShaderModel14TexldDzContract(device);
        RunCompiledEffectShaderModel14TexldDwContract(device);
        RunCompiledEffectShaderModel14TextureLoadContract(device);
        RunCompiledEffectShaderModel14PhaseContract(device);
    });
    RunOnFreshSoftwareDevice("legacy texture families", [](GraphicsDevice& device)
    {
        RunCompiledEffectLegacyTextureMatrixContract(device);
        RunCompiledEffectLegacyTextureMatrix2Contract(device);
        RunCompiledEffectLegacyTextureMatrix3SampleContract(device);
        RunCompiledEffectLegacyTextureMatrix3SpecularContract(device);
        RunCompiledEffectLegacyDepthOutputContract(device);
        RunCompiledEffectLegacyTextureRemapContract(device);
        RunCompiledEffectLegacyDependentTextureContract(device);
        RunCompiledEffectLegacyBumpEnvironmentContract(device);
    });
    RunOnFreshSoftwareDevice("multi-stream/instancing", [](GraphicsDevice& device)
    {
        RunCompiledEffectMultiStreamDrawContract(device);
        RunCompiledEffectInstancingDrawContract(device);
    });
    RunOnFreshSoftwareDevice("SpriteBatch", [](GraphicsDevice& device)
    {
        RunCompiledEffectSpriteBatchContract(device);
        RunCompiledEffectSpriteBatchMultiPassContract(device);
        RunCompiledEffectSpriteBatchTextureSlotContract(device);
        RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
    });
    RunOnFreshSoftwareDevice("samplers", [](GraphicsDevice& device)
    {
        RunCompiledEffectSamplerPixelContract(device);
        RunCompiledEffectSamplerResultSwizzleContract(device);
        RunCompiledEffectVertexSamplerContract(device);
        RunCompiledEffectVertexSamplerResultSwizzleContract(device);
        RunCompiledEffectVertexSamplerDimensionsContract(device);
        RunCompiledEffectCubeAndVolumeSamplerContract(device);
    });
    RunOnFreshSoftwareDevice("targets/state isolation", [](GraphicsDevice& device)
    {
        RunCompiledEffectPassSelectionContract(device);
        RunCompiledEffectInheritedVertexStageContract(device);
        RunCompiledEffectRenderTargetSourceContract(device);
        RunCompiledEffectStockDrawIsolationContract(device);
    });
    RunOnFreshSoftwareDevice("orientation/switching/stress", [](GraphicsDevice& device)
    {
        RunCompiledEffectOrientationContract(device);
        RunCompiledEffectSwitchingContract(device);
        RunCompiledEffectManyDrawsContract(device);
        RunCompiledEffectTruncationContract(device);
    });
    // First carried by the retired METAL renderer (AM4-144/146) and by WebGPU (WEBGPU-160/170/208).
    RunOnFreshSoftwareDevice("pixel centres/device slots/MRT masks", [](GraphicsDevice& device)
    {
        RunCompiledEffectHalfPixelCoverageContract(device);
        RunCompiledEffectDeviceTextureSlotAfterApplyContract(device);
        RunCompiledEffectMrtWriteMaskContract(device);
    });
    RunOnFreshSoftwareDevice("per-register/per-draw LOD bias, AddressW, SpriteBatch alternation",
                             [](GraphicsDevice& device)
    {
        RunCompiledEffectLodBiasRegisterContract(device);
        RunCompiledEffectLodBiasPerDrawContract(device);
        RunCompiledEffectAddressWContract(device);
        RunCompiledEffectSpriteBatchAlternationContract(device);
    });
}


// Software keeps no record of the shaders a stock effect or a plain SpriteBatch runs (they are not
// compiled runtimes). XNA would let a later pass without a vertex shader inherit the stock one;
// Software refuses that draw rather than reusing an older compiled vertex shader.
TEST(SoftwareCompiledEffectStageTest, APassWithoutAVertexShaderAfterAStockDrawRefuses)
{
    using namespace CNA::TestSupport;
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64, 64));
    ASSERT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
    Effect effect(device, BuildSyntheticDrawableEffectWithPixelOnlyPass());
    effect.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
    effect.getParametersProperty()["Tint"]->SetValue(Vector4(0.25f, 0.5f, 0.75f, 1.0f));
    struct ClipVertex { float x, y, z; };
    const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    const ClipVertex quad[6] = {
        {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
    };
    VertexPositionColor stockQuad[6];
    for (int i = 0; i < 6; ++i)
        stockQuad[i] = VertexPositionColor(Vector3(quad[i].x, quad[i].y, quad[i].z), Color::White);

    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2, declaration);

    BasicEffect stock(device);
    stock.setVertexColorEnabledProperty(true);
    stock.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, stockQuad, 0, 2);

    effect.setCurrentTechniqueProperty(effect.getTechniquesProperty()[1]);
    effect.getTechniquesProperty()[1]->getPassesProperty()[0]->Apply();
    EXPECT_THROW(device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad),
                                           0, 2, declaration),
                 System::InvalidOperationException);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
}

// An Immediate SpriteBatch with a pixel-only effect, interrupted by a stock draw between two
// sprites: the second sprite still draws, with SpriteEffect's vertex stage applied again, instead of
// refusing for want of a vertex shader (review rv-stage2 F1).
TEST(SoftwareCompiledEffectStageTest, AnImmediatePixelOnlyBatchSurvivesAStockDrawBetweenSprites)
{
    using namespace CNA::TestSupport;
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64, 64));
    ASSERT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
    Effect effect(device, BuildSyntheticPixelOnlySamplingEffect({}));
    effect.getParametersProperty()["Tint"]->SetValue(Vector4::One);
    Texture2D white(device, 1, 1);
    const Color texel[1] = {Color::White};
    white.SetData(texel, 1);
    VertexPositionColor stockQuad[6];
    const Vector3 corners[6] = {{-1, 1, 0}, {-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, 0}};
    for (int i = 0; i < 6; ++i) stockQuad[i] = VertexPositionColor(corners[i], Color::Black);

    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, &effect);
    batch.Draw(white, Rectangle(0, 0, 4, 4), Color::White);
    BasicEffect stock(device);
    stock.setVertexColorEnabledProperty(true);
    stock.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, stockQuad, 0, 2);
    EXPECT_NO_THROW(batch.Draw(white, Rectangle(4, 4, 4, 4), Color::White));
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    Color pixel = Color::Transparent;
    const Rectangle probe(6, 6, 1, 1);
    target.GetData(0, &probe, &pixel, 0, 1);
    EXPECT_EQ(pixel, Color::White) << "the second sprite did not draw";
}

#endif
