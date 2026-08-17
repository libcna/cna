// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-062: the EasyGL compiled-effect runtime, exercised directly.
//
// SupportsCompiledEffects() still reports false: the draw route (plan_fx.md FX-062's own
// equivalent of FX-071) does not exist yet, only the runtime does. These tests go through
// CNA::Internal::Renderers::EasyGL::EasyGLRenderer::CreateCompiledEffect rather than the public
// Effect class for exactly that reason -- the public Effect(device, bytecode) constructor would
// refuse until the capability is true.
//
// What this proves that the existence-gate probe does not: the runtime honours CNA's own contract
// -- reflection matching the source, technique and pass selection, parameter and texture
// validation, render state translation, clone independence -- against the same committed fixtures
// the FNA3D and SDL_GPU backends use.

#if defined(CNA_EASYGL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/EasyGL/EasyGLCompiledEffect.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

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
    using CNA::Internal::Renderers::EasyGL::EasyGLRenderer;

    /// Reads a committed fixture. They live with the FNA3D renderer, which owns their provenance.
    std::vector<std::uint8_t> LoadEffect(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() /
            "../../../../../../fna3d/effects" / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    EasyGLRenderer* RendererOf(GraphicsDevice& device)
    {
        return dynamic_cast<EasyGLRenderer*>(&device.GetRenderer());
    }

    std::unique_ptr<ICompiledEffectRuntime> CreateRuntime(GraphicsDevice& device,
                                                          const std::string& name)
    {
        EasyGLRenderer* renderer = RendererOf(device);
        if (renderer == nullptr) return nullptr;
        const std::vector<std::uint8_t> bytes = LoadEffect(name);
        if (bytes.empty()) return nullptr;
        return renderer->CreateCompiledEffect(bytes.data(), bytes.size());
    }
}

TEST(EasyGLCompiledEffectTest, TheCapabilityStaysFalseUntilDrawsExecuteTheEffect)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the EasyGL renderer";
    // The runtime works, and the renderer does not claim it yet: a compiled pass cannot yet take
    // over this renderer's draw path (plan_fx.md FX-062's own equivalent of FX-071), and a true
    // here would mean a game silently rendered with a stock shader instead.
    EXPECT_FALSE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
}

TEST(EasyGLCompiledEffectTest, ReflectionMatchesTheConformanceSource)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    const auto& description = runtime->GetDescription();
    // CnaConformanceEffect.fx declares Gain, Tint, Transform, Weights, Lighting, FxTexture and
    // FxSampler, then two techniques whose passes are P0/StatePass and P1. Six parameters, not
    // seven: a sampler is not a reflected parameter, which is FNA's own answer for this binary
    // (tests/fixtures/compiled-effects/fna-effect-reflection.json lists exactly these six).
    EXPECT_EQ(description.parameters.size(), 6u);
    EXPECT_EQ(description.parameters.front().name, "Gain");
    EXPECT_EQ(description.parameters.back().name, "FxTexture");
    ASSERT_EQ(description.techniques.size(), 2u);
    EXPECT_EQ(description.techniques[0].name, "FirstTechnique");
    ASSERT_EQ(description.techniques[0].passes.size(), 2u);
    EXPECT_EQ(description.techniques[0].passes[0].name, "P0");
    EXPECT_EQ(description.techniques[0].passes[1].name, "StatePass");
    EXPECT_EQ(description.techniques[1].name, "SecondTechnique");
    ASSERT_EQ(description.techniques[1].passes.size(), 1u);
    EXPECT_EQ(description.techniques[1].passes[0].name, "P1");
}

TEST(EasyGLCompiledEffectTest, StatePassPublishesTheStatesItsSourceAssigns)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    const BlendState blend = BlendState::Opaque;
    const DepthStencilState depth = DepthStencilState::Default;
    const RasterizerState raster = RasterizerState::CullCounterClockwise;
    CompiledEffectDeviceState deviceState;
    deviceState.blend = &blend;
    deviceState.depthStencil = &depth;
    deviceState.rasterizer = &raster;

    // StatePass is the second pass of the first technique and is the one that assigns states.
    runtime->SetTechnique(0);
    CompiledEffectPassStateChanges changes;
    runtime->ApplyPass(1, deviceState, changes);

    ASSERT_TRUE(changes.blendChanged);
    EXPECT_EQ(changes.blend.getColorSourceBlendProperty(), Blend::SourceAlpha);
    EXPECT_EQ(changes.blend.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
    ASSERT_TRUE(changes.depthStencilChanged);
    EXPECT_FALSE(changes.depthStencil.getDepthBufferEnableProperty());
    EXPECT_FALSE(changes.depthStencil.getDepthBufferWriteEnableProperty());
    ASSERT_TRUE(changes.rasterizerChanged);
    EXPECT_EQ(changes.rasterizer.getCullModeProperty(), CullMode::None);
}

TEST(EasyGLCompiledEffectTest, APassThatAssignsNoStateLeavesTheGamesSelectionAlone)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    const BlendState blend = BlendState::Opaque;
    const DepthStencilState depth = DepthStencilState::Default;
    const RasterizerState raster = RasterizerState::CullCounterClockwise;
    CompiledEffectDeviceState deviceState;
    deviceState.blend = &blend;
    deviceState.depthStencil = &depth;
    deviceState.rasterizer = &raster;

    runtime->SetTechnique(0);
    CompiledEffectPassStateChanges changes;
    runtime->ApplyPass(0, deviceState, changes);  // P0 assigns only shaders

    EXPECT_FALSE(changes.blendChanged);
    EXPECT_FALSE(changes.depthStencilChanged);
    EXPECT_FALSE(changes.rasterizerChanged);
}

TEST(EasyGLCompiledEffectTest, OutOfRangeTechniqueAndPassAreRejected)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    EXPECT_THROW(runtime->SetTechnique(99), std::out_of_range);
    runtime->SetTechnique(1);
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_THROW(runtime->ApplyPass(5, deviceState, changes), std::out_of_range);
}

TEST(EasyGLCompiledEffectTest, ParameterWritesAreBoundedAndTypeChecked)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    const float tooMuch[64] = {};
    EXPECT_THROW(runtime->SetParameterValue(0, tooMuch, sizeof(tooMuch)), std::invalid_argument);
    EXPECT_THROW(runtime->SetParameterValue(999, tooMuch, 4), std::out_of_range);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    EXPECT_THROW(runtime->SetParameterTexture(999, &white), std::out_of_range);
    // Parameter 0 is the scalar Gain, not a texture.
    EXPECT_THROW(runtime->SetParameterTexture(0, &white), std::invalid_argument);
}

TEST(EasyGLCompiledEffectTest, MalformedBytecodeIsRejected)
{
    GraphicsDevice device;
    EasyGLRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the EasyGL renderer";

    const std::vector<std::uint8_t> garbage(512, 0xAB);
    EXPECT_ANY_THROW(renderer->CreateCompiledEffect(garbage.data(), garbage.size()));
    EXPECT_THROW(renderer->CreateCompiledEffect(nullptr, 0), std::invalid_argument);
}

TEST(EasyGLCompiledEffectTest, CloneCarriesItsOwnValuesAndSurvivesTheSource)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    runtime->SetTechnique(1);
    auto clone = runtime->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetDescription().techniques.size(),
              runtime->GetDescription().techniques.size());

    // Destroying the source first is the ordering a shared-ownership mistake would surface in.
    runtime.reset();
    EXPECT_NO_THROW({
        CompiledEffectDeviceState deviceState;
        CompiledEffectPassStateChanges changes;
        clone->ApplyPass(0, deviceState, changes);
    });
}

TEST(EasyGLCompiledEffectTest, EveryCommittedStockEffectParses)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the EasyGL renderer";

    for (const char* name : {"SpriteEffect.fxb", "BasicEffect.fxb", "AlphaTestEffect.fxb",
                             "DualTextureEffect.fxb", "EnvironmentMapEffect.fxb",
                             "SkinnedEffect.fxb"})
    {
        auto runtime = CreateRuntime(device, name);
        ASSERT_NE(runtime, nullptr) << name;
        EXPECT_FALSE(runtime->GetDescription().techniques.empty()) << name;
    }
}

#endif  // CNA_EASYGL_COMPILED_EFFECTS
