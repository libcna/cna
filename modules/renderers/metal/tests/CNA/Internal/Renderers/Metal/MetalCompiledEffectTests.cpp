// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-144: the Metal compiled-effect runtime.
//
// Like Vulkan and WebGPU this backend has no MojoShader-provided adapter: the nine-function effect
// context, the constant register files and the uniform packing are code in this repository, and
// on top of them SPIRV-Cross translates each linked stage to MSL -- so the tests that matter most
// are the ones that put real pixels through that translation.
//
// The runtime-level tests go through `MetalRenderer::CreateCompiledEffect` DIRECTLY, so they keep
// testing the backend rather than the layer above it. The shared cross-renderer contracts go through
// the public `Effect`/`GraphicsDevice` API instead, which is what makes them the same evidence
// FNA3D, SDL_GPU, EasyGL, Vulkan and WebGPU produce. The draw-level tests below the shared ones are
// WebGPU's own (WEBGPU-208, WMG-0028, WEBGPU-170), which ask renderer-neutral questions.

#if defined(CNA_METAL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Metal/MetalCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Metal/MetalRenderer.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include "System/NotSupportedException.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <array>
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
    using CNA::Internal::Renderers::Metal::MetalCompiledEffect;
    using CNA::Internal::Renderers::Metal::MetalCompiledShaderEXT;
    using CNA::Internal::Renderers::Metal::MetalRenderer;

    /// Reads a committed fixture. They live with the FNA3D renderer, which owns their provenance.
    std::vector<std::uint8_t> LoadEffect(const std::string& name)
    {
        const std::filesystem::path path = CNA::TestSupport::CompiledEffectDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    MetalRenderer* RendererOf(GraphicsDevice& device)
    {
        return dynamic_cast<MetalRenderer*>(&device.GetRenderer());
    }

    std::unique_ptr<ICompiledEffectRuntime> CreateRuntime(GraphicsDevice& device,
                                                          const std::vector<std::uint8_t>& bytes)
    {
        MetalRenderer* renderer = RendererOf(device);
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

TEST(MetalCompiledEffectTest, TheCapabilityIsTrueAndThePublicBoundaryAcceptsBytecode)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";
    // plans/plan_apple_m4.md AM4-144, as WEBGPU-171: true only because the draw route exists. The two report
    // false/true together on purpose -- a capability that says true while a compiled draw falls
    // through to a stock shader is exactly the defect FX-080 removed from the other backends.
    EXPECT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
    EXPECT_NO_THROW(Microsoft::Xna::Framework::Graphics::Effect(
        device, CNA::TestSupport::BuildSyntheticDrawableEffect()));
}

TEST(MetalCompiledEffectTest, EveryCommittedStockEffectTranslatesAndSplits)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";
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

TEST(MetalCompiledEffectTest, RealXna4GameEffectsWithShaderModel1PixelShadersParseAndReflect)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";
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

TEST(MetalCompiledEffectTest, EveryPassOfTheRealXna4GameEffectsCreatesBothShaderModules)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";
    // plans/plan_fx.md FX-134, on Metal's route: EVERY pass of the two real XNA 4.0 game effects
    // links and translates to MSL for both stages -- six of the eighteen are Shader Model 1.x. The
    // draw tests then compile and run that MSL.
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
        auto* compiled = dynamic_cast<CNA::Internal::Renderers::Metal::MetalCompiledEffect*>(
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

                MetalCompiledShaderEXT* vertexShader = nullptr;
                MetalCompiledShaderEXT* pixelShader = nullptr;
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
                MetalCompiledEffect::CompiledVertexStreamEXT stream{};
                stream.elements = &elements;
                stream.stride = static_cast<std::uint32_t>(elements.size() * 16);

                MetalCompiledEffect::LinkedPassEXT link;
                ASSERT_NO_THROW(link = compiled->LinkAndGetShadersEXT({stream}))
                    << name << " technique " << t << " pass " << p;
                EXPECT_FALSE(link.vertex->msl.empty())
                    << name << " technique " << t << " pass " << p;
                EXPECT_FALSE(link.pixel->msl.empty())
                    << name << " technique " << t << " pass " << p;
                ++linked;
            }
        }
    }
    // Eighteen passes across the two fixtures; six of them are the Shader Model 1.x ones.
    EXPECT_EQ(linked, 18);
}

// AM4-144: SPIRV-Cross is handed only what MojoShader produced; anything else is refused by name
// rather than reaching the translator.
TEST(MetalCompiledEffectTest, TheMslTranslationRefusesSomethingThatIsNotSpirv)
{
    using CNA::Internal::Renderers::Metal::MetalCompiledStageKind;
    using CNA::Internal::Renderers::Metal::TranslateMetalCompiledStageEXT;
    const std::vector<std::uint32_t> notSpirv{1u, 2u, 3u, 4u, 5u};
    const auto result = TranslateMetalCompiledStageEXT(notSpirv.data(), notSpirv.size(),
                                                       MetalCompiledStageKind::Pixel, false);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.stage.msl.empty());
    const auto none = TranslateMetalCompiledStageEXT(nullptr, 0, MetalCompiledStageKind::Vertex, false);
    EXPECT_FALSE(none.error.empty());
}

// AM4-144: the stock SpriteEffect's pass links and translates deterministically -- the same linked
// SPIR-V yields byte-identical MSL with the same entry point and hash, which is what lets the
// renderer cache a Metal library per body rather than per draw.
TEST(MetalCompiledEffectTest, TranslationIsDeterministicAndNamesItsEntryPoint)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";
    std::unique_ptr<ICompiledEffectRuntime> runtime =
        CreateRuntime(device, LoadEffect("SpriteEffect.fxb"));
    ASSERT_NE(runtime, nullptr);
    auto* compiled = dynamic_cast<MetalCompiledEffect*>(runtime.get());
    ASSERT_NE(compiled, nullptr);
    CompiledEffectDeviceState state{};
    CompiledEffectPassStateChanges changes{};
    compiled->ApplyPass(0, state, changes);

    MetalCompiledShaderEXT* vertexShader = nullptr;
    MetalCompiledShaderEXT* pixelShader = nullptr;
    compiled->GetBoundShadersEXT(vertexShader, pixelShader);
    ASSERT_NE(vertexShader, nullptr);
    std::vector<VertexElement> elements;
    for (int a = 0; a < vertexShader->parseData->attribute_count; ++a)
    {
        elements.emplace_back(static_cast<int>(elements.size()) * 16, VertexElementFormat::Vector4,
                              FromMojoShaderUsage(vertexShader->parseData->attributes[a].usage),
                              vertexShader->parseData->attributes[a].index);
    }
    MetalCompiledEffect::CompiledVertexStreamEXT stream{};
    stream.elements = &elements;
    stream.stride = static_cast<std::uint32_t>(elements.size() * 16);
    const auto first = compiled->LinkAndGetShadersEXT({stream});
    const auto second = compiled->LinkAndGetShadersEXT({stream});
    ASSERT_NE(first.vertex, nullptr);
    ASSERT_NE(first.pixel, nullptr);
    EXPECT_FALSE(first.vertex->entryPoint.empty());
    EXPECT_FALSE(first.pixel->entryPoint.empty());
    EXPECT_NE(first.vertex->msl.find(first.vertex->entryPoint), std::string::npos);
    EXPECT_NE(first.pixel->msl.find("fragment"), std::string::npos);
    // One translation per linked body: the second link is served the first's.
    EXPECT_EQ(first.vertex, second.vertex);
    EXPECT_EQ(first.pixel, second.pixel);
    EXPECT_EQ(first.pipelineKey, second.pipelineKey);
    // SpriteEffect samples its texture at register s0.
    ASSERT_EQ(first.pixelSamplers.size(), 1u);
    EXPECT_EQ(first.pixelSamplers[0].slot, 0u);
}

// AM4-144: XNA's Direct3D 9 pixel centres for a compiled draw. A compiled shader computes its own
// clip position, so the renderer cannot fold the correction into a matrix as the stock route does;
// Metal moves the viewport instead (EasyGL's posFixup does the same in GL). A quad whose edges lie
// on half pixels discriminates: Direct3D 9 samples pixel centres at whole coordinates, so a quad
// over [0.5, 2.5) covers pixels 1 and 2, while Metal's own half-pixel centres would give 0 and 1.
TEST(MetalCompiledEffectDrawTest, AHalfPixelEdgedQuadCoversDirect3D9PixelCentres)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector4;
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";

    constexpr int kSize = 8;
    Effect effect(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
    auto& parameters = effect.getParametersProperty();
    parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
    parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

    struct ClipVertex { float x, y, z; };
    const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    // Pixel x in [0.5, 2.5) is NDC [-0.875, -0.375); rows likewise, y up.
    const float l = -0.875f, r = -0.375f, t = 0.875f, b = 0.375f;
    const ClipVertex quad[6] = {{l, t, 0}, {l, b, 0}, {r, b, 0}, {l, t, 0}, {r, b, 0}, {r, t, 0}};

    RenderTarget2D target(device, kSize, kSize);
    device.SetRenderTarget(&target);
    device.Clear(Color(0, 0, 0, 255));
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
                              declaration);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            const bool covered = x >= 1 && x <= 2 && y >= 1 && y <= 2;
            const Color pixel = pixels[static_cast<std::size_t>(y * kSize + x)];
            EXPECT_EQ(pixel.getRProperty() > 128, covered) << "pixel " << x << "," << y;
        }
    }
}

// AM4-144: GraphicsDevice's texture and sampler slots are what a compiled draw samples. EffectPass
// .Apply() writes the pass's assignments there, and a game may replace a slot before drawing --
// Direct3D 9 device state, which XNA keeps, and EasyGL's reading. The pass assigns RED to slot 0
// through its texture parameter; the game then puts BLUE there; the draw must sample BLUE.
TEST(MetalCompiledEffectDrawTest, ADeviceTextureSlotReplacedAfterApplyIsTheOneSampled)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    using CNA::TestSupport::SamplingQuadVertex;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector4;
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the Metal renderer";

    Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect({
        {Fx::SampMagFilter, Fx::FilterPoint}, {Fx::SampMinFilter, Fx::FilterPoint},
        {Fx::SampMipFilter, Fx::FilterPoint}, {Fx::SampAddressU, Fx::AddressClamp},
        {Fx::SampAddressV, Fx::AddressClamp}}));
    auto& parameters = effect.getParametersProperty();
    parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
    parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    Texture2D red(device, 1, 1);
    Texture2D blue(device, 1, 1);
    const Color redTexel(255, 0, 0, 255), blueTexel(0, 0, 255, 255);
    red.SetData(&redTexel, 1);
    blue.SetData(&blueTexel, 1);
    parameters["FxTexture"]->SetValue(&red);

    SamplingQuadVertex quad[6];
    CNA::TestSupport::FillSamplingQuad(quad, 0.5f, 0.5f);
    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    device.Clear(Color(9, 19, 29, 255));
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    ASSERT_EQ(device.getTexturesProperty()[0], &red) << "Apply publishes the pass's texture to the device";
    device.getTexturesProperty()(0, &blue);
    device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
                              CNA::TestSupport::SamplingQuadDeclaration());
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    Color pixel(0, 0, 0, 0);
    const Rectangle centre(4, 4, 1, 1);
    target.GetData(0, &centre, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getRProperty(), 0, 3);
    EXPECT_NEAR(pixel.getBProperty(), 255, 3) << "the device's slot, replaced after Apply, wins";
}

// AM4-144/146: an ordinary compiled Effect writing oC0 = Tint and oC1 = Tint.yxzw (the fixture's own
// swizzles, CompiledEffectFixtures.hpp `writesMrt`) into an MRT set -- the shape of SDL_GPU's
// SDLGPU-75 legs on Metal. Each attachment the pixel shader writes takes XNA's ColorWriteChannels for
// its own slot (ColorWriteChannels1 for slot 1) and the one BlendState; a {Color, Single} set (XNA
// requires equal bit depths) keeps slot 1's float output exact; and returning to the first state
// reuses the first pipeline.
TEST(MetalCompiledEffectDrawTest, AnMrtPassWritesEachSlotUnderItsOwnWriteMask)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4;
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (RendererOf(device) == nullptr || !CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "this build did not select the Metal renderer with compiled effects";

    constexpr int kSize = 8;
    Effect effect(device, CNA::TestSupport::BuildSyntheticMrtDrawableEffect());
    auto& parameters = effect.getParametersProperty();
    parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
    EffectPass& pass = *effect.getTechniquesProperty()[0]->getPassesProperty()[1];
    struct ClipVertex { float x, y, z; };
    const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    const ClipVertex quad[6] = {
        {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
        {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
    };
    const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
    const auto draw = [&](RenderTarget2D& a, RenderTarget2D& b, const Vector4& tint,
                          const BlendState& blend, const Color& clear) {
        device.SetRenderTargets({RenderTargetBinding(&a), RenderTargetBinding(&b)});
        device.Clear(clear);
        device.setBlendStateProperty(blend);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        parameters["Tint"]->SetValue(tint);
        pass.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
                                  declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    };
    const auto near = [](const Color& got, const Color& want) {
        return std::abs(got.getRProperty() - want.getRProperty()) <= 3 &&
               std::abs(got.getGProperty() - want.getGProperty()) <= 3 &&
               std::abs(got.getBProperty() - want.getBProperty()) <= 3 &&
               std::abs(got.getAProperty() - want.getAProperty()) <= 3;
    };
    const auto text = [](const Color& c) {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    };

    RenderTarget2D a0(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D a1(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
    Color first, second;
    draw(a0, a1, Vector4(0.2f, 0.4f, 0.8f, 1.0f), BlendState::Opaque, Color::Black);
    a0.GetData(0, &centre, &first, 0, 1);
    a1.GetData(0, &centre, &second, 0, 1);
    EXPECT_TRUE(near(first, Color(51, 102, 204, 255))) << "oC0 = Tint, got " << text(first);
    EXPECT_TRUE(near(second, Color(102, 51, 204, 255))) << "oC1 = Tint.yxzw, got " << text(second);

    RenderTarget2D mixed0(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D mixed1(device, kSize, kSize, false, SurfaceFormat::Single, DepthFormat::None);
    draw(mixed0, mixed1, Vector4(0.25f, 0.5f, 0.75f, 1.0f), BlendState::Opaque, Color::Black);
    Color mixedFirst;
    float mixedSecond = -1.0f;
    mixed0.GetData(0, &centre, &mixedFirst, 0, 1);
    mixed1.GetData(0, &centre, &mixedSecond, 0, 1);
    EXPECT_TRUE(near(mixedFirst, Color(64, 128, 191, 255))) << text(mixedFirst);
    EXPECT_FLOAT_EQ(mixedSecond, 0.5f) << "a Single slot 1 keeps oC1.x = Tint.y exactly";

    BlendState maskedAdditive = BlendState::Additive;
    maskedAdditive.setColorWriteChannelsProperty(ColorWriteChannels::Red);
    maskedAdditive.setColorWriteChannels1Property(ColorWriteChannels::Green);
    draw(a0, a1, Vector4(0.2f, 0.4f, 0.8f, 0.5f), maskedAdditive, Color(10, 20, 30, 40));
    a0.GetData(0, &centre, &first, 0, 1);
    a1.GetData(0, &centre, &second, 0, 1);
    EXPECT_TRUE(near(first, Color(36, 20, 30, 40))) << "slot 0 Red mask with Additive, got " << text(first);
    // Additive is SourceAlpha/One: green gets oC1.y = Tint.x = 0.2 times alpha 0.5, over 20.
    EXPECT_TRUE(near(second, Color(10, 46, 30, 40))) << "slot 1 Green mask with Additive, got " << text(second);

    draw(a0, a1, Vector4(0.6f, 0.3f, 0.1f, 1.0f), BlendState::Opaque, Color::Black);
    a0.GetData(0, &centre, &first, 0, 1);
    a1.GetData(0, &centre, &second, 0, 1);
    EXPECT_TRUE(near(first, Color(153, 77, 26, 255))) << text(first);
    EXPECT_TRUE(near(second, Color(77, 153, 26, 255))) << text(second);
}

TEST(MetalCompiledEffectTest, SharedBackendConformanceContract)
{
    // HiDef, as Vulkan's and SDL_GPU's copies of this test ask for: the contract's first line
    // asserts it (its render-state section uses separate alpha blending), and under the default
    // Reach device that assertion was the only thing this test ever ran.
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedPackedVertexColorContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPackedVertexColorContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSpriteBatchPixelOnlyContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchPixelOnlyContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSpriteBatchDeviceTextureSlotContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchDeviceTextureSlotContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSamplerPixelContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    // Every option is on, LOD bias included: on Metal the bias is the sampler's own
    // (MTLSamplerDescriptor.lodBias, macOS/iOS 26; AM4-034) and MaxMipLevel its lodMinClamp.
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedStockLayoutIsolationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectStockLayoutIsolationContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedPassSelectionContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    // HiDef: Reach has no volume textures, so on a default device the volume half of this
    // contract refuses at Texture3D construction and the test skips (WMG-0028's pattern).
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedManyDrawsContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    // plans/plan_fx.md FX-112: 600 compiled draws, which on this renderer means 600 transient
    // uniform-buffer acquisitions crossing the pool's recycle point (WEBGPU-12/59).
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedTruncationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}


// plans/plan_webgpu.md WEBGPU-208's question, on Metal: the LOD bias reaches the SAMPLER'S OWN
// REGISTER, in PIXELS. (Metal carries it on the register's MTLSamplerState.)
//
// The shared sampler contract already proves a bias moves the level, but it declares its sampler
// at `s0` -- so it passes just as well against an implementation that applies "the" bias globally,
// or that always reads index 0 of the injected block. XNA's contract is per sampler register, and
// this is the test that can tell the difference: the SAME effect is built with its sampler at `s2`,
// and nothing else changes. An implementation keyed on slot 0 reads a bias of 0 here and stays on
// level 0.
//
// One texel per pixel across the target, so the computed level of detail is exactly 0 and a +1 bias
// must select level 1 -- the same geometry the shared contract uses, for the same reason: with a
// constant coordinate the derivative is zero and the level is implementation-defined.
TEST(MetalCompiledEffectDrawTest, LodBiasAppliesToTheSamplersOwnRegisterRatherThanSlotZero)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    using CNA::TestSupport::SamplingQuadVertex;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector4;

    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    constexpr int kSize = 8;
    constexpr std::uint32_t kRegister = 2;  // deliberately NOT s0
    const Color background(9, 19, 29, 255);
    const auto declaration = CNA::TestSupport::SamplingQuadDeclaration();

    // Four mip levels, each a flat distinct colour. A real chain is nearly self-similar and a test
    // built on one cannot tell level 1 from level 0 at all.
    const Color levelColors[4] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                  Color(0, 0, 255, 255), Color(255, 255, 0, 255)};
    Texture2D mipped(device, kSize, kSize, /*mipMap=*/true, SurfaceFormat::Color);
    ASSERT_GE(mipped.getLevelCountProperty(), 2);
    for (int level = 0; level < mipped.getLevelCountProperty(); ++level)
    {
        const int extent = std::max(1, kSize >> level);
        const Rectangle whole(0, 0, extent, extent);
        std::vector<Color> texels(static_cast<std::size_t>(extent * extent),
                                  levelColors[std::min(level, 3)]);
        mipped.SetData(level, &whole, texels.data(), 0, static_cast<int>(texels.size()));
    }

    const SamplingQuadVertex ramp[6] = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
    };

    const auto sampleWithBias = [&](float bias) {
        Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            {
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
                {Fx::SampMipMapLodBias, CNA::TestSupport::FloatBits(bias), true},
            },
            kRegister));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        parameters["FxTexture"]->SetValue(&mipped);

        RenderTarget2D target(device, kSize, kSize);
        device.SetRenderTarget(&target);
        device.Clear(background);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                  static_cast<const void*>(ramp), 0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    };

    const Color unbiased = sampleWithBias(0.0f);
    const Color biased = sampleWithBias(1.0f);

    const auto expectLevel = [](const Color& actual, const Color& expected, const char* label) {
        SCOPED_TRACE(label);
        EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
        EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
        EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
    };
    expectLevel(unbiased, levelColors[0], "no bias on s2 keeps the computed level 0");
    expectLevel(biased, levelColors[1],
                "a +1 bias on s2 selects level 1 -- reading slot 0 would have stayed on level 0");
}

// plans/plan_webgpu.md WEBGPU-208's question, on Metal: the bias is the one in force at the draw.
// Metal encodes each draw immediately, so this guards the encoder state rather than a replay; the
// WebGPU commentary below explains the A/B.
//
// This renderer records its draws and replays them at `Present()`, so every piece of pass state a
// draw depends on has to be snapshotted when the draw is issued. The LOD bias is no different, and
// this is the A/B that can tell a captured value from a live one: TWO draws are queued into one
// target with NO readback between them, the first with no bias and the second with +1. Applying the
// second effect's pass is itself the mutation -- it overwrites the sampler state a replay-time read
// would find. An implementation that read the bias at replay would give BOTH halves the last bias
// applied, so the left half would come back on level 1 instead of level 0.
//
// Each half is its own 8x8 quad with the full 0..1 coordinate range across it, which keeps one
// texel per pixel -- and therefore a computed level of exactly 0 -- in both halves.
TEST(MetalCompiledEffectDrawTest, LodBiasIsCapturedWithTheDeferredDrawRatherThanReadAtReplay)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    using CNA::TestSupport::SamplingQuadVertex;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector4;

    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    constexpr int kTexture = 8;
    const Color background(9, 19, 29, 255);
    const auto declaration = CNA::TestSupport::SamplingQuadDeclaration();

    const Color levelColors[4] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                  Color(0, 0, 255, 255), Color(255, 255, 0, 255)};
    Texture2D mipped(device, kTexture, kTexture, /*mipMap=*/true, SurfaceFormat::Color);
    ASSERT_GE(mipped.getLevelCountProperty(), 2);
    for (int level = 0; level < mipped.getLevelCountProperty(); ++level)
    {
        const int extent = std::max(1, kTexture >> level);
        const Rectangle whole(0, 0, extent, extent);
        std::vector<Color> texels(static_cast<std::size_t>(extent * extent),
                                  levelColors[std::min(level, 3)]);
        mipped.SetData(level, &whole, texels.data(), 0, static_cast<int>(texels.size()));
    }

    const auto makeEffect = [&](float bias) {
        auto effect = std::make_unique<Effect>(device, CNA::TestSupport::BuildSyntheticSamplingEffect(
            {
                {Fx::SampMagFilter, Fx::FilterPoint},
                {Fx::SampMinFilter, Fx::FilterPoint},
                {Fx::SampMipFilter, Fx::FilterPoint},
                {Fx::SampAddressU, Fx::AddressClamp},
                {Fx::SampAddressV, Fx::AddressClamp},
                {Fx::SampMipMapLodBias, CNA::TestSupport::FloatBits(bias), true},
            },
            0));
        auto& parameters = effect->getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        parameters["FxTexture"]->SetValue(&mipped);
        return effect;
    };
    auto unbiasedEffect = makeEffect(0.0f);
    auto biasedEffect = makeEffect(1.0f);

    // Half-width quads, each carrying the whole 0..1 coordinate range.
    const auto halfQuad = [](float x0, float x1, SamplingQuadVertex* out) {
        const SamplingQuadVertex quad[6] = {
            {x0,  1.0f, 0.0f, 0.0f, 0.0f}, {x0, -1.0f, 0.0f, 0.0f, 1.0f},
            {x1, -1.0f, 0.0f, 1.0f, 1.0f}, {x0,  1.0f, 0.0f, 0.0f, 0.0f},
            {x1, -1.0f, 0.0f, 1.0f, 1.0f}, {x1,  1.0f, 0.0f, 1.0f, 0.0f},
        };
        for (int i = 0; i < 6; ++i) out[i] = quad[i];
    };
    SamplingQuadVertex left[6];
    SamplingQuadVertex right[6];
    halfQuad(-1.0f, 0.0f, left);
    halfQuad(0.0f, 1.0f, right);

    RenderTarget2D target(device, kTexture * 2, kTexture);
    device.SetRenderTarget(&target);
    device.Clear(background);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);

    unbiasedEffect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList,
                              static_cast<const void*>(left), 0, 2, declaration);
    // The mutation: this Apply() overwrites the sampler state a replay-time read would find.
    biasedEffect->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList,
                              static_cast<const void*>(right), 0, 2, declaration);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color leftPixel(0, 0, 0, 0);
    Color rightPixel(0, 0, 0, 0);
    const Rectangle leftCentre(kTexture / 2, kTexture / 2, 1, 1);
    const Rectangle rightCentre(kTexture + kTexture / 2, kTexture / 2, 1, 1);
    target.GetData(0, &leftCentre, &leftPixel, 0, 1);
    target.GetData(0, &rightCentre, &rightPixel, 0, 1);

    const auto expectLevel = [](const Color& actual, const Color& expected, const char* label) {
        SCOPED_TRACE(label);
        EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
        EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
        EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
    };
    expectLevel(leftPixel, levelColors[0],
                "the FIRST draw keeps its own zero bias even though a +1 pass was applied after it");
    expectLevel(rightPixel, levelColors[1], "the second draw carries its own +1 bias");
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
TEST(MetalCompiledEffectDrawTest, AddressWSelectsADifferentVolumeSliceForEachMode)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    using CNA::TestSupport::SamplingQuadVertexXYZ;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector4;

    // plans/plan_webgpu_modern_graphics.md WMG-0028, the same defect WMG-0005 found in six other
    // rows: a default GraphicsDevice is Reach, Reach has no volume textures at all, and this test
    // builds a Texture3D on its third line. It therefore threw before reaching a single assertion
    // and reported as a renderer failure. tools/platform/profile_dead_tests.py named it
    // DEAD-ON-PROFILE; the fix is to ask for the profile the test actually uses.
    GraphicsDevice device(Microsoft::Xna::Framework::Graphics::GraphicsAdapter::
                              getDefaultAdapterProperty(),
                          Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef,
                          Microsoft::Xna::Framework::Graphics::PresentationParameters{});
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
                {Fx::SampAddressW, static_cast<std::uint32_t>(addressW)},
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
        effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
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
TEST(MetalCompiledEffectDrawTest, SpriteBatchAlternatesCompiledAndStockAcrossBatches)
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


// AM4-144: the Shader Model 1-3 contracts the GLSL route established (EasyGL runs them), on the
// SPIR-V/MSL route. These pass on Metal -- RasterInput because Metal rebases FragCoord to Direct3D
// 9's integer vPos, PredicatedTexkill through CNA's mojoshader-6333f74-spirv-predicated-texkill
// patch; before either, both drew wrong pixels without a word.
TEST(MetalCompiledEffectDrawTest, SharedLoopContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLoopContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSignedLogContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSignedLogContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedNrmWriteMaskContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectNrmWriteMaskContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedDependentTemporaryTextureCoordinateContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDependentTemporaryTextureCoordinateContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedSubroutineContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSubroutineContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedDerivativeContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDerivativeContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedLegacyTextureMatrixContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyTextureMatrixContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedRasterInputContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRasterInputContract(device);
}

TEST(MetalCompiledEffectDrawTest, SharedPredicatedTexkillContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPredicatedTexkillContract(device);
}

// AM4-144: what MojoShader's SPIR-V profile cannot express refuses BY NAME, never draws wrong.
// Each of these is a shared contract the GLSL route passes through CNA's GLSL-profile patches; on
// this route the effect's construction (or, for vertex textures, its draw) throws the reason
// below. A contract that starts drawing here fails this test, which is the point: the construct
// then belongs in the passing list above. LegacyBumpEnvironment runs its TEXBEM and TEXBEML legs
// first -- they pass, with the bump-environment matrix Metal packs into MojoShader's uniform
// block -- and refuses at ps_1_4 BEM.
TEST(MetalCompiledEffectDrawTest, ConstructsTheSpirvProfileCannotExpressRefuseByName)
{
    GraphicsDevice probe;
    if (!CNA::TestSupport::SupportsCompiledEffects(probe))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    const auto expectRefusal = [](const char* label, void (*contract)(GraphicsDevice&),
                                  const char* reason) {
        SCOPED_TRACE(label);
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters());
        try
        {
            contract(device);
            ADD_FAILURE() << label << " drew instead of refusing";
        }
        catch (const std::exception& error)
        {
            EXPECT_NE(std::string(error.what()).find(reason), std::string::npos)
                << label << " refused for another reason: " << error.what();
        }
    };
    using namespace CNA::TestSupport;
    expectRefusal("LegacyExpp", [](GraphicsDevice& d) { RunCompiledEffectLegacyExppContract(d); },
                  "attribute fixup has no matching attribute register");
    expectRefusal("RelativeInputTextureCoordinate",
                  [](GraphicsDevice& d) { RunCompiledEffectRelativeInputTextureCoordinateContract(d); },
                  "relative input array access is unimplemented");
    expectRefusal("Predication", [](GraphicsDevice& d) { RunCompiledEffectPredicationContract(d); },
                  "predicated destinations unsupported");
    expectRefusal("LegacyDepthOutput", [](GraphicsDevice& d) { RunCompiledEffectLegacyDepthOutputContract(d); },
                  "unimplemented in spirv profile");
    expectRefusal("LegacyTextureRemap", [](GraphicsDevice& d) { RunCompiledEffectLegacyTextureRemapContract(d); },
                  "unimplemented in spirv profile");
    expectRefusal("LegacyDependentTexture",
                  [](GraphicsDevice& d) { RunCompiledEffectLegacyDependentTextureContract(d); },
                  "unimplemented in spirv profile");
    expectRefusal("LegacyBumpEnvironment",
                  [](GraphicsDevice& d) { RunCompiledEffectLegacyBumpEnvironmentContract(d); },
                  "BEM unimplemented in spirv profile");
    expectRefusal("SamplerResultSwizzle",
                  [](GraphicsDevice& d) { RunCompiledEffectSamplerResultSwizzleContract(d); },
                  "Swizzle of a value with an unknown type");
    // plans/plan_fx.md FX-109: no CNA renderer routes vertex textures.
    expectRefusal("VertexSampler", [](GraphicsDevice& d) { RunCompiledEffectVertexSamplerContract(d); },
                  "VERTEX shader samples a texture");
    // This one stops at the same SPIR-V swizzle limitation as SamplerResultSwizzle first.
    expectRefusal("VertexSamplerResultSwizzle",
                  [](GraphicsDevice& d) { RunCompiledEffectVertexSamplerResultSwizzleContract(d); },
                  "Swizzle of a value with an unknown type");
    // And this one at FX-109 too: since plans/plan_apple_m4.md AM4-147 Metal stores its Vector4
    // volume and cube textures, so construction reaches the vertex samplers they feed.
    expectRefusal("VertexSamplerDimensions",
                  [](GraphicsDevice& d) { RunCompiledEffectVertexSamplerDimensionsContract(d); },
                  "VERTEX shader samples a texture");
}

#endif  // CNA_METAL_COMPILED_EFFECTS
