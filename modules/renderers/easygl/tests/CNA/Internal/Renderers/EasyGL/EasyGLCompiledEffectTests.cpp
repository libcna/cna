// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-062: the EasyGL compiled-effect runtime and its draw route, exercised directly.
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
#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
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
        const std::filesystem::path path = CNA::TestSupport::CompiledEffectDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::vector<std::uint8_t> LoadEffectFixture(const std::string& name)
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectFixtureDirectory() / name;
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
    // plans/plan_fx.md FX-062: ordinary 3D draws draw with a compiled pass now
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

TEST(EasyGLCompiledEffectTest, LegacyAssignmentMetadataNeedsNoValueStorage)
{
    MOJOSHADER_effectState states[2]{};
    states[0].type = MOJOSHADER_RS_PIXELSHADERCONSTANT;
    states[1].type = MOJOSHADER_RS_SETSAMPLER;
    MOJOSHADER_effectStateChanges nativeChanges{};
    nativeChanges.render_state_change_count = 2;
    nativeChanges.render_state_changes = states;

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_NO_THROW(
        CNA::Internal::Renderers::MojoShaderEffect::TranslateRenderStates(
            nativeChanges, deviceState, changes));
    EXPECT_FALSE(changes.blendChanged);
    EXPECT_FALSE(changes.depthStencilChanged);
    EXPECT_FALSE(changes.rasterizerChanged);
}

TEST(EasyGLCompiledEffectTest, AuthenticXna4LegacyPassBindsSamplersAndSurvivesClone)
{
    GraphicsDevice device;
    EasyGLRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the EasyGL renderer";
    const std::vector<std::uint8_t> bytes =
        LoadEffectFixture("racing-normal-mapping-xna4.fxb");
    ASSERT_EQ(bytes.size(), 82656u);
    auto runtime = renderer->CreateCompiledEffect(bytes.data(), bytes.size());
    ASSERT_NE(runtime, nullptr);

    const auto& description = runtime->GetDescription();
    const auto diffuseParameter = std::find_if(
        description.parameters.begin(), description.parameters.end(),
        [](const auto& parameter) { return parameter.name == "diffuseTexture"; });
    const auto normalParameter = std::find_if(
        description.parameters.begin(), description.parameters.end(),
        [](const auto& parameter) { return parameter.name == "normalTexture"; });
    const auto diffuseTechnique = std::find_if(
        description.techniques.begin(), description.techniques.end(),
        [](const auto& technique) { return technique.name == "Diffuse"; });
    ASSERT_NE(diffuseParameter, description.parameters.end());
    ASSERT_NE(normalParameter, description.parameters.end());
    ASSERT_NE(diffuseTechnique, description.techniques.end());

    Texture2D diffuse = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255});
    Texture2D normal = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{128, 128, 255, 255});
    runtime->SetParameterTexture(diffuseParameter->runtimeIndex, &diffuse);
    runtime->SetParameterTexture(normalParameter->runtimeIndex, &normal);
    runtime->SetTechnique(static_cast<std::uint32_t>(
        std::distance(description.techniques.begin(), diffuseTechnique)));

    auto expectLegacyBindings = [&](ICompiledEffectRuntime& effect)
    {
        CompiledEffectDeviceState deviceState;
        CompiledEffectPassStateChanges changes;
        ASSERT_NO_THROW(effect.ApplyPass(0, deviceState, changes));
        const auto diffuseBinding = std::find_if(
            changes.samplers.begin(), changes.samplers.end(),
            [&](const auto& change)
            {
                return !change.vertexStage && change.slot == 0 &&
                    change.textureChanged && change.texture == &diffuse;
            });
        const auto normalBinding = std::find_if(
            changes.samplers.begin(), changes.samplers.end(),
            [&](const auto& change)
            {
                return !change.vertexStage && change.slot == 1 &&
                    change.textureChanged && change.texture == &normal;
            });
        EXPECT_NE(diffuseBinding, changes.samplers.end());
        EXPECT_NE(normalBinding, changes.samplers.end());
    };

    expectLegacyBindings(*runtime);
    auto clone = runtime->Clone();
    ASSERT_NE(clone, nullptr);
    expectLegacyBindings(*clone);
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
    // plans/plan_fx.md FX-062 golden-pixel test: draws CnaConformanceEffect.fxb's MainPixelShader
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
        // matters to a pass that itself changes that state group (plans/plan_fx.md FX-071's own finding,
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

TEST(EasyGLCompiledEffectDrawTest, AShaderMaySampleANullTextureLikeFnaOpenGL)
{
    // SAMPLE-014/FX-113: XNA/FNA permits a texture parameter to remain null. FNA's Effect leaves
    // the GraphicsDevice texture slot alone and FNA3D's OpenGL VerifySampler explicitly unbinds
    // the slot when it is also null. Spacewar relies on this for its non-reflective shapes.
    GraphicsDevice device;
    EasyGLRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the EasyGL renderer";

    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);
    // Do not assign FxTexture (parameter 5), and leave GraphicsDevice.Textures[0] null.

    const VertexDeclaration declaration(20, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    struct Vertex { float x, y, z, u, v; };
    const std::vector<Vertex> quad = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
    };
    auto vb = renderer->CreateVertexBuffer(6);
    vb->SetVertexDeclaration(declaration);
    vb->SetData(quad.data(), 6, sizeof(Vertex));

    RenderTarget2D rt(device, 8, 8);
    device.SetRenderTarget(&rt);
    device.Clear(Color(50, 50, 50, 255));
    device.setRasterizerStateProperty(RasterizerState::CullNone);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    GpuDrawParams params{};
    params.compiledEffectRuntime = runtime.get();
    EXPECT_NO_THROW(renderer->DrawPrimitivesEx(
        *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, params));

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
}

TEST(EasyGLCompiledEffectTest, AppliedSamplerRetainsNativeTextureAfterValueWrapperReplacement)
{
    // SAMPLE-014/FX-114: XNA's Texture objects are managed references. Spacewar faithfully calls
    // Content.Load<TextureCube>() in Render and replaces its C++ value wrapper even on a cache
    // hit. The applied sampler must retain the cached native resource rather than dereference the
    // wrapper address left in GraphicsDevice.Textures by the previous frame.
    GraphicsDevice device;
    auto runtime = CreateRuntime(device, "EnvironmentMapEffect.fxb");
    ASSERT_NE(runtime, nullptr) << "this build did not select the EasyGL renderer";

    const auto& parameters = runtime->GetDescription().parameters;
    const auto environmentMap = std::find_if(
        parameters.begin(), parameters.end(), [](const auto& parameter)
        {
            return parameter.name == "EnvironmentMap";
        });
    ASSERT_NE(environmentMap, parameters.end());

    std::weak_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> nativeResource;
    {
        std::optional<TextureCube> cube;
        cube.emplace(device, 1, false, SurfaceFormat::Color);
        nativeResource = cube->GetRenderer().weak_from_this();
        runtime->SetParameterTexture(environmentMap->runtimeIndex, &*cube);

        CompiledEffectDeviceState deviceState;
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(0, deviceState, changes);
        cube.reset();
    }

    EXPECT_FALSE(nativeResource.expired());
    runtime->SetParameterTexture(environmentMap->runtimeIndex, nullptr);
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_NO_THROW(runtime->ApplyPass(0, deviceState, changes));
    EXPECT_FALSE(nativeResource.expired())
        << "a null Effect parameter leaves the previously applied XNA sampler binding intact";

    runtime.reset();
    EXPECT_TRUE(nativeResource.expired());
}

TEST(EasyGLCompiledEffectTest, SharedBackendConformanceContract)
{
    // plans/plan_fx.md FX-060/FX-062: the same cross-renderer contract FNA3D's and SDL_GPU's own
    // SharedBackendConformanceContract tests run -- format, reflection, parameter API,
    // techniques/passes, render state, state policy, samplers, texture binding, clone and
    // lifetime -- through the public Effect/GraphicsDevice API, since SupportsCompiledEffects()
    // is true.
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(EasyGLCompiledEffectTest, PixelShaderOnlyPassAppliesOnDesktopCoreContext)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeSampler = true;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));

    EXPECT_NO_THROW(effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
}

// plans/plan_fx.md FX-084/FX-086: the shared draw matrix. Each of these renders the compiled effect's
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

TEST(EasyGLCompiledEffectDrawTest, SpriteBatchInheritsStockVertexShaderForPixelOnlyEffect)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeSampler = true;
    options.pixelShaderSamplesTexture = true;
    options.samplerRegister = 1;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Tint"]->SetValue(
        Microsoft::Xna::Framework::Vector4::One);

    Texture2D sprite(device, 1, 1);
    const Color red[1] = {Color::Red};
    sprite.SetData(red, 1);
    Texture2D secondary(device, 1, 1);
    const Color green[1] = {Color::Green};
    secondary.SetData(green, 1);
    device.getTexturesProperty()(1, &secondary);
    device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &effect);
    batch.Draw(sprite, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White);
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color actual(0, 0, 0, 0);
    const Rectangle centre(4, 4, 1, 1);
    target.GetData(0, &centre, &actual, 0, 1);
    EXPECT_NEAR(actual.getRProperty(), 0, 3);
    EXPECT_NEAR(actual.getGProperty(), 128, 3);
    EXPECT_NEAR(actual.getBProperty(), 0, 3);
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

TEST(EasyGLCompiledEffectDrawTest, MultipleRenderTargetSamplersKeepTheirOwnTextureUnits)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    constexpr int size = 32;
    constexpr int blockSize = 4;
    const Color black(0, 0, 0, 255);
    const Color white(255, 255, 255, 255);
    Texture2D pixel(device, 1, 1);
    pixel.SetData(&white, 1);

    RenderTarget2D patterned(device, size, size);
    device.SetRenderTarget(&patterned);
    device.Clear(black);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
    for (int blockY = 0; blockY < size / blockSize; ++blockY)
    {
        for (int blockX = 0; blockX < size / blockSize; ++blockX)
        {
            if ((blockX + blockY) % 2 == 0)
            {
                batch.Draw(pixel,
                           Rectangle(blockX * blockSize, blockY * blockSize,
                                     blockSize, blockSize),
                           white);
            }
        }
    }
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    RenderTarget2D neutral(device, size, size);
    device.SetRenderTarget(&neutral);
    device.Clear(white);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    const std::vector<std::uint8_t> bytes = LoadEffect("DualTextureEffect.fxb");
    ASSERT_FALSE(bytes.empty());
    Effect effect(device, bytes);
    auto& parameters = effect.getParametersProperty();
    parameters["Texture"]->SetValue(&patterned);
    parameters["Texture2"]->SetValue(&neutral);
    parameters["DiffuseColor"]->SetValue(
        Microsoft::Xna::Framework::Vector4::One);
    parameters["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    parameters["ShaderIndex"]->SetValue(1);

    struct DualTextureVertex
    {
        float x, y, z;
        float u0, v0;
        float u1, v1;
    };
    const DualTextureVertex vertices[6] = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    };
    const VertexDeclaration declaration(static_cast<int>(sizeof(DualTextureVertex)), {
        VertexElement(0, VertexElementFormat::Vector3,
                      VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2,
                      VertexElementUsage::TextureCoordinate, 0),
        VertexElement(20, VertexElementFormat::Vector2,
                      VertexElementUsage::TextureCoordinate, 1),
    });
    VertexBuffer vertexBuffer(device, declaration, 6, BufferUsage::WriteOnly);
    vertexBuffer.SetData(vertices, 6);

    RenderTarget2D result(device, size, size);
    device.SetRenderTarget(&result);
    device.Clear(Color::Magenta);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0].getPassesProperty()[0].Apply();
    device.SetVertexBuffer(&vertexBuffer);
    device.setIndicesProperty(nullptr);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::vector<Color> pixels(static_cast<std::size_t>(size * size));
    result.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (int blockY = 0; blockY < size / blockSize; ++blockY)
    {
        for (int blockX = 0; blockX < size / blockSize; ++blockX)
        {
            SCOPED_TRACE("block " + std::to_string(blockX) + "," +
                         std::to_string(blockY));
            const int x = blockX * blockSize + blockSize / 2;
            const int y = blockY * blockSize + blockSize / 2;
            const Color expected = (blockX + blockY) % 2 == 0 ? white : black;
            const Color actual = pixels[static_cast<std::size_t>(y * size + x)];
            EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
            EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
            EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
        }
    }
}

TEST(EasyGLCompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
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
    // plans/plan_fx.md FX-108. A live compiled Effect must retain its bytecode, parameter state and
    // texture bindings across a context recreation. The renderer-owned MojoShader context, shared
    // VAO and scratch copies all belong to the old context and must be rebuilt, not merely have
    // their creation flags cleared.
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

    TextureCube cube(device, 2, false, SurfaceFormat::Color);
    const Color cubePixels[4] = {
        Color(7, 17, 27, 255), Color(37, 47, 57, 255),
        Color(67, 77, 87, 255), Color(97, 107, 117, 255)};
    cube.SetData(CubeMapFace::PositiveX, cubePixels, 4);

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

    const Color after = drawAndRead();
    EXPECT_NEAR(after.getRProperty(), 255, 3)
        << "a compiled draw after a context recreation must rebuild its native effect";
    EXPECT_NEAR(after.getGProperty(), 0, 3);

    Color restoredCubePixels[4];
    cube.GetData(CubeMapFace::PositiveX, restoredCubePixels, 4);
    for (std::size_t index = 0; index < std::size(cubePixels); ++index)
        EXPECT_EQ(restoredCubePixels[index], cubePixels[index]);
}

TEST(EasyGLCompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedManyDrawsContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(EasyGLCompiledEffectDrawTest, SharedTruncationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}


// SAMPLE-028: MojoShader ends every generated vertex shader with Direct3D 9's clip-space depth
// conversion (`gl_Position.z = gl_Position.z * 2.0 - gl_Position.w`), because OpenGL's clip volume
// is z in [-w, w] where Direct3D's is [0, w]. EasyGL's own stock shaders do NOT do that -- they
// emit the XNA projection's Direct3D-style z unchanged. Without a compensating GL depth range the
// two encodings disagree, and compiled-effect geometry is depth-tested against everything else on
// a different scale: it wins where it should lose.
//
// Found on ColorReplacementSample_4_0, where the car body (a compiled effect) swallowed the
// headlight lens and thin window edges that ordinary BasicEffect parts draw in front of it. This
// pins the ordering directly: a compiled-effect quad placed BEHIND stock geometry must not
// overwrite it.
TEST(EasyGLCompiledEffectDrawTest, IsDepthTestedOnTheSameScaleAsStockGeometry)
{
    GraphicsDevice device;
    EasyGLRenderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the EasyGL renderer";

    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    runtime->SetParameterTexture(5, &white);  // FxTexture is parameter 5.

    RenderTarget2D rt(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24);
    device.SetRenderTarget(&rt);
    device.Clear(Color(50, 50, 50, 255));
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);

    // Both quads are handed over already in clip space -- the transforms are the identity, so the
    // compiled effect's own `mul(Position, Transform)` passes them straight through, exactly as
    // the sibling golden-pixel test relies on. z is therefore Direct3D-style: 0 is the near plane.
    struct Vertex { float x, y, z; std::uint32_t rgba; };
    const std::uint32_t kRed = 0xFF0000FFu;   // AABBGGRR: opaque red
    auto fullScreenQuad = [kRed](float z) {
        return std::vector<Vertex>{
            {-1.0f,  1.0f, z, kRed}, {-1.0f, -1.0f, z, kRed}, { 1.0f, -1.0f, z, kRed},
            {-1.0f,  1.0f, z, kRed}, { 1.0f, -1.0f, z, kRed}, { 1.0f,  1.0f, z, kRed}};
    };
    const VertexDeclaration stockDeclaration(16, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
    });

    // 1. Stock geometry at z = 0.6. The two z values are chosen so the encodings disagree about
    //    the ORDER, which is the whole defect: stock lands at (0.6+1)/2 = 0.800, and a compiled
    //    quad at z = 0.7 lands at 0.700 without the compensation (wrongly nearer) but at
    //    0.5 + 0.5*0.7 = 0.850 with it (correctly farther). Any pair with
    //    z_stock < z_fx < (z_stock+1)/2 exposes it; this one is the sample's own situation, a lens
    //    sitting just in front of the body.
    const auto nearQuad = fullScreenQuad(0.6f);
    auto stockVb = renderer->CreateVertexBuffer(6);
    stockVb->SetVertexDeclaration(stockDeclaration);
    stockVb->SetData(nearQuad.data(), 6, sizeof(Vertex));
    GpuDrawParams stockParams{};
    ASSERT_NO_THROW(renderer->DrawPrimitivesEx(
        *stockVb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, stockParams));

    // 2. A compiled-effect quad strictly BEHIND it. It must be rejected by the depth test.
    struct FxVertex { float x, y, z, u, v; };
    const std::vector<FxVertex> farQuad = {
        {-1.0f,  1.0f, 0.7f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.7f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.7f, 1.0f, 1.0f}, {-1.0f,  1.0f, 0.7f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.7f, 1.0f, 1.0f}, { 1.0f,  1.0f, 0.7f, 1.0f, 0.0f}};
    const VertexDeclaration fxDeclaration(20, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    auto fxVb = renderer->CreateVertexBuffer(6);
    fxVb->SetVertexDeclaration(fxDeclaration);
    fxVb->SetData(farQuad.data(), 6, sizeof(FxVertex));
    GpuDrawParams fxParams{};
    fxParams.compiledEffectRuntime = runtime.get();
    ASSERT_NO_THROW(renderer->DrawPrimitivesEx(
        *fxVb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, fxParams));

    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::vector<Color> pixels(64);
    rt.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color centre = pixels[8 * 4 + 4];
    EXPECT_EQ(centre.getRProperty(), 255)
        << "the stock quad is nearer, so it must still own the pixel";
    EXPECT_EQ(centre.getGProperty(), 0)
        << "a compiled-effect quad drawn BEHIND stock geometry overwrote it -- the two paths are "
           "encoding depth on different scales";
    EXPECT_EQ(centre.getBProperty(), 0);
}

#endif  // CNA_EASYGL_COMPILED_EFFECTS
