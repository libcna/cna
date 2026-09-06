// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-167..171: the WebGPU compiled-effect runtime.
//
// Like Vulkan (`plans/plan_fx.md` `FX-065`), this backend has no MojoShader-provided adapter: the
// nine-function effect context, the constant register files and the uniform packing are all code in
// this repository, so they need their own evidence rather than borrowed trust. Unlike Vulkan it also
// REWRITES the emitted SPIR-V -- WGSL has no combined image sampler -- so the tests that matter most
// here are the ones that put real pixels through that rewrite.
//
// The runtime-level tests go through `WebGPURenderer::CreateCompiledEffect` DIRECTLY, so they keep
// testing the backend rather than the layer above it. The shared cross-renderer contracts go through
// the public `Effect`/`GraphicsDevice` API instead, which is what makes them the same evidence
// FNA3D, SDL_GPU, EasyGL and Vulkan produce.

#if defined(CNA_WEBGPU_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/WebGPU/WebGPUCompiledEffect.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "CNA/Internal/Renderers/MojoShader/SpirvCombinedSamplerSplit.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "System/NotSupportedException.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using CNA::Internal::Renderers::CompiledEffectDeviceState;
    using CNA::Internal::Renderers::CompiledEffectPassStateChanges;
    using CNA::Internal::Renderers::ICompiledEffectRuntime;
    using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

    /// Reads a committed fixture. They live with the FNA3D renderer, which owns their provenance.
    std::vector<std::uint8_t> LoadEffect(const std::string& name)
    {
        const std::filesystem::path path = CNA::TestSupport::CompiledEffectDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    WebGPURenderer* RendererOf(GraphicsDevice& device)
    {
        return dynamic_cast<WebGPURenderer*>(&device.GetRenderer());
    }

    std::unique_ptr<ICompiledEffectRuntime> CreateRuntime(GraphicsDevice& device,
                                                          const std::vector<std::uint8_t>& bytes)
    {
        WebGPURenderer* renderer = RendererOf(device);
        if (renderer == nullptr || bytes.empty()) return nullptr;
        return renderer->CreateCompiledEffect(bytes.data(), bytes.size());
    }
}

TEST(WebGPUCompiledEffectTest, TheCapabilityIsTrueAndThePublicBoundaryAcceptsBytecode)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the WebGPU renderer";
    // plans/plan_webgpu.md WEBGPU-171: true only because the draw route exists. The two report
    // false/true together on purpose -- a capability that says true while a compiled draw falls
    // through to a stock shader is exactly the defect FX-080 removed from the other backends.
    EXPECT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
    EXPECT_NO_THROW(Microsoft::Xna::Framework::Graphics::Effect(
        device, CNA::TestSupport::BuildSyntheticDrawableEffect()));
}

TEST(WebGPUCompiledEffectTest, EveryCommittedStockEffectTranslatesAndSplits)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the WebGPU renderer";
    // The stock binaries Microsoft compiled, plus CNA's own conformance source and the two real
    // XNA 4.0 game effects. Parsing each one through this renderer's backend is what proves the
    // SPIR-V profile plus the ps_1_x MojoShader patches carry the whole committed corpus.
    for (const char* name : {"CnaConformanceEffect.fxb", "SpriteEffect.fxb", "BasicEffect.fxb",
                             "AlphaTestEffect.fxb", "DualTextureEffect.fxb",
                             "EnvironmentMapEffect.fxb", "SkinnedEffect.fxb"})
    {
        const std::vector<std::uint8_t> bytes = LoadEffect(name);
        ASSERT_FALSE(bytes.empty()) << name;
        std::unique_ptr<ICompiledEffectRuntime> runtime;
        ASSERT_NO_THROW(runtime = CreateRuntime(device, bytes)) << name;
        ASSERT_NE(runtime, nullptr) << name;
        EXPECT_FALSE(runtime->GetDescription().techniques.empty()) << name;
    }
}

TEST(WebGPUCompiledEffectTest, RealXna4GameEffectsWithShaderModel1PixelShadersTranslate)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the WebGPU renderer";
    // plans/plan_webgpu.md WEBGPU-166: these two failed to parse at all until CNA's
    // mojoshader-6333f74-spirv-texcrd.patch, and then produced an illegal entry-point interface
    // until mojoshader-6333f74-spirv-ps1x-interface.patch. They are the regression guard for both.
    for (const char* name : {"racing-shadow-map-xna4.fxb", "racing-normal-mapping-xna4.fxb"})
    {
        // These two are extracted game content, so they live with the fixtures rather than with
        // the stock binaries.
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectFixtureDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        ASSERT_TRUE(input.good()) << path;
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                              std::istreambuf_iterator<char>()};
        ASSERT_FALSE(bytes.empty()) << name;
        std::unique_ptr<ICompiledEffectRuntime> runtime;
        ASSERT_NO_THROW(runtime = CreateRuntime(device, bytes)) << name;
        ASSERT_NE(runtime, nullptr) << name;
        EXPECT_FALSE(runtime->GetDescription().techniques.empty()) << name;
    }
}

TEST(WebGPUCompiledEffectTest, TheSpirvSplitLeavesAModuleWithoutSamplersAlone)
{
    // A module that declares no combined image sampler must come back byte-identical: the rewrite
    // is not allowed to renumber or reorder anything it was not asked to change.
    using CNA::Internal::Renderers::MojoShaderEffect::SplitCombinedImageSamplers;
    // A minimal well-formed header plus one OpTypeVoid, which the walk must traverse and emit back.
    const std::vector<std::uint32_t> module{
        0x07230203u, 0x00010000u, 0u, 3u, 0u,
        (2u << 16) | 19u, 2u};
    const auto result = SplitCombinedImageSamplers(module.data(), module.size());
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(result.samplers.empty());
    EXPECT_EQ(result.words, module);
}

TEST(WebGPUCompiledEffectTest, TheSpirvSplitRefusesSomethingThatIsNotSpirv)
{
    using CNA::Internal::Renderers::MojoShaderEffect::SplitCombinedImageSamplers;
    const std::vector<std::uint32_t> notSpirv{1u, 2u, 3u, 4u, 5u};
    const auto result = SplitCombinedImageSamplers(notSpirv.data(), notSpirv.size());
    EXPECT_FALSE(result.error.empty());
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.words, notSpirv);
}

TEST(WebGPUCompiledEffectTest, SharedBackendConformanceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedSamplerPixelContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::CompiledEffectSamplerContractOptions options;
    // plans/plan_webgpu.md WEBGPU-161: `SamplerState.MipMapLevelOfDetailBias` has nowhere to go on
    // this renderer, and that is a property of WebGPU rather than of this compiled-effect route:
    // `WGPUSamplerDescriptor` carries `lodMinClamp` and `lodMaxClamp` and no bias field at all --
    // the string "lodBias" does not appear anywhere in the pinned webgpu.h. Every stock draw
    // family discards the value for the same reason, so this is a renderer-wide gap, not an FX one.
    // `MaxMipLevel` stays true: WEBGPU-161 expresses it through `lodMinClamp`.
    options.supportsLodBias = false;
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device, options);
}

TEST(WebGPUCompiledEffectDrawTest, SharedPassSelectionContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedManyDrawsContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    // plans/plan_fx.md FX-112: 600 compiled draws, which on this renderer means 600 transient
    // uniform-buffer acquisitions crossing the pool's recycle point (WEBGPU-12/59).
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(WebGPUCompiledEffectDrawTest, SharedTruncationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}


// plans/plan_webgpu.md WEBGPU-160: `SamplerState.AddressW`, measured in PIXELS.
//
// This is the acceptance the row could not meet before: "three modes, three different readbacks".
// `AddressW` is observable only where a renderer samples a VOLUME texture, and until the compiled
// Effect route existed this renderer sampled one nowhere -- so `WebGpuSamplerAddressWTests.cpp`
// could only count native samplers and prove the state ARRIVED. A compiled effect declaring
// `sampler3D` is an ordinary XNA surface (no CNAEXT volume API was added for this), and its
// `sampler_state` block carries `AddressW`, which is what makes the axis visible here.
//
// The texture is four depth slices of one colour each, so the texel centres sit at
// w = 0.125, 0.375, 0.625, 0.875. A coordinate of w = 1.375 separates all three modes:
//   Wrap   -> 0.375  -> slice 1 (green)
//   Clamp  -> 0.875  -> slice 3 (white)
//   Mirror -> 0.625  -> slice 2 (blue)
// A renderer that dropped W -- or that hardcoded one mode, which is what this one used to do --
// returns the same colour three times and fails on the second leg.
TEST(WebGPUCompiledEffectDrawTest, AddressWSelectsADifferentVolumeSliceForEachMode)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    using CNA::TestSupport::SamplingQuadVertexXYZ;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector4;

    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    constexpr int kSize = 8;
    const Color background(9, 19, 29, 255);
    const auto declaration = CNA::TestSupport::SamplingQuadDeclarationXYZ();

    const Color slices[4] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                             Color(0, 0, 255, 255), Color(255, 255, 255, 255)};
    Texture3D volume(device, 1, 1, 4, /*mipMap=*/false, SurfaceFormat::Color);
    volume.SetData(slices, 4);

    const auto sampleWithAddressW = [&](int addressW) {
        Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            {
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
                {Fx::SampAddressW, addressW},
            },
            0, CNA::TestSupport::SyntheticSamplerKind::Sampler3D));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        parameters["FxTexture"]->SetValue(&volume);

        SamplingQuadVertexXYZ quad[6];
        CNA::TestSupport::FillSamplingQuadXYZ(quad, 0.5f, 0.5f, 1.375f);

        RenderTarget2D target(device, kSize, kSize);
        device.SetRenderTarget(&target);
        device.Clear(background);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(quad), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    };

    const Color wrapped = sampleWithAddressW(Fx::AddressWrap);
    const Color clamped = sampleWithAddressW(Fx::AddressClamp);
    const Color mirrored = sampleWithAddressW(Fx::AddressMirror);

    const auto expectSlice = [](const Color& actual, const Color& expected, const char* label) {
        SCOPED_TRACE(label);
        EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
        EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
        EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
    };
    expectSlice(wrapped, slices[1], "Wrap turns w = 1.375 into 0.375, the second slice");
    expectSlice(clamped, slices[3], "Clamp turns w = 1.375 into the last slice");
    expectSlice(mirrored, slices[2], "Mirror turns w = 1.375 into 0.625, the third slice");

    // The three readbacks must genuinely differ; three equal colours would mean the axis was
    // dropped and every EXPECT above happened to be satisfied by one mode's answer.
    EXPECT_FALSE(wrapped == clamped);
    EXPECT_FALSE(clamped == mirrored);
    EXPECT_FALSE(wrapped == mirrored);
}

#endif  // CNA_WEBGPU_COMPILED_EFFECTS
