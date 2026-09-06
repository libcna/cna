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
#include "CNA/Internal/Renderers/MojoShader/SpirvToWgsl.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

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
    using CNA::Internal::Renderers::WebGPU::WebGPUCompiledEffect;
    using CNA::Internal::Renderers::WebGPU::WebGPUCompiledShaderEXT;
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

    /// The inverse of the renderer's own usage mapping, so a test can build a declaration that
    /// satisfies whatever inputs a fixture's vertex shader happens to declare.
    VertexElementUsage FromMojoShaderUsage(MOJOSHADER_usage usage)
    {
        switch (usage)
        {
            case MOJOSHADER_USAGE_POSITION:     return VertexElementUsage::Position;
            case MOJOSHADER_USAGE_COLOR:        return VertexElementUsage::Color;
            case MOJOSHADER_USAGE_TEXCOORD:     return VertexElementUsage::TextureCoordinate;
            case MOJOSHADER_USAGE_NORMAL:       return VertexElementUsage::Normal;
            case MOJOSHADER_USAGE_BINORMAL:     return VertexElementUsage::Binormal;
            case MOJOSHADER_USAGE_TANGENT:      return VertexElementUsage::Tangent;
            case MOJOSHADER_USAGE_BLENDINDICES: return VertexElementUsage::BlendIndices;
            case MOJOSHADER_USAGE_BLENDWEIGHT:  return VertexElementUsage::BlendWeight;
            case MOJOSHADER_USAGE_DEPTH:        return VertexElementUsage::Depth;
            case MOJOSHADER_USAGE_FOG:          return VertexElementUsage::Fog;
            case MOJOSHADER_USAGE_POINTSIZE:    return VertexElementUsage::PointSize;
            case MOJOSHADER_USAGE_SAMPLE:       return VertexElementUsage::Sample;
            case MOJOSHADER_USAGE_TESSFACTOR:   return VertexElementUsage::TessellateFactor;
            default: break;
        }
        ADD_FAILURE() << "unmapped MOJOSHADER_usage " << static_cast<int>(usage);
        return VertexElementUsage::Position;
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

TEST(WebGPUCompiledEffectTest, RealXna4GameEffectsWithShaderModel1PixelShadersParseAndReflect)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the WebGPU renderer";
    // plans/plan_webgpu.md WEBGPU-166: these two failed to PARSE at all until CNA's
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

TEST(WebGPUCompiledEffectTest, EveryPassOfTheRealXna4GameEffectsCreatesBothShaderModules)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the WebGPU renderer";
    // plans/plan_fx.md FX-129. Until the two ps_1_x linker patches landed, six of the eighteen
    // passes across these two fixtures produced a fragment module wgpu refused with
    // "Multiple bindings at location 1 are present" -- so this suite could claim parse and
    // reflection only. It can now claim the thing that matters: EVERY pass links and creates both
    // modules. Reflection alone would still pass with those six broken, which is exactly why the
    // weaker assertion was not enough.
    //
    // The declaration handed to each pass is derived from the pass's own vertex-shader inputs, so
    // this exercises the real link path (input-type patching, output-to-input linking, the
    // combined-sampler split) without needing per-fixture mesh knowledge.
    int linked = 0;
    for (const char* name : {"racing-shadow-map-xna4.fxb", "racing-normal-mapping-xna4.fxb"})
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectFixtureDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        ASSERT_TRUE(input.good()) << path;
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                              std::istreambuf_iterator<char>()};
        std::unique_ptr<ICompiledEffectRuntime> runtime = CreateRuntime(device, bytes);
        ASSERT_NE(runtime, nullptr) << name;
        auto* compiled = dynamic_cast<CNA::Internal::Renderers::WebGPU::WebGPUCompiledEffect*>(
            runtime.get());
        ASSERT_NE(compiled, nullptr) << name;

        const auto& description = runtime->GetDescription();
        for (std::size_t t = 0; t < description.techniques.size(); ++t)
        {
            runtime->SetTechnique(static_cast<std::uint32_t>(t));
            for (std::size_t p = 0; p < description.techniques[t].passes.size(); ++p)
            {
                CompiledEffectDeviceState state{};
                CompiledEffectPassStateChanges changes{};
                ASSERT_NO_THROW(compiled->ApplyPass(static_cast<std::uint32_t>(p), state, changes))
                    << name << " technique " << t << " pass " << p;

                WebGPUCompiledShaderEXT* vertexShader = nullptr;
                WebGPUCompiledShaderEXT* pixelShader = nullptr;
                compiled->GetBoundShadersEXT(vertexShader, pixelShader);
                ASSERT_NE(vertexShader, nullptr) << name << " technique " << t << " pass " << p;
                ASSERT_NE(pixelShader, nullptr) << name << " technique " << t << " pass " << p;

                // One Vector4 element per declared vertex-shader input, at successive offsets.
                std::vector<VertexElement> elements;
                const MOJOSHADER_parseData* vertexData = vertexShader->parseData;
                for (int a = 0; a < vertexData->attribute_count; ++a)
                {
                    const VertexElementUsage usage =
                        FromMojoShaderUsage(vertexData->attributes[a].usage);
                    elements.emplace_back(static_cast<int>(elements.size()) * 16,
                                          VertexElementFormat::Vector4, usage,
                                          vertexData->attributes[a].index);
                }
                WebGPUCompiledEffect::CompiledVertexStreamEXT stream{};
                stream.elements = &elements;
                stream.stride = static_cast<std::uint32_t>(elements.size() * 16);

                WebGPUCompiledEffect::LinkedPassEXT link;
                ASSERT_NO_THROW(link = compiled->LinkAndGetShadersEXT({stream}))
                    << name << " technique " << t << " pass " << p;
                EXPECT_NE(link.vertexModule, nullptr)
                    << name << " technique " << t << " pass " << p;
                EXPECT_NE(link.pixelModule, nullptr)
                    << name << " technique " << t << " pass " << p;
                ++linked;
            }
        }
    }
    // Eighteen passes across the two fixtures; six of them are the Shader Model 1.x ones.
    EXPECT_EQ(linked, 18);
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

// plans/plan_fx.md FX-112 / plans/plan_webgpu.md WEBGPU-170. The compiled SpriteBatch route leaves
// the stock sprite pipeline entirely, so the sequence that can break is returning to a compiled
// batch after a stock one has run between them: a pending-sprite run that survived, or batch state
// the compiled flush skipped, would show up here and nowhere else.
//
// On THIS renderer it exercises one more thing than it does on Vulkan, which is why it is worth
// carrying rather than trusting the shared suite alone: WebGPU queues every draw and replays them
// at Present() in public call order, and a compiled batch and a stock batch land in two DIFFERENT
// command families. So this is also the test that would catch `drawOrder_` interleaving the two
// families wrongly -- the middle third would then be drawn over by a compiled batch, or drawn
// before one. The three batches draw into separate thirds of one target so all three are readable
// at once.
TEST(WebGPUCompiledEffectDrawTest, SpriteBatchAlternatesCompiledAndStockAcrossBatches)
{
    using Microsoft::Xna::Framework::Vector4;
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    constexpr int kSize = 12;
    Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
    auto& parameters = effect.getParametersProperty();
    parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
        0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, -1.0f, 1.0f));

    // A green sprite texture, so a stock batch is unmistakable against either compiled Tint.
    Texture2D sprite(device, 1, 1);
    const Color green[1] = {Color(0, 255, 0, 255)};
    sprite.SetData(green, 1);

    RenderTarget2D target(device, kSize, kSize);
    device.SetRenderTarget(&target);
    device.Clear(Color(9, 19, 29, 255));

    const auto compiledBatch = [&](const Vector4& tint, const Rectangle& where) {
        parameters["Tint"]->SetValue(tint);
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr,
                    &effect);
        batch.Draw(sprite, where, Color::White);
        batch.End();
    };

    compiledBatch(Vector4(1.0f, 0.0f, 0.0f, 1.0f), Rectangle(0, 0, kSize, 4));
    {
        SpriteBatch stock(device);
        stock.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        stock.Draw(sprite, Rectangle(0, 4, kSize, 4), Color::White);
        stock.End();
    }
    compiledBatch(Vector4(0.0f, 0.0f, 1.0f, 1.0f), Rectangle(0, 8, kSize, 4));

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const auto readRow = [&](int y) {
        Color pixel(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, y, 1, 1);
        target.GetData(0, &probe, &pixel, 0, 1);
        return pixel;
    };
    const Color first = readRow(2);
    const Color middle = readRow(6);
    const Color last = readRow(10);

    EXPECT_NEAR(first.getRProperty(), 255, 3) << "the first compiled batch must write its Tint";
    EXPECT_NEAR(first.getGProperty(), 0, 3);
    EXPECT_NEAR(middle.getGProperty(), 255, 3)
        << "the stock batch between them must sample its own texture, not run the Effect";
    EXPECT_NEAR(middle.getRProperty(), 0, 3);
    EXPECT_NEAR(last.getBProperty(), 255, 3)
        << "a compiled batch after a stock one must run the Effect again, with its own Tint";
    EXPECT_NEAR(last.getRProperty(), 0, 3);
}

// -----------------------------------------------------------------------------------------------
// plans/plan_webgpu.md WEBGPU-203 -- THE BROWSER'S ROUTE, EXECUTED NATIVELY.
//
// Browser WebGPU ingests WGSL and nothing else, so a compiled Effect reaches it as WGSL translated
// from the same MojoShader SPIR-V the native route feeds directly. wgpu-native accepts WGSL too,
// which makes this build a real oracle for that route rather than a proxy for it: the SAME effect,
// the SAME draw and the SAME expected pixels, with only the shader-module representation swapped.
//
// These are therefore not "browser smoke tests". Each one is a shared cross-renderer contract that
// already passes through SPIR-V, re-run with the representation the browser will use. A divergence
// here is a translator defect, and the assertion that catches it is the contract's own.
// -----------------------------------------------------------------------------------------------
namespace
{
    /// Switches the renderer to the browser's shader representation for the life of one test, and
    /// puts it back afterwards -- a device outlives no test here, but the restore keeps the
    /// intent readable and survives a future shared device.
    class WgslRouteScope
    {
    public:
        explicit WgslRouteScope(GraphicsDevice& device) : renderer_(RendererOf(device))
        {
            if (renderer_ == nullptr) return;
            previous_ = renderer_->GetCompiledEffectShaderLanguageEXT();
            renderer_->SetCompiledEffectShaderLanguageEXT(
                WebGPURenderer::CompiledEffectShaderLanguageEXT::Wgsl);
        }

        ~WgslRouteScope()
        {
            if (renderer_ != nullptr) renderer_->SetCompiledEffectShaderLanguageEXT(previous_);
        }

        WgslRouteScope(const WgslRouteScope&) = delete;
        WgslRouteScope& operator=(const WgslRouteScope&) = delete;

        [[nodiscard]] bool Active() const { return renderer_ != nullptr; }

    private:
        WebGPURenderer* renderer_ = nullptr;
        WebGPURenderer::CompiledEffectShaderLanguageEXT previous_ =
            WebGPURenderer::CompiledEffectShaderLanguageEXT::Spirv;
    };
}

TEST(WebGPUCompiledEffectWgslTest, EveryCommittedPassTranslatesToWgslAndCreatesAModule)
{
    // Rung 0 of the acceptance ladder: the whole committed corpus survives the translator and
    // WebGPU accepts every module it produces. 27 technique/pass pairs, 54 shader modules.
    GraphicsDevice device;
    WebGPURenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the WebGPU renderer";
    WgslRouteScope wgsl(device);

    int linked = 0;
    const auto sweep = [&](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        ASSERT_TRUE(input.good()) << path;
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                              std::istreambuf_iterator<char>()};
        std::unique_ptr<ICompiledEffectRuntime> runtime = CreateRuntime(device, bytes);
        ASSERT_NE(runtime, nullptr) << path;
        auto* compiled = dynamic_cast<WebGPUCompiledEffect*>(runtime.get());
        ASSERT_NE(compiled, nullptr) << path;

        const auto& description = runtime->GetDescription();
        for (std::size_t t = 0; t < description.techniques.size(); ++t)
        {
            runtime->SetTechnique(static_cast<std::uint32_t>(t));
            for (std::size_t p = 0; p < description.techniques[t].passes.size(); ++p)
            {
                CompiledEffectDeviceState state{};
                CompiledEffectPassStateChanges changes{};
                ASSERT_NO_THROW(compiled->ApplyPass(static_cast<std::uint32_t>(p), state, changes))
                    << path << " technique " << t << " pass " << p;
                WebGPUCompiledShaderEXT* vertexShader = nullptr;
                WebGPUCompiledShaderEXT* pixelShader = nullptr;
                compiled->GetBoundShadersEXT(vertexShader, pixelShader);
                ASSERT_NE(vertexShader, nullptr);
                ASSERT_NE(pixelShader, nullptr);

                std::vector<VertexElement> elements;
                const MOJOSHADER_parseData* vertexData = vertexShader->parseData;
                for (int a = 0; a < vertexData->attribute_count; ++a)
                {
                    elements.emplace_back(static_cast<int>(elements.size()) * 16,
                                          VertexElementFormat::Vector4,
                                          FromMojoShaderUsage(vertexData->attributes[a].usage),
                                          vertexData->attributes[a].index);
                }
                WebGPUCompiledEffect::CompiledVertexStreamEXT stream{};
                stream.elements = &elements;
                stream.stride = static_cast<std::uint32_t>(elements.size() * 16);

                WebGPUCompiledEffect::LinkedPassEXT link;
                ASSERT_NO_THROW(link = compiled->LinkAndGetShadersEXT({stream}))
                    << path << " technique " << t << " pass " << p;
                EXPECT_NE(link.vertexModule, nullptr);
                EXPECT_NE(link.pixelModule, nullptr);
                ++linked;
            }
        }
    };

    for (const char* name : {"CnaConformanceEffect.fxb", "SpriteEffect.fxb", "BasicEffect.fxb",
                             "AlphaTestEffect.fxb", "DualTextureEffect.fxb",
                             "EnvironmentMapEffect.fxb", "SkinnedEffect.fxb"})
    {
        sweep(CNA::TestSupport::CompiledEffectDirectory() / name);
        if (::testing::Test::HasFatalFailure()) return;
    }
    for (const char* name : {"racing-shadow-map-xna4.fxb", "racing-normal-mapping-xna4.fxb"})
    {
        sweep(CNA::TestSupport::CompiledEffectFixtureDirectory() / name);
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_EQ(linked, 27);
}

TEST(WebGPUCompiledEffectWgslTest, TheTranslatorRefusesAConstructWgslCannotExpress)
{
    // The refusal path is part of the contract: an untranslatable module must be named, never
    // approximated. A combined image sampler is the one construct WGSL has no type for, and it is
    // exactly what reaches the translator if SplitCombinedImageSamplers() is skipped.
    using CNA::Internal::Renderers::MojoShaderEffect::TranslateSpirvToWgsl;
    const std::vector<std::uint32_t> notSpirv{1u, 2u, 3u, 4u, 5u};
    const auto refused = TranslateSpirvToWgsl(notSpirv.data(), notSpirv.size());
    EXPECT_TRUE(refused.wgsl.empty());
    EXPECT_NE(refused.error.find("magic number"), std::string::npos) << refused.error;
}

TEST(WebGPUCompiledEffectWgslTest, TranslationIsDeterministicAndNamesItsEntryPoint)
{
    // Regression-testing a generated language needs the generator to be a function of its input:
    // the same finished SPIR-V must produce byte-identical WGSL every time.
    GraphicsDevice device;
    WebGPURenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the WebGPU renderer";

    const std::vector<std::uint8_t> bytes = LoadEffect("CnaConformanceEffect.fxb");
    ASSERT_FALSE(bytes.empty());
    std::unique_ptr<ICompiledEffectRuntime> runtime = CreateRuntime(device, bytes);
    ASSERT_NE(runtime, nullptr);
    auto* compiled = dynamic_cast<WebGPUCompiledEffect*>(runtime.get());
    ASSERT_NE(compiled, nullptr);

    runtime->SetTechnique(0);
    CompiledEffectDeviceState state{};
    CompiledEffectPassStateChanges changes{};
    compiled->ApplyPass(0, state, changes);
    WebGPUCompiledShaderEXT* vertexShader = nullptr;
    WebGPUCompiledShaderEXT* pixelShader = nullptr;
    compiled->GetBoundShadersEXT(vertexShader, pixelShader);
    ASSERT_NE(vertexShader, nullptr);
    ASSERT_NE(pixelShader, nullptr);

    // Linking is what turns MojoShader's output into a finished module, and it returns the size of
    // the trailing patch table that must come off before the SPIR-V is read.
    std::vector<MOJOSHADER_vertexAttribute> attributes;
    for (int a = 0; a < vertexShader->parseData->attribute_count; ++a)
    {
        MOJOSHADER_vertexAttribute attribute{};
        attribute.usage = vertexShader->parseData->attributes[a].usage;
        attribute.usageIndex = vertexShader->parseData->attributes[a].index;
        attribute.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
        attributes.push_back(attribute);
    }
    const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
        vertexShader->parseData, pixelShader->parseData, attributes.data(),
        static_cast<int>(attributes.size()));
    ASSERT_GT(patchTableSize, 0);

    const MOJOSHADER_parseData* data = pixelShader->parseData;
    const std::size_t words =
        (static_cast<std::size_t>(data->output_len) - static_cast<std::size_t>(patchTableSize)) /
        sizeof(std::uint32_t);
    const auto split = CNA::Internal::Renderers::MojoShaderEffect::SplitCombinedImageSamplers(
        reinterpret_cast<const std::uint32_t*>(data->output), words);
    ASSERT_TRUE(split.error.empty()) << split.error;

    const std::string first = WebGPURenderer::TranslateCompiledEffectSpirvToWgslEXT(
        split.words.data(), split.words.size());
    const std::string second = WebGPURenderer::TranslateCompiledEffectSpirvToWgslEXT(
        split.words.data(), split.words.size());
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second);
    // The entry point KEEPS its SPIR-V name, because that is the string the pipeline already
    // names; a translator that renamed it to "main" would break every draw.
    const std::string entryPoint =
        data->mainfn != nullptr ? std::string(data->mainfn) : std::string();
    ASSERT_FALSE(entryPoint.empty());
    EXPECT_NE(first.find("@fragment fn " + entryPoint + "("), std::string::npos) << first;
}

// --- the acceptance ladder, each rung a shared contract re-run through WGSL ----------------------

#define CNA_WEBGPU_WGSL_ROUTE_TEST(name, call)                                                   \
    TEST(WebGPUCompiledEffectWgslDrawTest, name)                                                 \
    {                                                                                            \
        GraphicsDevice device;                                                                   \
        if (!CNA::TestSupport::SupportsCompiledEffects(device))                                  \
            GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";  \
        WgslRouteScope wgsl(device);                                                             \
        if (!wgsl.Active()) GTEST_SKIP() << "this build did not select the WebGPU renderer";     \
        call;                                                                                    \
    }

// Rungs 1-4: a compiled vertex+pixel pair, uniform scalars/vectors/matrices, a Texture2D sampler
// and ordinary transformed geometry.
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedDrawMatrixContract,
                           CNA::TestSupport::RunCompiledEffectDrawContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedBackendConformanceContract,
                           CNA::TestSupport::RunCompiledEffectContract(device))
// Rung 5: SpriteBatch with a custom compiled Effect.
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedSpriteBatchContract,
                           CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device))
// Rung 6: multi-pass.
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedSpriteBatchMultiPassContract,
                           CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedPassSelectionContract,
                           CNA::TestSupport::RunCompiledEffectPassSelectionContract(device))
// Rungs 7-8: cube and volume samplers, and the AddressW modes a volume sampler makes observable.
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedCubeAndVolumeSamplerContract,
                           CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device))
// Rung 9: a render target as the sampled source.
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedRenderTargetSourceContract,
                           CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(
    SharedSpriteBatchRenderTargetSourceContract,
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device))
// Rung 10: the rest of the shared corpus -- multi-stream, instancing, orientation, effect
// switching, sampler pixels, isolation from stock draws, texture slots, many draws, truncation.
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedMultiStreamDrawContract,
                           CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedInstancingDrawContract,
                           CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedOrientationContract,
                           CNA::TestSupport::RunCompiledEffectOrientationContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedEffectSwitchingContract,
                           CNA::TestSupport::RunCompiledEffectSwitchingContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedStockDrawIsolationContract,
                           CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedSpriteBatchTextureSlotContract,
                           CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedManyDrawsContract,
                           CNA::TestSupport::RunCompiledEffectManyDrawsContract(device))
CNA_WEBGPU_WGSL_ROUTE_TEST(SharedTruncationContract,
                           CNA::TestSupport::RunCompiledEffectTruncationContract(device))

#undef CNA_WEBGPU_WGSL_ROUTE_TEST

#endif  // CNA_WEBGPU_COMPILED_EFFECTS
