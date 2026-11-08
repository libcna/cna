// SPDX-License-Identifier: MS-PL
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include <cstdlib>
#include <string>

namespace
{
    using namespace CNA::TestSupport;
    using namespace Microsoft::Xna::Framework::Graphics;

    template<class Callback>
    void RunGap13Corpus(Callback callback, const char* directX9Refusal = nullptr)
    {
        if (CNA::GraphicsRendererSelection::GetSelected() == CNA::GraphicsRendererType::Fna3d)
            GTEST_SKIP() << "GAP-13 promotion gate covers optional families; FNA3D keeps its driver corpus";
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(64, 64));
        if (const char* required = std::getenv("CNA_GAP13_REQUIRE_CORPUS"))
        {
            ASSERT_EQ(std::string(required), "1");
            ASSERT_TRUE(SupportsCompiledEffects(device)) << "an enabled qualification cannot skip";
        }
        else if (!SupportsCompiledEffects(device))
            GTEST_SKIP() << "disabled route; the GAP-13 policy/refusal cases cover this build";
        const auto selected = CNA::GraphicsRendererSelection::GetSelected();
        if (selected == CNA::GraphicsRendererType::DirectX9 && directX9Refusal)
        {
            try { callback(device); FAIL() << "documented native profile refusal disappeared"; }
            catch (const std::runtime_error& error)
            {
                EXPECT_NE(std::string(error.what()).find(directX9Refusal), std::string::npos)
                    << error.what();
            }
        }
        else callback(device);
    }
}

// A separate 2D-only replay reaches FNA3D as well as the optional renderer families.
// It does not change the 44-case promotion corpus or imply legacy cube/volume inference.
TEST(CompiledEffectLegacySampledTextureTest, FiveInstructionsMatchLiteralForkFixtures)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64, 64));
    if (const char* required = std::getenv("CNA_GAP13_REQUIRE_CORPUS"))
    {
        ASSERT_EQ(std::string(required), "1");
        ASSERT_TRUE(SupportsCompiledEffects(device)) << "an enabled qualification cannot skip";
    }
    else if (!SupportsCompiledEffects(device))
        GTEST_SKIP() << "disabled route; the GAP-13 policy/refusal cases cover this build";
    for (const auto remap : {SyntheticLegacyTextureRemap::AlphaRed,
                             SyntheticLegacyTextureRemap::GreenBlue})
        RunCompiledEffectLegacySampledTextureContract(device, remap);
    for (const auto dependent : {SyntheticLegacyDependentTexture::RegisterRgb,
                                 SyntheticLegacyDependentTexture::DotSample,
                                 SyntheticLegacyDependentTexture::Dot})
        RunCompiledEffectLegacySampledTextureContract(
            device, SyntheticLegacyTextureRemap::None, dependent);
}

// A standalone public replay also qualifies FNA3D's driver routes without changing the corpus.
TEST(CompiledEffectLegacyMatrixDepthTest, PlainSignedMixedAndZeroMatchLiteralForkFixtures)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64, 64));
    if (const char* required = std::getenv("CNA_GAP13_REQUIRE_CORPUS"))
    {
        ASSERT_EQ(std::string(required), "1");
        ASSERT_TRUE(SupportsCompiledEffects(device)) << "an enabled qualification cannot skip";
    }
    else if (!SupportsCompiledEffects(device))
        GTEST_SKIP() << "disabled route; the GAP-13 policy/refusal cases cover this build";
    RunCompiledEffectLegacyMatrixDepthLiteralContract(device);
}

// Each callee contains its original independent oracle and exact pixel/state expectations.
// Keep every existing backend assertion; these call sites ensure the current shared list runs
// even where an older per-backend file never registered the section.
TEST(Gap13CompiledEffectCorpusTest, Draw)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectDrawContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, Loop)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLoopContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, Subroutine)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSubroutineContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, Predication)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectPredicationContract(device); },
        "PREDICATION unsupported by DirectX9 compiled-effect bytecode profile");
}

TEST(Gap13CompiledEffectCorpusTest, PredicatedTexkill)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectPredicatedTexkillContract(device); },
        "TEXKILL predication unsupported by DirectX9 compiled-effect bytecode profile");
}

TEST(Gap13CompiledEffectCorpusTest, ShaderModel14TexcrdDw)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectShaderModel14TexcrdDwContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, ShaderModel14TexldDz)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectShaderModel14TexldDzContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, ShaderModel14TexldDw)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectShaderModel14TexldDwContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, ShaderModel14TextureLoad)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectShaderModel14TextureLoadContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, ShaderModel14Phase)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectShaderModel14PhaseContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyTextureMatrix)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyTextureMatrixContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyTextureMatrix2)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyTextureMatrix2Contract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyTextureMatrix3Sample)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyTextureMatrix3SampleContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyTextureMatrix3Specular)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyTextureMatrix3SpecularContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyDepthOutput)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyDepthOutputContract(device); });
}

// Keep TEXDEPTH's independent register-depth control in the unchanged 44-case corpus.
TEST(Gap13CompiledEffectCorpusTest, LegacyRegisterDepthOutput)
{
    RunGap13Corpus([](GraphicsDevice& device) {
        RunCompiledEffectLegacyDepthOutputContract(device, /*registerOnly=*/true);
    });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyTextureRemap)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyTextureRemapContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyDependentTexture)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyDependentTextureContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LegacyBumpEnvironment)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectLegacyBumpEnvironmentContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, MultiStreamDraw)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectMultiStreamDrawContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, InstancingDraw)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectInstancingDrawContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, SpriteBatch)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSpriteBatchContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, SpriteBatchMultiPass)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSpriteBatchMultiPassContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, SpriteBatchTextureSlot)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSpriteBatchTextureSlotContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, SamplerPixel)
{
    RunGap13Corpus([](GraphicsDevice& device) {
        CompiledEffectSamplerContractOptions options;
        const auto selected = CNA::GraphicsRendererSelection::GetSelected();
        // OpenGL ES/WebGL expose no per-texture LOD bias (docs/sampler-state-support.md).
        options.supportsLodBias = selected != CNA::GraphicsRendererType::OpenGLES3 &&
                                  selected != CNA::GraphicsRendererType::WebGL2;
        RunCompiledEffectSamplerPixelContract(device, options);
    });
}

TEST(Gap13CompiledEffectCorpusTest, SamplerResultSwizzle)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSamplerResultSwizzleContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, PassSelection)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectPassSelectionContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, RenderTargetSource)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectRenderTargetSourceContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, SpriteBatchRenderTargetSource)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSpriteBatchRenderTargetSourceContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, StockDrawIsolation)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectStockDrawIsolationContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, StockLayoutIsolation)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectStockLayoutIsolationContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, Orientation)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectOrientationContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, Switching)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSwitchingContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, ManyDraws)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectManyDrawsContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, Truncation)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectTruncationContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, HalfPixelCoverage)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectHalfPixelCoverageContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, DeviceTextureSlotAfterApply)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectDeviceTextureSlotAfterApplyContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, VertexTextureSingle)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectVertexTextureContract(device, SurfaceFormat::Single); });
}

TEST(Gap13CompiledEffectCorpusTest, VertexTextureVector4)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectVertexTextureContract(device, SurfaceFormat::Vector4); });
}

TEST(Gap13CompiledEffectCorpusTest, MrtWriteMask)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectMrtWriteMaskContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, LodBiasRegister)
{
    RunGap13Corpus([](GraphicsDevice& device) {
        CompiledEffectSamplerContractOptions options;
        const auto selected = CNA::GraphicsRendererSelection::GetSelected();
        // OpenGL ES/WebGL expose no per-texture LOD bias (docs/sampler-state-support.md).
        options.supportsLodBias = selected != CNA::GraphicsRendererType::OpenGLES3 &&
                                  selected != CNA::GraphicsRendererType::WebGL2;
        RunCompiledEffectLodBiasRegisterContract(device, options);
    });
}

TEST(Gap13CompiledEffectCorpusTest, LodBiasPerDraw)
{
    RunGap13Corpus([](GraphicsDevice& device) {
        CompiledEffectSamplerContractOptions options;
        const auto selected = CNA::GraphicsRendererSelection::GetSelected();
        // OpenGL ES/WebGL expose no per-texture LOD bias (docs/sampler-state-support.md).
        options.supportsLodBias = selected != CNA::GraphicsRendererType::OpenGLES3 &&
                                  selected != CNA::GraphicsRendererType::WebGL2;
        RunCompiledEffectLodBiasPerDrawContract(device, options);
    });
}

TEST(Gap13CompiledEffectCorpusTest, AddressW)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectAddressWContract(device); });
}

TEST(Gap13CompiledEffectCorpusTest, SpriteBatchAlternation)
{
    RunGap13Corpus([](GraphicsDevice& device) { RunCompiledEffectSpriteBatchAlternationContract(device); });
}

