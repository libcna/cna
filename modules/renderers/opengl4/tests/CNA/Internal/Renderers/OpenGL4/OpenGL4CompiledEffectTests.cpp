// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0020: the OpenGL4 compiled-effect runtime and its draw
// routes, exercised directly.
//
// OpenGL4's compiled-effect support is EasyGL's desktop-profile route (plans/plan_fx.md FX-062)
// over this renderer's own resources, so this suite is EasyGL's own
// (modules/renderers/easygl/tests/.../EasyGLCompiledEffectTests.cpp) where that suite is
// renderer-neutral in substance: the FX-060 shared conformance contracts, the Direct3D 9 semantic
// contracts, and the draw-level golden-pixel tests that go through the public GraphicsDevice,
// Effect and SpriteBatch API. The lower-level runtime checks at the top reach the runtime through
// OpenGL4Renderer::CreateCompiledEffect. EasyGL's parser-validation matrix (the Rejects*/Accepts*
// suites) is not repeated: it exercises the shared MojoShader parser, not a renderer's route.

#if defined(CNA_OPENGL4_COMPILED_EFFECTS)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4CompiledEffect.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
    using CNA::Internal::Renderers::GpuDrawParams;
    using CNA::Internal::Renderers::ICompiledEffectRuntime;
    using CNA::Internal::Renderers::OpenGL4::OpenGL4Renderer;
    using CNA::Internal::Renderers::OpenGL4::OpenGL4RenderTargetRenderer;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;

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

    /// The device's OpenGL4 renderer, or null when this run selected another renderer.
    OpenGL4Renderer* RendererOf(GraphicsDevice& device)
    {
        return dynamic_cast<OpenGL4Renderer*>(&device.GetRenderer());
    }

    /// True when the device runs on OpenGL4 and that renderer executes compiled effects. A
    /// multi-renderer build can select another renderer at run time; that run tests nothing here.
    bool RunsCompiledEffectsOnOpenGL4(GraphicsDevice& device)
    {
        return device.GetGraphicsRendererType() == CNA::GraphicsRendererType::OpenGL4 &&
               CNA::TestSupport::SupportsCompiledEffects(device);
    }

    std::unique_ptr<ICompiledEffectRuntime> CreateRuntime(GraphicsDevice& device,
                                                          const std::string& name)
    {
        OpenGL4Renderer* renderer = RendererOf(device);
        if (renderer == nullptr) return nullptr;
        const std::vector<std::uint8_t> bytes = LoadEffect(name);
        if (bytes.empty()) return nullptr;
        return renderer->CreateCompiledEffect(bytes.data(), bytes.size());
    }

    /// Reads texel (0,0) of a GL texture through a scratch read framebuffer.
    template <typename T>
    void ReadFirstTexel(unsigned int texture, GLenum format, GLenum type, T* out)
    {
        using namespace CNA::Internal::Renderers::OpenGL4::GL4;
        GLint previousReadFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        GLuint framebuffer = 0;
        gl4_glGenFramebuffers(1, &framebuffer);
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        gl4_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   texture, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 1, 1, format, type, out);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        gl4_glDeleteFramebuffers(1, &framebuffer);
    }
}

TEST(OpenGL4CompiledEffectTest, TheCapabilityIsTrueWhenTheOptionIsBuilt)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    EXPECT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
}

TEST(OpenGL4CompiledEffectTest, ReflectionMatchesTheConformanceSource)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    // Six parameters, not seven: a sampler is not a reflected parameter, which is FNA's own
    // answer for this binary (tests/fixtures/compiled-effects/fna-effect-reflection.json).
    const auto& description = runtime->GetDescription();
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

TEST(OpenGL4CompiledEffectTest, AuthenticXna4LegacyPassBindsSamplersAndSurvivesClone)
{
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    const std::vector<std::uint8_t> bytes = LoadEffectFixture("racing-normal-mapping-xna4.fxb");
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
            changes.samplers.begin(), changes.samplers.end(), [&](const auto& change)
            {
                return !change.vertexStage && change.slot == 0 && change.textureChanged &&
                       change.texture == &diffuse;
            });
        const auto normalBinding = std::find_if(
            changes.samplers.begin(), changes.samplers.end(), [&](const auto& change)
            {
                return !change.vertexStage && change.slot == 1 && change.textureChanged &&
                       change.texture == &normal;
            });
        EXPECT_NE(diffuseBinding, changes.samplers.end());
        EXPECT_NE(normalBinding, changes.samplers.end());
    };

    expectLegacyBindings(*runtime);
    auto clone = runtime->Clone();
    ASSERT_NE(clone, nullptr);
    expectLegacyBindings(*clone);
}

TEST(OpenGL4CompiledEffectTest, OutOfRangeTechniqueAndPassAreRejected)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    EXPECT_THROW(runtime->SetTechnique(99), std::out_of_range);
    runtime->SetTechnique(1);
    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    EXPECT_THROW(runtime->ApplyPass(5, deviceState, changes), std::out_of_range);
}

TEST(OpenGL4CompiledEffectTest, ParameterWritesAreBoundedAndTypeChecked)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    const float tooMuch[64] = {};
    EXPECT_THROW(runtime->SetParameterValue(0, tooMuch, sizeof(tooMuch)), std::invalid_argument);
    EXPECT_THROW(runtime->SetParameterValue(999, tooMuch, 4), std::out_of_range);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    EXPECT_THROW(runtime->SetParameterTexture(999, &white), std::out_of_range);
    // Parameter 0 is the scalar Gain, not a texture.
    EXPECT_THROW(runtime->SetParameterTexture(0, &white), std::invalid_argument);
}

TEST(OpenGL4CompiledEffectTest, MalformedBytecodeIsRejected)
{
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

    const std::vector<std::uint8_t> garbage(512, 0xAB);
    EXPECT_ANY_THROW(renderer->CreateCompiledEffect(garbage.data(), garbage.size()));
    EXPECT_THROW(renderer->CreateCompiledEffect(nullptr, 0), std::invalid_argument);
}

TEST(OpenGL4CompiledEffectTest, CloneCarriesItsOwnValuesAndSurvivesTheSource)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

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

TEST(OpenGL4CompiledEffectTest, EveryCommittedStockEffectParses)
{
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

    for (const char* name : {"SpriteEffect.fxb", "BasicEffect.fxb", "AlphaTestEffect.fxb",
                             "DualTextureEffect.fxb", "EnvironmentMapEffect.fxb",
                             "SkinnedEffect.fxb"})
    {
        auto runtime = CreateRuntime(device, name);
        ASSERT_NE(runtime, nullptr) << name;
        EXPECT_FALSE(runtime->GetDescription().techniques.empty()) << name;
    }
}

TEST(OpenGL4CompiledEffectTest, AnEffectThatOutlivesItsDeviceReleasesSafely)
{
    // GL4-0020: a compiled effect can outlive the device that created it (the CNAEXT engine layer
    // holds SpriteBatch-owned effects by shared_ptr). The renderer releases the native effect
    // while its GL context is still current; the later destruction must not reach MojoShader or
    // the destroyed renderer, and the orphan refuses further use by name.
    std::unique_ptr<ICompiledEffectRuntime> orphan;
    {
        GraphicsDevice device;
        if (RendererOf(device) == nullptr)
            GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
        orphan = CreateRuntime(device, "CnaConformanceEffect.fxb");
        ASSERT_NE(orphan, nullptr);
        CompiledEffectDeviceState deviceState;
        CompiledEffectPassStateChanges changes;
        orphan->ApplyPass(0, deviceState, changes);
    }
    EXPECT_EQ(orphan->GetDescription().techniques.size(), 2u);
    EXPECT_THROW(orphan->SetTechnique(0), std::runtime_error);
    EXPECT_THROW((void)orphan->Clone(), std::runtime_error);
    EXPECT_NO_THROW(orphan.reset());
}

TEST(OpenGL4CompiledEffectDrawTest, RefusesADrawWithNoVertexDeclaration)
{
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    CompiledEffectDeviceState deviceState;
    CompiledEffectPassStateChanges changes;
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, deviceState, changes);

    GpuDrawParams params{};
    params.compiledEffectRuntime = runtime.get();

    // Stride 20 is one of ApplyLayout's recognised fixed-stride shapes, so SetData succeeds and
    // the compiled route's own missing-declaration refusal is what throws.
    auto vb = renderer->CreateVertexBuffer(3);
    struct Vertex { float x, y, z, u, v; };
    const std::vector<Vertex> triangle = {{0, 0, 0, 0, 0}, {1, 0, 0, 1, 0}, {0, 1, 0, 0, 1}};
    vb->SetData(triangle.data(), 3, sizeof(Vertex));

    EXPECT_THROW(
        renderer->DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                   Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1,
                                   params),
        System::NotSupportedException);
}

TEST(OpenGL4CompiledEffectDrawTest, RendersTheAppliedPassesExpectedPixelsIntoARenderTarget)
{
    // plans/plan_fx.md FX-062 golden pixels: CnaConformanceEffect.fxb's MainPixelShader (texture
    // sampling, struct-driven preshader) and FlatPixelShader (no sampling, array-driven preshader)
    // drawn into a RenderTarget2D and read back -- the same bytes the EasyGL, SDL_GPU and
    // standalone MojoShader probe tests verify.
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

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
    // Full-screen quad in clip space: Transform is the identity.
    const std::vector<Vertex> quad = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
    };

    auto drawQuadAndReadCentre = [&](int techniqueIndex, int passIndex, const char* label) -> Color
    {
        RenderTarget2D rt(device, 8, 8);
        device.SetRenderTarget(&rt);
        device.Clear(Color(50, 50, 50, 255));
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

    const Color mainPixel = drawQuadAndReadCentre(0, 0, "P0/MainPixelShader");
    EXPECT_NEAR(mainPixel.getRProperty(), 3, 3);
    EXPECT_NEAR(mainPixel.getGProperty(), 6, 3);
    EXPECT_NEAR(mainPixel.getBProperty(), 10, 3);
    EXPECT_NEAR(mainPixel.getAProperty(), 13, 3);

    const Color flatPixel = drawQuadAndReadCentre(1, 0, "P1/FlatPixelShader");
    EXPECT_NEAR(flatPixel.getRProperty(), 20, 3);
    EXPECT_NEAR(flatPixel.getGProperty(), 41, 3);
    EXPECT_NEAR(flatPixel.getBProperty(), 61, 3);
    EXPECT_NEAR(flatPixel.getAProperty(), 82, 3);
}

TEST(OpenGL4CompiledEffectDrawTest, AShaderMaySampleANullTextureLikeFnaOpenGL)
{
    // SAMPLE-014/FX-113: a texture parameter may remain null. FNA3D's OpenGL VerifySampler unbinds
    // the slot when the device texture is null too; the draw proceeds.
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

    auto runtime = CreateRuntime(device, "CnaConformanceEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    const VertexDeclaration declaration(20, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    struct Vertex { float x, y, z, u, v; };
    const std::vector<Vertex> quad = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
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

TEST(OpenGL4CompiledEffectTest, AppliedSamplerRetainsNativeTextureAfterValueWrapperReplacement)
{
    // SAMPLE-014/FX-114: an applied sampler keeps the cached native resource alive rather than a
    // value wrapper that may since have been replaced.
    GraphicsDevice device;
    if (RendererOf(device) == nullptr)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto runtime = CreateRuntime(device, "EnvironmentMapEffect.fxb");
    ASSERT_NE(runtime, nullptr);

    const auto& parameters = runtime->GetDescription().parameters;
    const auto environmentMap = std::find_if(
        parameters.begin(), parameters.end(),
        [](const auto& parameter) { return parameter.name == "EnvironmentMap"; });
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

TEST(OpenGL4CompiledEffectDrawTest, SharedSamplerPixelContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    // Desktop core has GL_TEXTURE_LOD_BIAS and GL_TEXTURE_MIN_LOD, so every section runs.
    CNA::TestSupport::CompiledEffectSamplerContractOptions options;
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device, options);
}

TEST(OpenGL4CompiledEffectDrawTest, FlippedSourceRetainsFormatChannelExpansionAndFullFloatPrecision)
{
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single))
        GTEST_SKIP() << "this context cannot render to SurfaceFormat.Single";

    RenderTarget2D source(device, 2, 2, false, SurfaceFormat::Single, DepthFormat::None);
    ASSERT_EQ(source.getFormatProperty(), SurfaceFormat::Single);
    const float values[4]{0.12345f, 0.12345f, 0.12345f, 0.12345f};
    source.SetData(values, 4);
    auto* nativeSource = dynamic_cast<OpenGL4RenderTargetRenderer*>(&source.GetRenderer());
    ASSERT_NE(nativeSource, nullptr);
    const unsigned int corrected = renderer->AcquireCompiledEffectFlippedSourceEXT(1, *nativeSource);
    ASSERT_NE(corrected, 0u);

    // glReadPixels reads the stored value (a swizzle applies to sampling only): the copy keeps
    // the target's R32F storage, so a value far below 1/255 survives exactly.
    float actual = -1.0f;
    ReadFirstTexel(corrected, GL_RED, GL_FLOAT, &actual);
    EXPECT_NEAR(actual, values[0], 0.000001f);

    // The copy is sampled in the target's place, so it must carry the target's Direct3D 9
    // expansion of the missing channels: SurfaceFormat.Single samples as (R, 1, 1, 1).
    using namespace CNA::Internal::Renderers::OpenGL4::GL4;
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, corrected);
    GLint swizzleG = 0, swizzleB = 0, swizzleA = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, &swizzleG);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, &swizzleB);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, &swizzleA);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    EXPECT_EQ(swizzleG, GL_ONE);
    EXPECT_EQ(swizzleB, GL_ONE);
    EXPECT_EQ(swizzleA, GL_ONE);

    RenderTarget2D colorSource(device, 2, 2, false, SurfaceFormat::Color, DepthFormat::None);
    const Color colorValues[4]{Color(17, 91, 207, 255), Color(17, 91, 207, 255),
                               Color(17, 91, 207, 255), Color(17, 91, 207, 255)};
    colorSource.SetData(colorValues, 4);
    auto* nativeColor = dynamic_cast<OpenGL4RenderTargetRenderer*>(&colorSource.GetRenderer());
    ASSERT_NE(nativeColor, nullptr);
    const unsigned int correctedColor =
        renderer->AcquireCompiledEffectFlippedSourceEXT(1, *nativeColor);
    std::uint8_t actualColor[4]{};
    ReadFirstTexel(correctedColor, GL_RGBA, GL_UNSIGNED_BYTE, actualColor);
    EXPECT_EQ(actualColor[0], 17);
    EXPECT_EQ(actualColor[1], 91);
    EXPECT_EQ(actualColor[2], 207);
    EXPECT_EQ(actualColor[3], 255);
}

// ColorReplacementSample_4_0: a compiled-effect quad placed BEHIND stock geometry must not
// overwrite it -- both routes must encode depth on the same scale.
TEST(OpenGL4CompiledEffectDrawTest, IsDepthTestedOnTheSameScaleAsStockGeometry)
{
    GraphicsDevice device;
    OpenGL4Renderer* renderer = RendererOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

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

    const auto nearQuad = fullScreenQuad(0.6f);
    auto stockVb = renderer->CreateVertexBuffer(6);
    stockVb->SetVertexDeclaration(stockDeclaration);
    stockVb->SetData(nearQuad.data(), 6, sizeof(Vertex));
    GpuDrawParams stockParams{};
    ASSERT_NO_THROW(renderer->DrawPrimitivesEx(
        *stockVb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, stockParams));

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
    EXPECT_EQ(centre.getRProperty(), 255) << "the stock quad is nearer and must own the pixel";
    EXPECT_EQ(centre.getGProperty(), 0)
        << "a compiled-effect quad drawn BEHIND stock geometry overwrote it";
    EXPECT_EQ(centre.getBProperty(), 0);
}

TEST(OpenGL4CompiledEffectTest, SharedBackendConformanceContract)
{
    // plans/plan_fx.md FX-060/FX-062: the same cross-renderer contract FNA3D's and SDL_GPU's own
    // SharedBackendConformanceContract tests run -- format, reflection, parameter API,
    // techniques/passes, render state, state policy, samplers, texture binding, clone and
    // lifetime -- through the public Effect/GraphicsDevice API, since SupportsCompiledEffects()
    // is true.
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(OpenGL4CompiledEffectTest, PixelShaderOnlyPassAppliesOnDesktopCoreContext)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeSampler = true;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));

    EXPECT_NO_THROW(effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply());
}

TEST(OpenGL4CompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LoopUsesIterationCountNotAddressLimit)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLoopContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LogIgnoresSignAndReturnsFiniteValueForZero)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSignedLogContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9NrmUsesXYZLengthRegardlessOfDestinationMask)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectNrmWriteMaskContract(device);
}

class OpenGL4CompiledEffectCompositeWriteMaskTest :
    public ::testing::TestWithParam<CNA::TestSupport::SyntheticCompositeWriteMaskProbe>
{};

TEST_P(OpenGL4CompiledEffectCompositeWriteMaskTest,
       WritesOnlySelectedCompositeResultComponents)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectCompositeWriteMaskContract(device, GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    D3D9DstAndCrs,
    OpenGL4CompiledEffectCompositeWriteMaskTest,
    ::testing::Values(
        CNA::TestSupport::SyntheticCompositeWriteMaskProbe::VertexDst,
        CNA::TestSupport::SyntheticCompositeWriteMaskProbe::VertexCrs));

TEST(OpenGL4CompiledEffectDrawTest, ShaderModel11ExppUsesLegacyFourPartResult)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyExppContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, ShaderModel11ImplicitVertexInputMap)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel11InputContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, ShaderModel11ExtendedVertexInputMap)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel11ExtendedInputContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9RelativeInputTextureCoordinates)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRelativeInputTextureCoordinateContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9DependentTemporaryTextureCoordinatesDriveLod)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDependentTemporaryTextureCoordinateContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9DependentTemporaryCubeAndVolumeCoordinatesDriveLod)
{
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDependentTemporaryTextureCoordinate3DContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9SubroutinesExecuteAndReturn)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSubroutineContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9DerivativesUseAdjacentLockStepRegisters)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDerivativeContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9RasterInputsUseXnaCoordinatesAndFacing)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRasterInputContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9InstructionPredicationMasksBothShaderStages)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPredicationContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9PredicationGatesTexkill)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPredicatedTexkillContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9ShaderModel14TexcrdDwSelector)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel14TexcrdDwContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9ShaderModel14TexldDz)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel14TexldDzContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9ShaderModel14TexldDwSelector)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel14TexldDwContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9ShaderModel14TextureLoad)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel14TextureLoadContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9ShaderModel14Phase)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectShaderModel14PhaseContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, VertexFloatRedefinitionUsesTheFinalConstant)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeDrawableProgram = true;
    options.immediateConstantDefinitionProbe =
        CNA::TestSupport::SyntheticImmediateConstantDefinitionProbe::VertexFloatDuplicate;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Transform"]->SetValue(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());

    struct Vertex { float x, y, z; };
    const Vertex quad[6] = {
        {-1,  1, 0}, {-1, -1, 0}, { 1, -1, 0},
        {-1,  1, 0}, { 1, -1, 0}, { 1,  1, 0},
    };
    const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    RenderTarget2D target(device, 4, 4);
    device.SetRenderTarget(&target);
    device.Clear(Color::Magenta);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList,
                              static_cast<const void*>(quad), 0, 2, declaration);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color centre = Color::Transparent;
    const Rectangle rectangle(2, 2, 1, 1);
    target.GetData(0, &rectangle, &centre, 0, 1);
    EXPECT_EQ(centre, Color::Lime);
}

TEST(OpenGL4CompiledEffectDrawTest, ProjectedCubeLoadDividesDirectionByW)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    constexpr std::array profiles{
        CNA::TestSupport::SyntheticTexldDestinationModifierProbe::
            Pixel20TexldpPartialPrecision,
        CNA::TestSupport::SyntheticTexldDestinationModifierProbe::
            Pixel30TexldpPartialPrecision,
    };
    for (const auto profile : profiles)
    {
        SCOPED_TRACE(static_cast<int>(profile));
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.samplerKind = CNA::TestSupport::SyntheticSamplerKind::SamplerCube;
        options.texldDestinationModifierProbe = profile;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
        };
        Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
        EXPECT_EQ(CNA::TestSupport::DrawCompiledEffectProjectedCubeSampler(device, effect),
                  Color::Green);
    }
}

TEST(OpenGL4CompiledEffectDrawTest, Projected2DLoadDerivesLodAfterDivisionByW)
{
    namespace Fx = CNA::TestSupport::EffectFormat;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    const std::array probes = {
        CNA::TestSupport::SyntheticTexldDestinationModifierProbe::
            Pixel20TexldpPartialPrecision,
        CNA::TestSupport::SyntheticTexldDestinationModifierProbe::
            Pixel30TexldpPartialPrecision,
    };
    for (const auto probe : probes)
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.texldDestinationModifierProbe = probe;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        };
        Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
        EXPECT_EQ(CNA::TestSupport::DrawCompiledEffectProjected2DLod(device, effect),
                  Color::Blue);
    }
}

TEST(OpenGL4CompiledEffectDrawTest, ShaderModel3PacksDisjointSemanticsIntoOneRegister)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeDrawableProgram = true;
    options.semanticDeclarationProbe =
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::PackedDisjoint;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Transform"]->SetValue(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());

    struct Vertex { float x, y, z; };
    const Vertex quad[6] = {
        {-1,  1, 0}, {-1, -1, 0}, { 1, -1, 0},
        {-1,  1, 0}, { 1, -1, 0}, { 1,  1, 0},
    };
    const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    RenderTarget2D target(device, 4, 4);
    device.SetRenderTarget(&target);
    device.Clear(Color::Magenta);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList,
                              static_cast<const void*>(quad), 0, 2, declaration);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color centre = Color::Transparent;
    const Rectangle probe(2, 2, 1, 1);
    target.GetData(0, &probe, &centre, 0, 1);
    EXPECT_NEAR(centre.getRProperty(), 64, 2);
    EXPECT_NEAR(centre.getGProperty(), 128, 2);
    EXPECT_NEAR(centre.getBProperty(), 191, 2);
    EXPECT_NEAR(centre.getAProperty(), 255, 2);
}

TEST(OpenGL4CompiledEffectDrawTest, ShaderModel3PacksSeparateSemanticOutputsIntoOnePixelInput)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeDrawableProgram = true;
    options.semanticDeclarationProbe =
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Transform"]->SetValue(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());

    struct Vertex { float x, y, z; };
    const Vertex quad[6] = {
        {-1,  1, 0}, {-1, -1, 0}, { 1, -1, 0},
        {-1,  1, 0}, { 1, -1, 0}, { 1,  1, 0},
    };
    const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    RenderTarget2D target(device, 4, 4);
    device.SetRenderTarget(&target);
    device.Clear(Color::Magenta);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList,
                              static_cast<const void*>(quad), 0, 2, declaration);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color centre = Color::Transparent;
    const Rectangle probe(2, 2, 1, 1);
    target.GetData(0, &probe, &centre, 0, 1);
    EXPECT_NEAR(centre.getRProperty(), 64, 2);
    EXPECT_NEAR(centre.getGProperty(), 128, 2);
    EXPECT_NEAR(centre.getBProperty(), 191, 2);
    EXPECT_NEAR(centre.getAProperty(), 255, 2);
}

class OpenGL4CompiledEffectCentroidTest :
    public ::testing::TestWithParam<CNA::TestSupport::SyntheticSemanticDeclarationProbe>
{
};

TEST_P(OpenGL4CompiledEffectCentroidTest, InterpolatesInsideAPartiallyCoveredPixel)
{
    using Probe = CNA::TestSupport::SyntheticSemanticDeclarationProbe;
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeDrawableProgram = true;
    options.semanticDeclarationProbe = GetParam();
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Transform"]->SetValue(
        Microsoft::Xna::Framework::Matrix::getIdentityProperty());

    struct Vertex { float x, y, z, u, v; };
    const Vertex triangle[3] = {
        {-0.2f,  1.0f, 0.0f, 0.0f, 0.0f},
        {-0.2f, -1.0f, 0.0f, 0.0f, 0.0f},
        { 0.9f,  0.0f, 0.0f, 1.0f, 0.0f},
    };
    const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Color,
                          DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList,
                              static_cast<const void*>(triangle), 0, 1, declaration);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color edge = Color::Transparent;
    const Rectangle probe(1, 1, 1, 1);
    target.GetData(0, &probe, &edge, 0, 1);
    if (GetParam() == Probe::PixelCentroidControl ||
        GetParam() == Probe::Pixel20CentroidControl)
    {
        EXPECT_GT(edge.getRProperty(), edge.getGProperty());
        EXPECT_GT(edge.getRProperty(), 32);
    }
    else
    {
        EXPECT_GT(edge.getGProperty(), edge.getRProperty());
        EXPECT_GT(edge.getGProperty(), 32);
    }
}

INSTANTIATE_TEST_SUITE_P(
    D3D9Contract,
    OpenGL4CompiledEffectCentroidTest,
    ::testing::Values(
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::PixelCentroidControl,
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::PixelCentroidExplicit,
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::PixelCentroidImplicitColor,
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::Pixel20CentroidControl,
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::Pixel20CentroidExplicit,
        CNA::TestSupport::SyntheticSemanticDeclarationProbe::Pixel20CentroidImplicitColor));

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacyTextureMatrix)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyTextureMatrixContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacySampledTextureMatrix2)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyTextureMatrix2Contract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacySampledTextureMatrix3)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyTextureMatrix3SampleContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacyTextureMatrix3Specular)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyTextureMatrix3SpecularContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacyPixelDepthOutputs)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyDepthOutputContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacyTextureComponentRemap)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyTextureRemapContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacyDependentTextureOperations)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyDependentTextureContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9LegacyBumpEnvironmentOperations)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectLegacyBumpEnvironmentContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SpriteBatchInheritsStockVertexShaderForPixelOnlyEffect)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
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

TEST(OpenGL4CompiledEffectDrawTest, SpriteBatchLayerDepthReachesInheritedStockVertexShader)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeSampler = true;
    options.pixelShaderSamplesTexture = true;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Tint"]->SetValue(
        Microsoft::Xna::Framework::Vector4::One);

    Texture2D nearTexture(device, 1, 1);
    Texture2D farTexture(device, 1, 1);
    const Color red[1] = {Color::Red};
    const Color green[1] = {Color::Green};
    nearTexture.SetData(red, 1);
    farTexture.SetData(green, 1);

    RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);
    device.SetRenderTarget(&target);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                 Color::Black, 1.0f, 0);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                &DepthStencilState::Default, &RasterizerState::CullNone, &effect);
    batch.Draw(nearTexture, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Microsoft::Xna::Framework::Vector2::Zero, SpriteEffects::None, 0.1f);
    batch.Draw(farTexture, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Microsoft::Xna::Framework::Vector2::Zero, SpriteEffects::None, 0.9f);
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color actual = Color::Transparent;
    const Rectangle centre(4, 4, 1, 1);
    target.GetData(0, &centre, &actual, 0, 1);
    EXPECT_GT(actual.getRProperty(), 100);
    EXPECT_NEAR(actual.getGProperty(), 0, 3);
    EXPECT_NEAR(actual.getBProperty(), 0, 3);
}

TEST(OpenGL4CompiledEffectDrawTest, SpriteBatchBeginMipClampReachesCompiledPixelSampler)
{
    // SOFTWARE-187: this effect samples slot 0 but assigns no sampler-state fields of its own.
    // FNA therefore leaves SpriteBatch.Begin's public slot-0 state authoritative. The sprite is
    // magnified, so only MaxMipLevel=2 can select the blue 1x1 mip instead of the red base level.
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeSampler = true;
    options.pixelShaderSamplesTexture = true;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Tint"]->SetValue(
        Microsoft::Xna::Framework::Vector4::One);

    Texture2D sprite(device, 4, 4, true, SurfaceFormat::Color);
    const Color red[16] = {
        Color::Red, Color::Red, Color::Red, Color::Red,
        Color::Red, Color::Red, Color::Red, Color::Red,
        Color::Red, Color::Red, Color::Red, Color::Red,
        Color::Red, Color::Red, Color::Red, Color::Red,
    };
    const Color green[4] = {Color::Green, Color::Green, Color::Green, Color::Green};
    const Color blue[1] = {Color::Blue};
    sprite.SetData(0, nullptr, red, 0, 16);
    sprite.SetData(1, nullptr, green, 0, 4);
    sprite.SetData(2, nullptr, blue, 0, 1);

    SamplerState sampler = SamplerState::PointClamp;
    sampler.setMaxMipLevelProperty(2);
    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr, &effect);
    batch.Draw(sprite, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color actual(0, 0, 0, 0);
    const Rectangle centre(4, 4, 1, 1);
    target.GetData(0, &centre, &actual, 0, 1);
    EXPECT_NEAR(actual.getRProperty(), 0, 3);
    EXPECT_NEAR(actual.getGProperty(), 0, 3);
    EXPECT_NEAR(actual.getBProperty(), 255, 3);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9SamplerSourceSwizzlesSampleResult)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSamplerResultSwizzleContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9TexlddUsesItsSecondSourceAsTheSampler)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    namespace Fx = CNA::TestSupport::EffectFormat;
    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeDrawableProgram = true;
    options.includeSampler = true;
    options.pixelShaderUsesTextureGradients = true;
    options.samplerStates = {
        {Fx::SampMagFilter, Fx::FilterPoint},
        {Fx::SampMinFilter, Fx::FilterPoint},
        {Fx::SampMipFilter, Fx::FilterPoint},
        {Fx::SampAddressU, Fx::AddressClamp},
        {Fx::SampAddressV, Fx::AddressClamp},
    };
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    EXPECT_EQ(CNA::TestSupport::DrawCompiledEffectSamplerResultSwizzle(device, effect),
              Color(32, 64, 128, 255));
}

TEST(OpenGL4CompiledEffectDrawTest, SharedVertexSamplerContract)
{
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectVertexSamplerContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, D3D9VertexSamplerSourceSwizzlesSampleResult)
{
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectVertexSamplerResultSwizzleContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedVertexSamplerDimensionsContract)
{
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectVertexSamplerDimensionsContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, DeviceTextureAndSamplerOverridesRemainAuthoritativeAfterPassApply)
{
    // SOFTWARE-186: FNA's Effect.INTERNAL_updateSamplers publishes a pass's assignments into the
    // GraphicsDevice collections. A later application assignment to those same public slots is
    // therefore what ApplySamplers verifies at the draw. The three possible centre colours below
    // identify the failure precisely: blue means the stale effect-private texture won; green
    // means the public texture won but the stale Clamp sampler did; only red means both public
    // collections remained authoritative.
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
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
    parameters["Tint"]->SetValue(Microsoft::Xna::Framework::Vector4::One);

    Texture2D passTexture(device, 1, 1);
    const Color blue[1] = {Color::Blue};
    passTexture.SetData(blue, 1);
    parameters["FxTexture"]->SetValue(&passTexture);

    Texture2D deviceTexture(device, 2, 1);
    const Color redGreen[2] = {Color::Red, Color::Green};
    deviceTexture.SetData(redGreen, 2);

    CNA::TestSupport::SamplingQuadVertex quad[6];
    CNA::TestSupport::FillSamplingQuad(quad, 1.25f, 0.5f);
    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();

    device.getTexturesProperty()(0, &deviceTexture);
    device.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, static_cast<const void*>(quad), 0, 2,
        CNA::TestSupport::SamplingQuadDeclaration());
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color actual(0, 0, 0, 0);
    const Rectangle centre(4, 4, 1, 1);
    target.GetData(0, &centre, &actual, 0, 1);
    EXPECT_NEAR(actual.getRProperty(), 255, 3);
    EXPECT_NEAR(actual.getGProperty(), 0, 3);
    EXPECT_NEAR(actual.getBProperty(), 0, 3);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedPassSelectionContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, MultipleRenderTargetSamplersKeepTheirOwnTextureUnits)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
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
    effect.getTechniquesProperty()[0]->getPassesProperty()[0]->Apply();
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

TEST(OpenGL4CompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SpriteBatchCompiledPassKeepsTexelCenters)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    namespace Fx = CNA::TestSupport::EffectFormat;
    constexpr int size = 4;
    Texture2D source(device, size, size);
    std::vector<Color> sourcePixels;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            sourcePixels.emplace_back(20 + x * 60, 30 + y * 55, 40, 255);
    source.SetData(sourcePixels.data(), static_cast<int>(sourcePixels.size()));

    Effect effect(device, CNA::TestSupport::BuildSyntheticSamplingEffect({
        {Fx::SampMagFilter, Fx::FilterLinear},
        {Fx::SampMinFilter, Fx::FilterLinear},
        {Fx::SampMipFilter, Fx::FilterLinear},
        {Fx::SampAddressU, Fx::AddressClamp},
        {Fx::SampAddressV, Fx::AddressClamp},
    }));
    auto& parameters = effect.getParametersProperty();
    parameters["Tint"]->SetValue(Microsoft::Xna::Framework::Vector4::One);
    parameters["FxTexture"]->SetValue(&source);
    parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
        0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, -1.0f, 1.0f));

    RenderTarget2D result(device, size, size);
    device.SetRenderTarget(&result);
    device.Clear(Color::Black);
    SpriteBatch batch(device);
    SamplerState linearClamp = SamplerState::LinearClamp;
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linearClamp,
                nullptr, nullptr, &effect);
    batch.Draw(source, Rectangle(0, 0, size, size), Color::White);
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::vector<Color> actual(sourcePixels.size());
    result.GetData(actual.data(), static_cast<int>(actual.size()));
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        SCOPED_TRACE("pixel " + std::to_string(i % size) + "," + std::to_string(i / size));
        EXPECT_NEAR(actual[i].getRProperty(), sourcePixels[i].getRProperty(), 3);
        EXPECT_NEAR(actual[i].getGProperty(), sourcePixels[i].getGProperty(), 3);
        EXPECT_NEAR(actual[i].getBProperty(), sourcePixels[i].getBProperty(), 3);
    }
}

TEST(OpenGL4CompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    // SOFTWARE-179: Texture3D is a HiDef-only XNA resource. This compiled-effect family predated
    // profile-ceiling enforcement and accidentally kept constructing the default Reach device.
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedManyDrawsContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(OpenGL4CompiledEffectDrawTest, SharedTruncationContract)
{
    GraphicsDevice device;
    if (!RunsCompiledEffectsOnOpenGL4(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}

#endif  // CNA_OPENGL4_COMPILED_EFFECTS
