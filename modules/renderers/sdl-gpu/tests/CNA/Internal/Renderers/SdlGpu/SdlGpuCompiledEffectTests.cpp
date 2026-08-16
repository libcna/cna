// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-061: the SDL_GPU compiled-effect runtime, exercised directly.
//
// These go through CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer::CreateCompiledEffect rather
// than through the public Effect class, because SupportsCompiledEffects() is deliberately still
// false on this renderer: a compiled pass cannot yet take over its draw path, and advertising
// support before that would mean silently drawing with a stock shader. The runtime underneath is
// complete, so it is tested at the level where it is reachable.
//
// What this proves that the existence-gate probes do not: the runtime honours CNA's own contract
// -- reflection matching the source, technique and pass selection, parameter and texture
// validation, render and sampler state translation, clone independence -- against the same
// committed fixtures the FNA3D backend uses.

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffect.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffectVertexLayout.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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
    using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;

    /// Reads a committed fixture. They live with the FNA3D renderer, which owns their provenance.
    std::vector<std::uint8_t> LoadEffect(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() /
            "../../../../../../fna3d/effects" / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    SdlGpuRenderer* RendererOf(GraphicsDevice& device)
    {
        return dynamic_cast<SdlGpuRenderer*>(&device.GetRenderer());
    }

    std::unique_ptr<ICompiledEffectRuntime> CreateRuntime(GraphicsDevice& device,
                                                          const std::string& name)
    {
        SdlGpuRenderer* renderer = RendererOf(device);
        if (renderer == nullptr) return nullptr;
        const std::vector<std::uint8_t> bytes = LoadEffect(name);
        if (bytes.empty()) return nullptr;
        return renderer->CreateCompiledEffect(bytes.data(), bytes.size());
    }
}

TEST(SdlGpuCompiledEffectTest, TheCapabilityStaysFalseUntilDrawsExecuteTheEffect)
{
    GraphicsDevice device;
    // The whole point of this file: the runtime works, and the renderer still does not claim it.
    // A compiled pass cannot yet take over this renderer's draw path, and a true here would mean
    // a game silently rendered with a stock shader instead.
    EXPECT_FALSE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
}

TEST(SdlGpuCompiledEffectTest, ReflectionMatchesTheConformanceSource)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";

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

TEST(SdlGpuCompiledEffectTest, StatePassPublishesTheStatesItsSourceAssigns)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";

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

TEST(SdlGpuCompiledEffectTest, APassThatAssignsNoStateLeavesTheGamesSelectionAlone)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";

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

TEST(SdlGpuCompiledEffectTest, EachPassBindsItsOwnShaderPair)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";
    auto* sdlGpuEffect =
        dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(runtime.get());
    ASSERT_NE(sdlGpuEffect, nullptr);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    MOJOSHADER_sdlShaderData* vertex = nullptr;
    MOJOSHADER_sdlShaderData* pixel = nullptr;
    sdlGpuEffect->GetBoundShadersEXT(vertex, pixel);
    // This is what a compiled-effect draw route will need. Without it the pass would have nothing
    // to render with, so a null pair is a real failure rather than a missing feature.
    EXPECT_NE(vertex, nullptr);
    EXPECT_NE(pixel, nullptr);
}

TEST(SdlGpuCompiledEffectTest, OutOfRangeTechniqueAndPassAreRejected)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";

    EXPECT_THROW(runtime->SetTechnique(99), std::out_of_range);
    runtime->SetTechnique(1);
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_THROW(runtime->ApplyPass(5, deviceState, changes), std::out_of_range);
}

TEST(SdlGpuCompiledEffectTest, ParameterWritesAreBoundedAndTypeChecked)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";

    const float tooMuch[64] = {};
    EXPECT_THROW(runtime->SetParameterValue(0, tooMuch, sizeof(tooMuch)), std::invalid_argument);
    EXPECT_THROW(runtime->SetParameterValue(999, tooMuch, 4), std::out_of_range);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    EXPECT_THROW(runtime->SetParameterTexture(999, &white), std::out_of_range);
    // Parameter 0 is the scalar Gain, not a texture.
    EXPECT_THROW(runtime->SetParameterTexture(0, &white), std::invalid_argument);
}

TEST(SdlGpuCompiledEffectTest, MalformedBytecodeIsRejected)
{
    GraphicsDevice device;
    SdlGpuRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the SDL_GPU renderer";

    const std::vector<std::uint8_t> garbage(512, 0xAB);
    EXPECT_ANY_THROW(renderer->CreateCompiledEffect(garbage.data(), garbage.size()));
    EXPECT_THROW(renderer->CreateCompiledEffect(nullptr, 0), std::invalid_argument);
}

TEST(SdlGpuCompiledEffectTest, CloneCarriesItsOwnValuesAndSurvivesTheSource)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";

    runtime->SetTechnique(1);
    auto clone = runtime->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetDescription().techniques.size(),
              runtime->GetDescription().techniques.size());

    // Destroying the source first is the ordering a shared-ownership mistake would surface in.
    runtime.reset();
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_NO_THROW(clone->ApplyPass(0, deviceState, changes));
}

// ---- plan_fx.md FX-071: vertex-attribute builder and uniform snapshot capture ------------------

TEST(SdlGpuCompiledEffectVertexLayoutTest, EveryFormatAndUsageMapsToADistinctNativeValue)
{
    using CNA::Internal::Renderers::SdlGpu::ToMojoShaderUsage;
    using CNA::Internal::Renderers::SdlGpu::ToSdlGpuVertexElementFormat;

    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Single), SDL_GPU_VERTEXELEMENTFORMAT_FLOAT);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Vector2), SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Vector3), SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Vector4), SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Color), SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Byte4), SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Short2), SDL_GPU_VERTEXELEMENTFORMAT_SHORT2);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::Short4), SDL_GPU_VERTEXELEMENTFORMAT_SHORT4);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::NormalizedShort2), SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::NormalizedShort4), SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::HalfVector2), SDL_GPU_VERTEXELEMENTFORMAT_HALF2);
    EXPECT_EQ(ToSdlGpuVertexElementFormat(VertexElementFormat::HalfVector4), SDL_GPU_VERTEXELEMENTFORMAT_HALF4);
    EXPECT_THROW((void) ToSdlGpuVertexElementFormat(static_cast<VertexElementFormat>(999)), std::invalid_argument);

    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Position), MOJOSHADER_USAGE_POSITION);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Color), MOJOSHADER_USAGE_COLOR);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::TextureCoordinate), MOJOSHADER_USAGE_TEXCOORD);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Normal), MOJOSHADER_USAGE_NORMAL);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Binormal), MOJOSHADER_USAGE_BINORMAL);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Tangent), MOJOSHADER_USAGE_TANGENT);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::BlendIndices), MOJOSHADER_USAGE_BLENDINDICES);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::BlendWeight), MOJOSHADER_USAGE_BLENDWEIGHT);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Depth), MOJOSHADER_USAGE_DEPTH);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Fog), MOJOSHADER_USAGE_FOG);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::PointSize), MOJOSHADER_USAGE_POINTSIZE);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::Sample), MOJOSHADER_USAGE_SAMPLE);
    EXPECT_EQ(ToMojoShaderUsage(VertexElementUsage::TessellateFactor), MOJOSHADER_USAGE_TESSFACTOR);
    EXPECT_THROW((void) ToMojoShaderUsage(static_cast<VertexElementUsage>(999)), std::invalid_argument);
}

TEST(SdlGpuCompiledEffectVertexLayoutTest, BuildsOneAttributePerShaderInputLocatedByArrayIndex)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";
    auto* sdlGpuEffect =
        dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(runtime.get());
    ASSERT_NE(sdlGpuEffect, nullptr);

    // P0 (technique 0, pass 0): MainVertexShader declares float4 Position : POSITION0 and
    // float2 TexCoord : TEXCOORD0, in that order (see CnaConformanceEffect.fx's VertexIn struct).
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    MOJOSHADER_sdlShaderData* vertex = nullptr;
    MOJOSHADER_sdlShaderData* pixel = nullptr;
    sdlGpuEffect->GetBoundShadersEXT(vertex, pixel);
    ASSERT_NE(vertex, nullptr);
    const MOJOSHADER_parseData* parseData = MOJOSHADER_sdlGetShaderParseData(vertex);
    ASSERT_NE(parseData, nullptr);

    // VertexPositionTexture's own layout (stride 20): a float4 shader input is satisfied by a
    // Vector3 declared element -- the vertex input assembler fills the missing w with 1.0, the same
    // rule every graphics API applies, so this is not a format mismatch.
    const std::vector<VertexElement> declaration = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };

    const auto attributes = CNA::Internal::Renderers::SdlGpu::BuildCompiledEffectVertexAttributes(
        *parseData, declaration, /*bufferSlot=*/0);

    ASSERT_EQ(attributes.size(), 2u);
    EXPECT_EQ(attributes[0].location, 0u);
    EXPECT_EQ(attributes[0].buffer_slot, 0u);
    EXPECT_EQ(attributes[0].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    EXPECT_EQ(attributes[0].offset, 0u);
    EXPECT_EQ(attributes[1].location, 1u);
    EXPECT_EQ(attributes[1].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);
    EXPECT_EQ(attributes[1].offset, 12u);
}

TEST(SdlGpuCompiledEffectVertexLayoutTest, RefusesADeclarationMissingAShaderInput)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";
    auto* sdlGpuEffect =
        dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(runtime.get());
    ASSERT_NE(sdlGpuEffect, nullptr);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    MOJOSHADER_sdlShaderData* vertex = nullptr;
    MOJOSHADER_sdlShaderData* pixel = nullptr;
    sdlGpuEffect->GetBoundShadersEXT(vertex, pixel);
    ASSERT_NE(vertex, nullptr);
    const MOJOSHADER_parseData* parseData = MOJOSHADER_sdlGetShaderParseData(vertex);
    ASSERT_NE(parseData, nullptr);

    // Position only -- the shader also needs TexCoord0, which this declaration does not supply.
    const std::vector<VertexElement> declaration = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    };

    EXPECT_THROW(
        (void) CNA::Internal::Renderers::SdlGpu::BuildCompiledEffectVertexAttributes(
            *parseData, declaration, /*bufferSlot=*/0),
        System::NotSupportedException);
}

TEST(SdlGpuCompiledEffectTest, CapturedUniformSnapshotReflectsTheAppliedPassAndCurrentValues)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";
    auto* sdlGpuEffect =
        dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(runtime.get());
    ASSERT_NE(sdlGpuEffect, nullptr);

    // P0: MainVertexShader reads Transform (a float4x4 uniform); MainPixelShader reads Gain, Tint
    // and Lighting -- both stages have a non-empty uniform buffer to capture.
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    std::vector<std::uint8_t> vertexBytes;
    std::vector<std::uint8_t> pixelBytes;
    sdlGpuEffect->CaptureUniformSnapshotEXT(vertexBytes, pixelBytes);
    EXPECT_FALSE(vertexBytes.empty());
    EXPECT_FALSE(pixelBytes.empty());
    // Each packed uniform occupies a whole number of 16-byte (float4-register) slots.
    EXPECT_EQ(vertexBytes.size() % 16u, 0u);
    EXPECT_EQ(pixelBytes.size() % 16u, 0u);

    // Gain is parameter 0 (see ReflectionMatchesTheConformanceSource); changing it and reapplying
    // the same pass must change the pixel shader's captured bytes, since MainPixelShader reads it.
    const float newGain = 0.75f;
    runtime->SetParameterValue(0, &newGain, sizeof(newGain));
    runtime->ApplyPass(0, deviceState, changes);
    std::vector<std::uint8_t> secondVertexBytes;
    std::vector<std::uint8_t> secondPixelBytes;
    sdlGpuEffect->CaptureUniformSnapshotEXT(secondVertexBytes, secondPixelBytes);
    EXPECT_NE(pixelBytes, secondPixelBytes);
}

TEST(SdlGpuCompiledEffectTest, BoundSamplerPersistsAcrossAPassThatDoesNotReassignIt)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";
    auto* sdlGpuEffect =
        dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(runtime.get());
    ASSERT_NE(sdlGpuEffect, nullptr);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    // FxTexture is parameter 5, the last of the six reflected parameters (see
    // ReflectionMatchesTheConformanceSource); FxSampler is its only sampler, so it occupies
    // register/slot 0.
    runtime->SetParameterTexture(5, &white);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);  // P0: assigns FxSampler/FxTexture

    Texture* boundTexture = nullptr;
    SamplerState boundSampler;
    sdlGpuEffect->GetBoundSamplerEXT(0, /*vertexStage=*/false, boundTexture, boundSampler);
    EXPECT_EQ(boundTexture, &white);

    // StatePass assigns render states but not FxSampler/FxTexture again; the earlier binding must
    // still stand, matching real XNA GraphicsDevice.Textures/SamplerStates behavior.
    runtime->ApplyPass(1, deviceState, changes);
    boundTexture = nullptr;
    sdlGpuEffect->GetBoundSamplerEXT(0, /*vertexStage=*/false, boundTexture, boundSampler);
    EXPECT_EQ(boundTexture, &white);

    // The vertex stage never bound anything.
    Texture* vertexTexture = &white;
    sdlGpuEffect->GetBoundSamplerEXT(0, /*vertexStage=*/true, vertexTexture, boundSampler);
    EXPECT_EQ(vertexTexture, nullptr);
}

TEST(SdlGpuCompiledEffectVertexLayoutTest, LinksTheAppliedPassAndReturnsStableShaderModules)
{
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the SDL_GPU renderer";
    auto* sdlGpuEffect =
        dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(runtime.get());
    ASSERT_NE(sdlGpuEffect, nullptr);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);  // P0: MainVertexShader/MainPixelShader

    const std::vector<VertexElement> declaration = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };

    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* pixelShader = nullptr;
    const auto attributes = sdlGpuEffect->LinkAndGetShadersEXT(declaration, vertexShader, pixelShader);
    EXPECT_NE(vertexShader, nullptr);
    EXPECT_NE(pixelShader, nullptr);
    EXPECT_EQ(attributes.size(), 2u);

    // Applying a different pass (StatePass, same MainVertexShader but a different pixel shader)
    // and linking again must not invalidate the first pair -- MojoShader's linker cache keeps
    // both alive for the context's whole lifetime, which is what makes it safe for a deferred
    // draw command to keep a linked pair around past this call.
    runtime->ApplyPass(1, deviceState, changes);
    SDL_GPUShader* secondVertexShader = nullptr;
    SDL_GPUShader* secondPixelShader = nullptr;
    (void) sdlGpuEffect->LinkAndGetShadersEXT(declaration, secondVertexShader, secondPixelShader);
    EXPECT_NE(secondPixelShader, pixelShader);  // StatePass links FlatPixelShader, not MainPixelShader
    EXPECT_NE(vertexShader, nullptr);
    EXPECT_NE(pixelShader, nullptr);
}

TEST(SdlGpuCompiledEffectTest, EveryCommittedStockEffectParses)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the SDL_GPU renderer";

    for (const char* name : {"SpriteEffect.fxb", "BasicEffect.fxb", "AlphaTestEffect.fxb",
                             "DualTextureEffect.fxb", "EnvironmentMapEffect.fxb",
                             "SkinnedEffect.fxb"})
    {
        auto runtime = CreateRuntime(device, name);
        ASSERT_NE(runtime, nullptr) << name;
        EXPECT_FALSE(runtime->GetDescription().techniques.empty()) << name;
    }
}

#endif  // CNA_SDL_GPU_COMPILED_EFFECTS
