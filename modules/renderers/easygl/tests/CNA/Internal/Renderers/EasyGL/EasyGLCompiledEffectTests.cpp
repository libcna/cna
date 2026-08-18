// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-062: the EasyGL compiled-effect runtime and its draw route, exercised directly.
//
// SupportsCompiledEffects() reports true (FX-062 is done: ordinary 3D draws draw with a compiled
// pass, verified by RendersTheAppliedPassesExpectedPixelsIntoARenderTarget below and by
// SharedBackendConformanceContract). Most tests here still go through
// CNA::Internal::Renderers::EasyGL::EasyGLRenderer::CreateCompiledEffect directly rather than the
// public Effect class -- that predates the draw route and stays useful as a lower-level check of
// the runtime itself (reflection, state translation, parameter/texture validation, clone
// independence) independent of whatever draws it.
//
// What this proves that the existence-gate probe does not: the runtime honours CNA's own contract
// -- reflection matching the source, technique and pass selection, parameter and texture
// validation, render state translation, clone independence -- against the same committed fixtures
// the FNA3D and SDL_GPU backends use.

#if defined(CNA_EASYGL_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/EasyGL/EasyGLCompiledEffect.hpp"
#include "CNA/Internal/Renderers/EasyGL/GlProfile.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
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

TEST(EasyGLCompiledEffectTest, TheCapabilityIsTrueNowThatDrawsExecuteTheEffect)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this build did not select the EasyGL renderer";
    // plan_fx.md FX-062: ordinary 3D draws draw with a compiled pass now
    // (RendersTheAppliedPassesExpectedPixelsIntoARenderTarget), verified by a real golden-pixel
    // readback and the FX-060 shared conformance contract (SharedBackendConformanceContract) -- so
    // this capability no longer means "will silently render with a stock shader instead".
    EXPECT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
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

TEST(EasyGLCompiledEffectDrawTest, RefusesADrawWithNoVertexDeclaration)
{
    GraphicsDevice device;
    EasyGLRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the EasyGL renderer";

    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    GpuDrawParams params{};
    params.compiledEffectRuntime = runtime.get();

    // No SetVertexDeclaration call -- DeclaredVertexLayout stays empty. Stride 20 (float3 position
    // + float2 texcoord) is deliberate: it is one of ApplyLayout's recognized fixed-stride
    // fallbacks (GLTF-157 tightened the unrecognized-stride case to refuse rather than silently
    // treat it as position-only), so SetData itself succeeds and the compiled-effect draw path's
    // own declaredElements.empty() refusal -- the thing this test exists to exercise -- is what
    // actually throws.
    auto vb = renderer->CreateVertexBuffer(3);
    struct Vertex { float x, y, z, u, v; };
    const std::vector<Vertex> triangle = {
        {0, 0, 0, 0, 0}, {1, 0, 0, 1, 0}, {0, 1, 0, 0, 1}};
    vb->SetData(triangle.data(), 3, sizeof(Vertex));

    EXPECT_THROW(
        renderer->DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                   Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params),
        System::NotSupportedException);
}

TEST(EasyGLCompiledEffectDrawTest, RendersTheAppliedPassesExpectedPixelsIntoARenderTarget)
{
    // plan_fx.md FX-062 golden-pixel test: draws CnaConformanceEffect.fxb's MainPixelShader
    // (texture-sampling, struct-driven preshader) and FlatPixelShader (no sampling, array-driven
    // preshader) into a RenderTarget2D through the real EasyGLRenderer::DrawPrimitivesEx path and
    // reads the centre pixel back via RenderTarget2D::GetData(), matching the exact bytes
    // independently hand-verified for both the SDL_GPU adapter
    // (SdlGpuCompiledEffectDrawTest.RendersTheAppliedPassesExpectedPixelsIntoARenderTarget) and
    // this renderer's own standalone existence-gate probe
    // (tools/graphics/mojoshader_gl_probe.cpp --render).
    GraphicsDevice device;
    EasyGLRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the EasyGL renderer";

    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    runtime->SetParameterTexture(5, &white);  // FxTexture is parameter 5.

    const VertexDeclaration declaration(20, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    struct Vertex { float x, y, z, u, v; };
    // Full-screen quad in clip space: Transform is the identity, so MainVertexShader's
    // mul(Position, Transform) passes Position straight through -- these need to already be NDC.
    const std::vector<Vertex> quad = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
    };

    auto drawQuadAndReadCentre = [&](int techniqueIndex, int passIndex, const char* label) -> Color
    {
        RenderTarget2D rt(device, 8, 8);
        device.SetRenderTarget(&rt);
        device.Clear(Color(50, 50, 50, 255));  // a colour neither expected result is close to
        // Neither P0 nor P1 assign a CullMode render state of their own (only StatePass does), so
        // CompiledEffectDeviceState::rasterizer is never consulted for these two passes -- it only
        // matters to a pass that itself changes that state group (plan_fx.md FX-071's own finding,
        // confirmed here too). What actually matters at draw time is this renderer's own live GL
        // rasterizer state, set through the ordinary public GraphicsDevice.RasterizerState.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        CompiledEffectDeviceState deviceState;
        CompiledEffectPassStateChanges changes;
        runtime->SetTechnique(techniqueIndex);
        runtime->ApplyPass(passIndex, deviceState, changes);

        GpuDrawParams params{};
        params.compiledEffectRuntime = runtime.get();

        auto vb = renderer->CreateVertexBuffer(6);
        vb->SetVertexDeclaration(declaration);
        vb->SetData(quad.data(), 6, sizeof(Vertex));
        EXPECT_NO_THROW(renderer->DrawPrimitivesEx(
            *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
            Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, params)) << label;

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        rt.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    };

    // Technique 0 pass 0 (P0): MainVertexShader/MainPixelShader -- samples FxTexture, and its
    // preshader combines the Lighting struct's Intensity/Thresholds[0] via saturate().
    const Color mainPixel = drawQuadAndReadCentre(0, 0, "P0/MainPixelShader");
    EXPECT_NEAR(mainPixel.getRProperty(), 3, 3);
    EXPECT_NEAR(mainPixel.getGProperty(), 6, 3);
    EXPECT_NEAR(mainPixel.getBProperty(), 10, 3);
    EXPECT_NEAR(mainPixel.getAProperty(), 13, 3);

    // Technique 1 pass 0 (P1): MainVertexShader/FlatPixelShader -- no sampling at all, its
    // preshader combines the Tint and Weights[2] array parameters via a single MUL_SCALAR.
    const Color flatPixel = drawQuadAndReadCentre(1, 0, "P1/FlatPixelShader");
    EXPECT_NEAR(flatPixel.getRProperty(), 20, 3);
    EXPECT_NEAR(flatPixel.getGProperty(), 41, 3);
    EXPECT_NEAR(flatPixel.getBProperty(), 61, 3);
    EXPECT_NEAR(flatPixel.getAProperty(), 82, 3);
}

TEST(EasyGLCompiledEffectTest, SharedBackendConformanceContract)
{
    // plan_fx.md FX-060/FX-062: the same cross-renderer contract FNA3D's and SDL_GPU's own
    // SharedBackendConformanceContract tests run -- format, reflection, parameter API,
    // techniques/passes, render state, state policy, samplers, texture binding, clone and
    // lifetime -- through the public Effect/GraphicsDevice API, since SupportsCompiledEffects()
    // is true.
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

// plan_fx.md FX-084/FX-086: the shared draw matrix. Each of these renders the compiled effect's
// own Tint parameter into a render target and reads it back, so a draw that silently used a stock
// shader -- or bound an attribute from the wrong stream -- fails instead of passing quietly.

TEST(EasyGLCompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedSamplerPixelContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::CompiledEffectSamplerContractOptions options;
    // OpenGL ES has no GL_TEXTURE_LOD_BIAS at all, so this renderer applies the bias only on
    // the desktop-core profiles -- see docs/sampler-state-support.md. The ES-profile builds run
    // every other section; the bias one is a named gap rather than a weakened assertion.
    options.supportsLodBias = CNA::Internal::Renderers::EasyGL::IsDesktopCoreProfile(
        CNA::Internal::Renderers::EasyGL::ActiveGlProfile());
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device, options);
}

TEST(EasyGLCompiledEffectDrawTest, SharedPassSelectionContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, CompiledDrawObjectsSurviveAContextRecreation)
{
    // plan_fx.md FX-108. The compiled route owns two GL objects outside easy-gl's recovery
    // registry: one shared vertex-array object and, per sampler slot, the row-order-corrected copy
    // of a render-target source. Their creation flags used to stay true across a context
    // recreation while the names behind them died with the old context, so every later compiled
    // draw bound array object 0 and rasterized nothing -- silently.
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    namespace Fx = CNA::TestSupport::EffectFormat;
    Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect({
        {Fx::SampMagFilter, Fx::FilterPoint},
        {Fx::SampMinFilter, Fx::FilterPoint},
        {Fx::SampMipFilter, Fx::FilterPoint},
        {Fx::SampAddressU, Fx::AddressClamp},
        {Fx::SampAddressV, Fx::AddressClamp},
    }));
    auto& parameters = effect.getParametersProperty();
    parameters["Transform"]->SetValue(Microsoft::Xna::Framework::Matrix::getIdentityProperty());
    parameters["Tint"]->SetValue(Microsoft::Xna::Framework::Vector4(1.0f, 1.0f, 1.0f, 1.0f));

    Texture2D source(device, 1, 1);
    const Color sourcePixel[1] = {Color(255, 0, 0, 255)};
    source.SetData(sourcePixel, 1);
    parameters["FxTexture"]->SetValue(&source);

    const auto drawAndRead = [&]() -> Color {
        CNA::TestSupport::SamplingQuadVertex quad[6];
        CNA::TestSupport::FillSamplingQuad(quad, 0.5f, 0.5f);
        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
                                  CNA::TestSupport::SamplingQuadDeclaration());
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color pixel(0, 0, 0, 0);
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    };

    const Color before = drawAndRead();
    ASSERT_NEAR(before.getRProperty(), 255, 3) << "the baseline draw must sample the red texel";

    device.GetRenderer().DebugSimulateContextLoss();

    // What the renderer may legitimately do here is refuse: this renderer does not yet recreate
    // its MojoShader context, which is a documented limitation. What it must NOT do is silently
    // draw nothing, or draw with a stale array object, which is what an unreset creation flag
    // produced.
    try
    {
        const Color after = drawAndRead();
        EXPECT_NEAR(after.getRProperty(), 255, 3)
            << "a compiled draw after a context recreation must rebuild its own GL objects";
        EXPECT_NEAR(after.getGProperty(), 0, 3);
    }
    catch (const std::exception& error)
    {
        EXPECT_FALSE(std::string(error.what()).empty())
            << "an explicit refusal must name what could not be restored";
    }
}

#endif  // CNA_EASYGL_COMPILED_EFFECTS
