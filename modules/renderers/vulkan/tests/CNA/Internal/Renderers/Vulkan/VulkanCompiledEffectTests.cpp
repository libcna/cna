// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-065: the Vulkan compiled-effect runtime.
//
// This backend is the first with no MojoShader-provided adapter -- the nine-function effect context
// is CNA's own, written against the portable SPIR-V profile (see VulkanCompiledEffect.hpp). Every
// piece the OpenGL and SDL_GPU backends inherit from `mojoshader_opengl.c` / `mojoshader_sdlgpu.c`
// (shader ref-counting, the bound pair, the constant register files, the uniform packing) is code
// in this repository on this renderer, so it needs its own evidence rather than borrowed trust.
//
// The runtime-level tests here go through `VulkanRenderer::CreateCompiledEffect` DIRECTLY rather
// than the public `Effect` class, so they keep testing the backend itself rather than the layer
// above it. The shared cross-renderer contracts at the bottom of this file go through the public
// `Effect`/`GraphicsDevice` API instead, which is what makes them the same evidence FNA3D,
// SDL_GPU and EasyGL produce.

#if defined(CNA_VULKAN_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Vulkan/VulkanCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

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
    using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

    /// Reads a committed fixture. They live with the FNA3D renderer, which owns their provenance.
    std::vector<std::uint8_t> LoadEffect(const std::string& name)
    {
        const std::filesystem::path path = CNA::TestSupport::CompiledEffectDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    VulkanRenderer* RendererOf(GraphicsDevice& device)
    {
        return dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
    }

    std::unique_ptr<ICompiledEffectRuntime> CreateRuntime(GraphicsDevice& device,
                                                          const std::vector<std::uint8_t>& bytes)
    {
        VulkanRenderer* renderer = RendererOf(device);
        if (renderer == nullptr || bytes.empty()) return nullptr;
        return renderer->CreateCompiledEffect(bytes.data(), bytes.size());
    }
}

TEST(VulkanCompiledEffectTest, TheCapabilityIsTrueAndThePublicBoundaryAcceptsBytecode)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Vulkan renderer";
    // plans/plan_fx.md FX-065: true only because the draw route exists. The two report false/true
    // together on purpose -- a capability that says true while a compiled draw falls through to a
    // stock shader is exactly the defect FX-080 removed from the other three backends.
    EXPECT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
    EXPECT_NO_THROW(Microsoft::Xna::Framework::Graphics::Effect(
        device, CNA::TestSupport::BuildSyntheticConformanceEffect({})));
}

TEST(VulkanCompiledEffectTest, ParsesTheCompilerProducedConformanceFixture)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, LoadEffect("CnaConformanceEffect.fxb"));
    ASSERT_NE(runtime, nullptr) << "this build did not select the Vulkan renderer";

    // The reflection every other backend produces for this same binary. Compiled by the same fxc
    // XNA's Content Pipeline used, so agreement here is agreement with a real Effect compiler --
    // through a SPIR-V backend that no other CNA renderer exercises.
    const auto& description = runtime->GetDescription();
    ASSERT_EQ(description.techniques.size(), 2u);
    EXPECT_EQ(description.techniques[0].name, "FirstTechnique");
    EXPECT_EQ(description.techniques[1].name, "SecondTechnique");
    ASSERT_EQ(description.techniques[0].passes.size(), 2u);
    EXPECT_EQ(description.techniques[0].passes[0].name, "P0");
    EXPECT_EQ(description.techniques[0].passes[1].name, "StatePass");

    bool sawGain = false;
    bool sawTint = false;
    bool sawTexture = false;
    for (const auto& parameter : description.parameters)
    {
        if (parameter.name == "Gain")
        {
            sawGain = true;
            EXPECT_EQ(parameter.semantic, "SCALAR");
        }
        if (parameter.name == "Tint") sawTint = true;
        if (parameter.name == "FxTexture") sawTexture = true;
    }
    EXPECT_TRUE(sawGain);
    EXPECT_TRUE(sawTint);
    EXPECT_TRUE(sawTexture);
}

TEST(VulkanCompiledEffectTest, EveryCommittedStockEffectCompilesToSpirv)
{
    // The real load-bearing check for a backend of CNA's own: MojoShader's SPIR-V profile has to
    // accept every shader XNA's stock effects contain, including the multi-variant ones selected
    // by ShaderIndex. A profile gap shows up here as a parse failure, not as a wrong pixel later.
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Vulkan renderer";
    for (const char* name : {"SpriteEffect.fxb", "BasicEffect.fxb", "AlphaTestEffect.fxb",
                             "DualTextureEffect.fxb", "EnvironmentMapEffect.fxb",
                             "SkinnedEffect.fxb"})
    {
        SCOPED_TRACE(name);
        const std::vector<std::uint8_t> bytes = LoadEffect(name);
        ASSERT_FALSE(bytes.empty()) << "fixture not found: " << name;
        auto runtime = CreateRuntime(device, bytes);
        ASSERT_NE(runtime, nullptr);
        EXPECT_FALSE(runtime->GetDescription().techniques.empty());
    }
}

TEST(VulkanCompiledEffectTest, AppliesAPassAndPublishesTheStatesItAssigns)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, LoadEffect("CnaConformanceEffect.fxb"));
    ASSERT_NE(runtime, nullptr) << "this build did not select the Vulkan renderer";

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    // StatePass is the one that assigns render states and a sampler in the fixture's source.
    runtime->ApplyPass(1, deviceState, changes);
    EXPECT_TRUE(changes.depthStencilChanged)
        << "StatePass assigns ZEnable/ZWriteEnable, so a depth-stencil group must be published";
    EXPECT_TRUE(changes.blendChanged)
        << "StatePass assigns AlphaBlendEnable/SrcBlend/DestBlend/BlendOp";
    EXPECT_TRUE(changes.rasterizerChanged) << "StatePass assigns CullMode";
    // The sampler_state block belongs to P0, not StatePass: the fixture's StatePass binds
    // FlatPixelShader, which returns `Tint * Weights[1]` and samples nothing, while P0 binds
    // MainPixelShader, which is the one that reads FxSampler.
    CompiledEffectPassStateChanges sampling;
    runtime->ApplyPass(0, deviceState, sampling);
    ASSERT_FALSE(sampling.samplers.empty())
        << "the fixture's sampler_state block must reach the device on the pass that samples";
    EXPECT_EQ(sampling.samplers[0].slot, 0u);
    EXPECT_EQ(sampling.samplers[0].sampler.getAddressUProperty(), TextureAddressMode::Mirror)
        << "the source declares AddressU = Mirror";
    EXPECT_EQ(sampling.samplers[0].sampler.getAddressVProperty(), TextureAddressMode::Clamp);
    EXPECT_EQ(sampling.samplers[0].sampler.getMaxAnisotropyProperty(), 8);

    // Re-applying the SAME pass must publish the same state again (plans/plan_fx.md FX-101): MojoShader
    // takes a commit-only shortcut there, and a backend that clears its state-change struct first
    // silently publishes nothing on every application after the first.
    CompiledEffectPassStateChanges again;
    runtime->ApplyPass(0, deviceState, again);
    EXPECT_FALSE(again.samplers.empty())
        << "a repeated apply of one pass must re-publish its own sampler binding";
}

TEST(VulkanCompiledEffectTest, TechniqueAndPassSelectionAreBounded)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, LoadEffect("CnaConformanceEffect.fxb"));
    ASSERT_NE(runtime, nullptr) << "this build did not select the Vulkan renderer";

    EXPECT_NO_THROW(runtime->SetTechnique(1));
    EXPECT_THROW(runtime->SetTechnique(2), std::out_of_range);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    // SecondTechnique has exactly one pass.
    EXPECT_NO_THROW(runtime->ApplyPass(0, deviceState, changes));
    EXPECT_THROW(runtime->ApplyPass(1, deviceState, changes), std::out_of_range);
}

TEST(VulkanCompiledEffectTest, CloneCarriesItsOwnValuesAndSurvivesTheSource)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, LoadEffect("CnaConformanceEffect.fxb"));
    ASSERT_NE(runtime, nullptr) << "this build did not select the Vulkan renderer";

    runtime->SetTechnique(1);
    auto clone = runtime->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetDescription().techniques.size(),
              runtime->GetDescription().techniques.size());

    // Writing the clone's storage must not reach the source's: MOJOSHADER_cloneEffect gives the
    // copy its own parameter storage while sharing only immutable compiled shader objects.
    const float value = 0.5f;
    EXPECT_NO_THROW(clone->SetParameterValue(0, &value, sizeof(value)));

    // Disposing the source first must leave the clone usable, and vice versa.
    runtime.reset();
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_NO_THROW(clone->ApplyPass(0, deviceState, changes));
}

TEST(VulkanCompiledEffectTest, MalformedBytecodeIsRejectedWithoutCrashing)
{
    GraphicsDevice device;
    VulkanRenderer* renderer = RendererOf(device);
    if (renderer == nullptr)
        GTEST_SKIP() << "this build did not select the Vulkan renderer";

    EXPECT_THROW(renderer->CreateCompiledEffect(nullptr, 0), std::invalid_argument);
    const std::vector<std::uint8_t> garbage(64, 0xABu);
    EXPECT_ANY_THROW(renderer->CreateCompiledEffect(garbage.data(), garbage.size()));

    const std::vector<std::uint8_t> shortHeader{
        0x01, 0x09, 0xFF, 0xFE, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    EXPECT_ANY_THROW(renderer->CreateCompiledEffect(shortHeader.data(), shortHeader.size()));

    // A REAL effect truncated mid-table: structurally credible for as far as the parser gets, so
    // it reaches deeper than the hand-built header above. It trips an assertion inside pinned
    // MojoShader's own effect parser (`mojoshader_effects.c` `readvalue`), and MojoShader's
    // `assert` resolves to SDL's, whose default handler BLOCKS for an interactive answer. This
    // case therefore used to hang a whole suite run rather than fail, which is why it was left out
    // of this test until `tests/HarnessAssertionPolicy.cpp` (plans/plan_fx.md FX-111) made the suite's
    // assertion policy non-interactive. It is here now because it is the one case that proves that
    // policy is in effect -- if it regresses, this hangs.
    const std::vector<std::uint8_t> whole = LoadEffect("CnaConformanceEffect.fxb");
    if (!whole.empty())
    {
        for (std::size_t bytes = 4; bytes < whole.size(); bytes += 4)
        {
            const std::vector<std::uint8_t> truncated(
                whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(bytes));
            // Two outcomes are acceptable and a third is not. Refusing is the usual one. Parsing
            // successfully is also legitimate -- the last few lengths of this fixture drop only
            // trailing bytes MojoShader does not need -- but then the runtime has to be WHOLE, not
            // a half-built object that crashes at the next call. What is not acceptable is wedging
            // or killing the process, which is the whole point of sweeping every length rather
            // than one: on this fixture 1544 bytes reaches an assertion inside MojoShader's own
            // parser, and before FX-111 that alone hung the suite. The length is a property of the
            // fixture rather than of the contract, so it is not pinned here.
            std::unique_ptr<ICompiledEffectRuntime> parsed;
            try
            {
                parsed = renderer->CreateCompiledEffect(truncated.data(), truncated.size());
            }
            catch (const std::exception&)
            {
                continue;
            }
            ASSERT_NE(parsed, nullptr) << "truncated to " << bytes << " bytes";
            EXPECT_FALSE(parsed->GetDescription().techniques.empty())
                << "truncated to " << bytes << " bytes: accepted, but reflects nothing";
        }
    }
}

TEST(VulkanCompiledEffectTest, RepeatedCreateApplyDisposeCyclesStayStable)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Vulkan renderer";
    const std::vector<std::uint8_t> bytes = LoadEffect("CnaConformanceEffect.fxb");
    ASSERT_FALSE(bytes.empty());

    // The backend's shader ref-counting and VkShaderModule ownership are CNA's own here, so the
    // create/apply/destroy cycle is worth exercising rather than assumed.
    for (int cycle = 0; cycle < 8; ++cycle)
    {
        SCOPED_TRACE("cycle " + std::to_string(cycle));
        auto runtime = CreateRuntime(device, bytes);
        ASSERT_NE(runtime, nullptr);
        CompiledEffectDeviceState deviceState;
        CompiledEffectPassStateChanges changes;
        EXPECT_NO_THROW(runtime->ApplyPass(0, deviceState, changes));
        auto clone = runtime->Clone();
        EXPECT_NO_THROW(clone->ApplyPass(0, deviceState, changes));
        if ((cycle & 1) == 0) clone.reset();
    }
}

// plans/plan_fx.md FX-060/FX-065: the same cross-renderer contracts FNA3D, SDL_GPU and EasyGL run,
// through the public Effect/GraphicsDevice API. Each drawing contract renders the compiled
// effect's own parameters into a render target and reads the pixels back, so a draw that silently
// used a stock shader -- or bound an attribute, uniform slice or sampler from the wrong place --
// fails instead of passing quietly.

TEST(VulkanCompiledEffectTest, SharedBackendConformanceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedSamplerPixelContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::CompiledEffectSamplerContractOptions options;
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device, options);
}

TEST(VulkanCompiledEffectDrawTest, SharedPassSelectionContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedManyDrawsContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    // plans/plan_fx.md FX-112: this renderer's own per-frame uniform chunks are what this contract was
    // written against -- 600 compiled draws cross two of them -- but the shape is not Vulkan's,
    // so it lives in the shared suite where a fifth backend's own ring gets the same check.
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(VulkanCompiledEffectDrawTest, SharedTruncationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}

// plans/plan_fx.md FX-112. The compiled SpriteBatch route leaves the stock sprite pipeline entirely and
// takes an early return out of End(), so the sequence that can break is returning to a compiled
// batch after a stock one has run between them: a pending-sprite list that survived, or batch state
// the early return skipped, would show up here and nowhere else. The three batches draw into
// separate thirds of one target so all three results are readable at once.
TEST(VulkanCompiledEffectDrawTest, SpriteBatchAlternatesCompiledAndStockAcrossBatches)
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

#endif  // CNA_VULKAN_COMPILED_EFFECTS
