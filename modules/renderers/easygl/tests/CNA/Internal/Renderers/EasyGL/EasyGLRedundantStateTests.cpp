// SPDX-License-Identifier: MS-PL

// GLB-41: GraphicsDevice re-applies all sixteen pixel samplers and
// the rasterizer state before every draw. EasyGL records what it last wrote, so an unchanged state
// costs no GL call -- under WebGL every call crosses into JavaScript, and the anisotropy limit and
// viewport reads were synchronous round trips to the GPU process. These tests pin both halves of
// that contract against a real context: the GL objects always hold exactly what the latest
// application asked for, and an unchanged application really is not re-sent.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <metagl/metagl.hpp>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

namespace
{
    using namespace CNA::Testing::Renderers;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework::Graphics;  // NOLINT(google-build-using-namespace)

    constexpr GLint kNearestMipmapNearest =
        static_cast<GLint>(metagl::TextureMinFilter::NearestMipmapNearest);
    constexpr GLint kLinearMipmapLinear =
        static_cast<GLint>(metagl::TextureMinFilter::LinearMipmapLinear);
    constexpr GLint kMagNearest = static_cast<GLint>(metagl::TextureMagFilter::Nearest);
    constexpr GLint kMagLinear = static_cast<GLint>(metagl::TextureMagFilter::Linear);
    constexpr GLint kRepeat = static_cast<GLint>(metagl::TextureWrapMode::Repeat);
    constexpr GLint kClampToEdge = static_cast<GLint>(metagl::TextureWrapMode::ClampToEdge);

    struct ExpectedSampler
    {
        GLint minFilter;
        GLint magFilter;
        GLint wrap;
        GLfloat minLod;
    };

    class EasyGLRedundantStateTest : public ::testing::Test
    {
    protected:
        std::unique_ptr<GraphicsDevice> device;
        std::unique_ptr<Texture2D> texture;
        std::unique_ptr<BasicEffect> effect;

        void SetUp() override
        {
            if (!CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
                GTEST_SKIP() << "the selected renderer has no EasyGL sampler objects";

            device = std::make_unique<GraphicsDevice>(
                GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                PresentationParameters::CreateWindowedEXT(800, 480));
            // CnaTests links its own static copy of meta-gl, apart from the renderer's inside
            // libcna.so (VKPAR-0017), and meta-gl terminates on a call through a table nobody
            // initialised -- which is how three of these tests aborted on Linux, as FX-144 found in
            // EasyGLCompiledEffectTests (CNA Apple M4 AM4-284). Load this binary's
            // copy from the renderer's own context before reading GL state directly.
            auto* renderer = dynamic_cast<CNA::Internal::Renderers::EasyGL::EasyGLRenderer*>(
                &device->GetRenderer());
            ASSERT_NE(renderer, nullptr);
            if (!::metagl::IsInitialized())
                ASSERT_TRUE(::metagl::Initialize(renderer->GetProcAddressLoaderEXT()));
            texture = std::make_unique<Texture2D>(*device, 1, 1);
            const Color white = Color::White;
            texture->SetData(&white, 1);
            effect = std::make_unique<BasicEffect>(*device);
            effect->setTextureEnabledProperty(true);
            effect->setTextureProperty(texture.get());
        }

        void DrawWith(const SamplerState& state)
        {
            const std::array<VertexPositionTexture, 3> triangle{{
                VertexPositionTexture(Vector3(-0.5f, -0.5f, 0.0f), Vector2(0.0f, 1.0f)),
                VertexPositionTexture(Vector3( 0.5f, -0.5f, 0.0f), Vector2(1.0f, 1.0f)),
                VertexPositionTexture(Vector3( 0.0f,  0.5f, 0.0f), Vector2(0.5f, 0.0f)),
            }};
            device->getSamplerStatesProperty()[0] = state;
            effect->Apply();
            device->DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1,
                                       VertexPositionTexture::getVertexDeclarationStatic());
        }

        [[nodiscard]] static GLuint SamplerOnUnitZero()
        {
            metagl::glActiveTexture(metagl::TextureUnit::Texture0);
            GLint name = 0;
            metagl::glGetIntegerv(metagl::GetParameter::SamplerBinding, &name);
            return static_cast<GLuint>(name);
        }

        [[nodiscard]] static GLint SamplerInt(GLuint sampler, metagl::SamplerParameter pname)
        {
            GLint value = 0;
            metagl::glGetSamplerParameteriv(metagl::SamplerId{sampler}, pname, &value);
            return value;
        }

        [[nodiscard]] static GLfloat SamplerFloat(GLuint sampler, metagl::SamplerParameter pname)
        {
            GLfloat value = 0.0f;
            metagl::glGetSamplerParameterfv(metagl::SamplerId{sampler}, pname, &value);
            return value;
        }

        static void ExpectSampler(GLuint sampler, const ExpectedSampler& expected, const char* step)
        {
            ASSERT_NE(sampler, 0u) << step << ": no sampler object is bound to unit 0";
            EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::MinFilter), expected.minFilter) << step;
            EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::MagFilter), expected.magFilter) << step;
            EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::WrapS), expected.wrap) << step;
            EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::WrapT), expected.wrap) << step;
            EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::WrapR), expected.wrap) << step;
            EXPECT_EQ(SamplerFloat(sampler, metagl::SamplerParameter::MinLod), expected.minLod) << step;
        }
    };
}

TEST_F(EasyGLRedundantStateTest, SamplerObjectHoldsEveryApplicationAcrossAlternatingStates)
{
    SamplerState mipClamped = SamplerState::LinearWrap;
    mipClamped.setMaxMipLevelProperty(2);

    const ExpectedSampler anisotropicWrap{kLinearMipmapLinear, kMagLinear, kRepeat, 0.0f};
    const ExpectedSampler pointClamp{kNearestMipmapNearest, kMagNearest, kClampToEdge, 0.0f};
    const ExpectedSampler linearWrapFromMip2{kLinearMipmapLinear, kMagLinear, kRepeat, 2.0f};

    DrawWith(SamplerState::AnisotropicWrap);
    ExpectSampler(SamplerOnUnitZero(), anisotropicWrap, "first AnisotropicWrap");
    DrawWith(SamplerState::PointClamp);
    ExpectSampler(SamplerOnUnitZero(), pointClamp, "PointClamp");
    DrawWith(SamplerState::AnisotropicWrap);
    ExpectSampler(SamplerOnUnitZero(), anisotropicWrap, "AnisotropicWrap again");
    DrawWith(mipClamped);
    ExpectSampler(SamplerOnUnitZero(), linearWrapFromMip2, "LinearWrap with MaxMipLevel 2");
    DrawWith(mipClamped);
    ExpectSampler(SamplerOnUnitZero(), linearWrapFromMip2, "the same state repeated");
    DrawWith(SamplerState::PointClamp);
    ExpectSampler(SamplerOnUnitZero(), pointClamp, "PointClamp after a mip clamp");
}

TEST_F(EasyGLRedundantStateTest, AnisotropyIsClampedToTheContextLimitAndResetByOtherFilters)
{
    if (!metagl::HasExtension("GL_EXT_texture_filter_anisotropic"))
        GTEST_SKIP() << "this context has no anisotropic filtering";

    GLfloat limit = 1.0f;
    metagl::glGetFloatv(metagl::GetParameter::MaxTextureMaxAnisotropy, &limit);
    SamplerState overLimit = SamplerState::AnisotropicWrap;
    overLimit.setMaxAnisotropyProperty(1024);

    DrawWith(overLimit);
    EXPECT_EQ(SamplerFloat(SamplerOnUnitZero(), metagl::SamplerParameter::MaxAnisotropy), limit);
    DrawWith(SamplerState::PointClamp);
    EXPECT_EQ(SamplerFloat(SamplerOnUnitZero(), metagl::SamplerParameter::MaxAnisotropy), 1.0f);
    DrawWith(SamplerState::AnisotropicWrap);
    EXPECT_EQ(SamplerFloat(SamplerOnUnitZero(), metagl::SamplerParameter::MaxAnisotropy),
              std::min(4.0f, limit));
}

TEST_F(EasyGLRedundantStateTest, AnUnchangedSamplerStateIsNotWrittenAgain)
{
    DrawWith(SamplerState::PointClamp);
    const GLuint sampler = SamplerOnUnitZero();
    ASSERT_NE(sampler, 0u);

    // Change the object behind the renderer's back. The renderer is the only writer in
    // production, so the next identical application has nothing to write -- and must not.
    metagl::glSamplerParameteri(metagl::SamplerId{sampler}, metagl::SamplerParameter::MinFilter,
                                static_cast<GLint>(metagl::TextureMinFilter::Linear));
    DrawWith(SamplerState::PointClamp);
    EXPECT_EQ(SamplerOnUnitZero(), sampler);
    EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::MinFilter),
              static_cast<GLint>(metagl::TextureMinFilter::Linear))
        << "an identical application re-sent a parameter it had already written";

    // A state that really differs is written in full.
    DrawWith(SamplerState::LinearClamp);
    EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::MinFilter), kLinearMipmapLinear);
    EXPECT_EQ(SamplerInt(sampler, metagl::SamplerParameter::MagFilter), kMagLinear);
}

TEST_F(EasyGLRedundantStateTest, ContextLossForgetsWhatWasWritten)
{
    DrawWith(SamplerState::PointClamp);
    ExpectSampler(SamplerOnUnitZero(), {kNearestMipmapNearest, kMagNearest, kClampToEdge, 0.0f},
                  "before the loss");

    device->GetRenderer().DebugSimulateContextLoss();

    // The recreated sampler object starts at GL's defaults. Had the record survived, the same
    // state would look already written and the fresh object would keep those defaults.
    DrawWith(SamplerState::PointClamp);
    ExpectSampler(SamplerOnUnitZero(), {kNearestMipmapNearest, kMagNearest, kClampToEdge, 0.0f},
                  "after the context was recreated");
}

TEST_F(EasyGLRedundantStateTest, PolygonModeFollowsEveryFillModeChange)
{
    if (!CNA_RENDERER_IS(OpenGL33))
        GTEST_SKIP() << "GL_POLYGON_MODE can be read back only from a desktop context";
    if (!device->SupportsCapability(CNA::GraphicsCapability::WireFrame))
        GTEST_SKIP() << "this context has no native polygon mode";

    constexpr auto kPolygonMode = static_cast<metagl::GetParameter>(0x0B40);
    constexpr GLint kLine = 0x1B01;
    constexpr GLint kFill = 0x1B02;
    const auto polygonMode = [&] {
        std::array<GLint, 2> mode{};
        metagl::glGetIntegerv(kPolygonMode, mode.data());
        return mode[0];
    };
    RasterizerState wire;
    wire.setCullModeProperty(CullMode::None);
    wire.setFillModeProperty(FillMode::WireFrame);
    const auto drawWith = [&](const RasterizerState& rasterizer) {
        device->setRasterizerStateProperty(rasterizer);
        DrawWith(SamplerState::LinearClamp);
    };

    drawWith(wire);
    EXPECT_EQ(polygonMode(), kLine);
    drawWith(RasterizerState::CullNone);
    EXPECT_EQ(polygonMode(), kFill);
    drawWith(RasterizerState::CullNone);
    EXPECT_EQ(polygonMode(), kFill);
    drawWith(wire);
    EXPECT_EQ(polygonMode(), kLine);

    // The recreated context starts filled; the wireframe selection must be sent to it again.
    device->GetRenderer().DebugSimulateContextLoss();
    drawWith(wire);
    EXPECT_EQ(polygonMode(), kLine);
    drawWith(RasterizerState::CullNone);
    EXPECT_EQ(polygonMode(), kFill);
}

// L46 (GLB-43): a stock program keeps the last value sent to each uniform, so an identical draw
// sends none, a changed value is still sent, and a context loss forgets every recorded value.
namespace
{
    Color CentreAfterDrawing(GraphicsDevice& device, BasicEffect& effect, RenderTarget2D& target,
                             const Color& diffuse)
    {
        const std::array<VertexPositionTexture, 3> triangle{{
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3( 3.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f,  3.0f, 0.0f), Vector2(0.0f, 0.0f)),
        }};
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.setDiffuseColorProperty(diffuse.ToVector3());
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1,
                                  VertexPositionTexture::getVertexDeclarationStatic());
        device.SetRenderTarget(nullptr);
        std::array<Color, 16> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[5];
    }
}

TEST_F(EasyGLRedundantStateTest, AnIdenticalDrawSendsNoStockUniform)
{
    auto* renderer = dynamic_cast<CNA::Internal::Renderers::EasyGL::EasyGLRenderer*>(
        &device->GetRenderer());
    ASSERT_NE(renderer, nullptr);
    DrawWith(SamplerState::LinearClamp);
    const std::uint64_t afterFirst = renderer->GetStockUniformUploadCountEXT();
    EXPECT_GT(afterFirst, 0u);
    DrawWith(SamplerState::LinearClamp);
    DrawWith(SamplerState::LinearClamp);
    EXPECT_EQ(renderer->GetStockUniformUploadCountEXT(), afterFirst)
        << "every uniform of an unchanged draw is already in the program";
}

TEST_F(EasyGLRedundantStateTest, AChangedStockUniformIsStillSent)
{
    RenderTarget2D target(*device, 4, 4);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::Red), Color::Red);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::Lime), Color::Lime);
    // Back to a value the record held two draws ago: the record follows the latest one.
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::Red), Color::Red);
}

TEST_F(EasyGLRedundantStateTest, AContextLossForgetsTheRecordedUniforms)
{
    RenderTarget2D target(*device, 4, 4);
    ASSERT_EQ(CentreAfterDrawing(*device, *effect, target, Color::Red), Color::Red);
    device->GetRenderer().DebugSimulateContextLoss();
    // The relinked program starts with every uniform at zero; had the record survived, the
    // diffuse colour would look already sent and the triangle would come out black.
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::Red), Color::Red);
}


namespace
{
    using CNA::Internal::Renderers::EasyGL::EasyGLRenderer;
    using CNA::Internal::Renderers::EasyGL::EasyGLTextureBindingState;
    using CNA::Internal::Renderers::EasyGL::EasyGLTrackedTexture;

    const char* const kTextureBindingVertex = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); }
)";
    const char* const kTextureBindingFragment = R"(#version 300 es
precision highp float;
uniform sampler2D uSource;
out vec4 FragColor;
void main() { FragColor = texture(uSource, vec2(0.5)); }
)";
    const char* const kCubeBindingFragment = R"(#version 300 es
precision highp float;
uniform samplerCube uSource;
out vec4 FragColor;
void main() { FragColor = texture(uSource, vec3(1.0, 0.0, 0.0)); }
)";
    const char* const kVolumeBindingFragment = R"(#version 300 es
precision highp float;
uniform highp sampler3D uSource;
out vec4 FragColor;
void main() { FragColor = texture(uSource, vec3(0.5)); }
)";

    Color BindingTargetCentre(RenderTarget2D& target)
    {
        Color pixel;
        const Rectangle centre(2, 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    }

    Color BindingSprite(GraphicsDevice& device, SpriteBatch& batch, RenderTarget2D& target,
                        Texture2D& source, ShaderEffect* custom = nullptr)
    {
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                    nullptr, nullptr, &RasterizerState::CullNone, custom);
        batch.Draw(source, Rectangle(0, 0, 4, 4), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);
        return BindingTargetCentre(target);
    }
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsIdenticalDrawsSendNoAdditionalBind)
{
    auto& renderer = dynamic_cast<EasyGLRenderer&>(device->GetRenderer());
    renderer.InvalidateTextureBindingsEXT();
    const auto before = renderer.GetTextureBindCountEXT();
    DrawWith(SamplerState::PointClamp);
    const auto first = renderer.GetTextureBindCountEXT();
    ASSERT_GT(first, before);
    for (int i = 0; i < 5; ++i) DrawWith(SamplerState::PointClamp);
    EXPECT_EQ(renderer.GetTextureBindCountEXT(), first);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsChangedTexturesChangePixels)
{
    RenderTarget2D target(*device, 4, 4);
    Texture2D green(*device, 1, 1);
    const Color red = Color::Red, lime = Color::Lime;
    texture->SetData(&red, 1);
    green.SetData(&lime, 1);
    effect->setTextureProperty(texture.get());
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    effect->setTextureProperty(&green);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), lime);
    effect->setTextureProperty(texture.get());
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsStockSpriteAndCustomEffectsInterleave)
{
    RenderTarget2D target(*device, 4, 4);
    SpriteBatch batch(*device);
    Texture2D blue(*device, 1, 1);
    const Color red = Color::Red, bluePixel = Color::Blue;
    texture->SetData(&red, 1);
    blue.SetData(&bluePixel, 1);
    ShaderEffect custom(*device, kTextureBindingVertex, kTextureBindingFragment);
    ASSERT_TRUE(custom.IsEffectValid());
    custom.SetUniformInt("uSource", 1);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    EXPECT_EQ(BindingSprite(*device, batch, target, blue), bluePixel);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    custom.SetTexture(1, blue);
    EXPECT_EQ(BindingSprite(*device, batch, target, *texture, &custom), bluePixel);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    // This custom binding mutates the same slot the next stock draw needs.
    custom.SetTexture(0, blue);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsUploadAndReadbackCannotLeaveAStaleBinding)
{
    RenderTarget2D target(*device, 4, 4);
    Texture2D other(*device, 1, 1);
    const Color red = Color::Red, lime = Color::Lime, blue = Color::Blue;
    texture->SetData(&red, 1);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    // SetData on another object overwrites unit zero through easy-gl's hidden image bind.
    other.SetData(&lime, 1);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    Color read;
    other.GetData(&read, 1);
    EXPECT_EQ(read, lime);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    ASSERT_EQ(device->getTexturesProperty()[0], texture.get());
    device->getTexturesProperty()(0, nullptr);
    // Clearing the XNA slot permits SetData but leaves the recorded native binding in place.
    const auto& native = dynamic_cast<const CNA::Internal::Renderers::EasyGL::EasyGLTextureRenderer&>(
        texture->GetRenderer());
    metagl::glActiveTexture(metagl::TextureUnit::Texture0);
    GLint bound = 0;
    metagl::glGetIntegerv(metagl::GetParameter::TextureBinding2D, &bound);
    ASSERT_EQ(static_cast<unsigned int>(bound), native.texture.native_handle());
    texture->SetData(&blue, 1);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), blue);
    // A real GPU GetData, after rendering and mip generation on a different 2D texture.
    RenderTarget2D mipTarget(*device, 4, 4, true, SurfaceFormat::Color, DepthFormat::None);
    device->SetRenderTarget(&mipTarget);
    device->Clear(lime);
    device->SetRenderTarget(nullptr);
    EXPECT_EQ(BindingTargetCentre(mipTarget), lime);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), blue);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsCubeVolumeAndUnitsStayIndependent)
{
    RenderTarget2D target(*device, 4, 4);
    SpriteBatch batch(*device);
    TextureCube cube(*device, 1, false, SurfaceFormat::Color);
    Texture3D volume(*device, 1, 1, 1, false, SurfaceFormat::Color);
    const Color lime = Color::Lime, blue = Color::Blue, red = Color::Red;
    for (int face = 0; face < 6; ++face)
        cube.SetData(static_cast<CubeMapFace>(face), &lime, 1);
    volume.SetData(&blue, 1);
    texture->SetData(&red, 1);
    ShaderEffect cubeEffect(*device, kTextureBindingVertex, kCubeBindingFragment);
    ShaderEffect volumeEffect(*device, kTextureBindingVertex, kVolumeBindingFragment);
    ASSERT_TRUE(cubeEffect.IsEffectValid());
    ASSERT_TRUE(volumeEffect.IsEffectValid());
    // SpriteBatch still binds a 2D source on unit 0; cube/3D on that same unit must coexist.
    cubeEffect.SetUniformInt("uSource", 0);
    volumeEffect.SetUniformInt("uSource", 0);
    cubeEffect.SetTexture(0, cube);
    EXPECT_EQ(BindingSprite(*device, batch, target, *texture, &cubeEffect), lime);
    volumeEffect.SetTexture(0, volume);
    EXPECT_EQ(BindingSprite(*device, batch, target, *texture, &volumeEffect), blue);
    // Re-selecting the cube is a cached bind, although a 3D texture now occupies the same unit.
    cubeEffect.SetTexture(0, cube);
    EXPECT_EQ(BindingSprite(*device, batch, target, *texture, &cubeEffect), lime);
    Color read;
    cube.GetData(CubeMapFace::PositiveX, &read, 1);
    EXPECT_EQ(read, lime);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    volume.GetData(&read, 1);
    EXPECT_EQ(read, blue);
    volumeEffect.SetUniformInt("uSource", 1);
    volumeEffect.SetTexture(1, volume);
    EXPECT_EQ(BindingSprite(*device, batch, target, *texture, &volumeEffect), blue);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsZeroIsKnownSeparatelyForEachTargetAndUnit)
{
    auto state = std::make_shared<EasyGLTextureBindingState>();
    EasyGLTrackedTexture twoD(state), cube(state), volume(state);
    twoD.create(); cube.create(); volume.create();
    twoD.bind(metagl::TextureTarget::Texture2D);
    cube.bind(metagl::TextureTarget::TextureCubeMap);
    volume.bind(metagl::TextureTarget::Texture3D);
    state->Bind(metagl::TextureUnit::Texture0, metagl::TextureTarget::Texture2D, 0);
    const auto afterZero = state->BindCount();
    state->Bind(metagl::TextureUnit::Texture0, metagl::TextureTarget::Texture2D, 0);
    EXPECT_EQ(state->BindCount(), afterZero);
    GLint name = -1;
    metagl::glGetIntegerv(metagl::GetParameter::TextureBinding2D, &name);
    EXPECT_EQ(name, 0);
    metagl::glGetIntegerv(metagl::GetParameter::TextureBindingCubeMap, &name);
    EXPECT_EQ(static_cast<unsigned int>(name), cube.native_handle());
    metagl::glGetIntegerv(metagl::GetParameter::TextureBinding3D, &name);
    EXPECT_EQ(static_cast<unsigned int>(name), volume.native_handle());
    state->Bind(metagl::TextureUnit::Texture1, metagl::TextureTarget::Texture2D, 0);
    EXPECT_EQ(state->BindCount(), afterZero + 1);
    state->Bind(metagl::TextureUnit::Texture0, metagl::TextureTarget::TextureCubeMap, 0);
    state->Bind(metagl::TextureUnit::Texture0, metagl::TextureTarget::Texture3D, 0);
    EXPECT_EQ(state->BindCount(), afterZero + 3);
    metagl::glGetIntegerv(metagl::GetParameter::TextureBindingCubeMap, &name);
    EXPECT_EQ(name, 0);
    metagl::glGetIntegerv(metagl::GetParameter::TextureBinding3D, &name);
    EXPECT_EQ(name, 0);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsDeletedTextureReplacementDrawsNewPixels)
{
    RenderTarget2D target(*device, 4, 4);
    const Color red = Color::Red, lime = Color::Lime;
    texture->SetData(&red, 1);
    ASSERT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    effect->setTextureProperty(nullptr);
    texture.reset();
    // Exercise repeated retirement/recreation without assuming the driver's name allocation policy.
    for (int i = 0; i < 8; ++i)
    {
        texture = std::make_unique<Texture2D>(*device, 1, 1);
        const Color expected = (i % 2 == 0) ? lime : red;
        texture->SetData(&expected, 1);
        effect->setTextureProperty(texture.get());
        EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), expected);
        effect->setTextureProperty(nullptr);
        texture.reset();
    }
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsAbandonedNameForgetsEveryUnitBeforeReuse)
{
    auto state = std::make_shared<EasyGLTextureBindingState>();
    EasyGLTrackedTexture native(state);
    native.create();
    native.bind(metagl::TextureTarget::Texture2D);
    native.active_bind(metagl::TextureUnit::Texture1, metagl::TextureTarget::Texture2D);
    const auto name = native.native_handle();
    native.reset_handle_no_gl();
    // Keep the GL name live to make numeric-name reuse deterministic. The abandoned handle's
    // proofs must already be gone, independent of whether glGenTextures recycles deleted names.
    metagl::glBindTexture(metagl::TextureTarget::Texture2D, metagl::TextureId{});
    const auto before = state->BindCount();
    state->Bind(metagl::TextureUnit::Texture1, metagl::TextureTarget::Texture2D, name);
    state->Bind(metagl::TextureUnit::Texture0, metagl::TextureTarget::Texture2D, name);
    EXPECT_EQ(state->BindCount(), before + 2);
    GLint bound = 0;
    metagl::glGetIntegerv(metagl::GetParameter::TextureBinding2D, &bound);
    EXPECT_EQ(static_cast<unsigned int>(bound), name);
    state->Forget(name);
    const metagl::TextureId id{name};
    metagl::glDeleteTextures(1, &id);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsExternalMutationRequiresInvalidation)
{
    RenderTarget2D target(*device, 4, 4);
    Texture2D other(*device, 1, 1);
    const Color red = Color::Red, lime = Color::Lime;
    texture->SetData(&red, 1);
    other.SetData(&lime, 1);
    ASSERT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    auto& native = dynamic_cast<CNA::Internal::Renderers::EasyGL::EasyGLTextureRenderer&>(
        other.GetRenderer());
    // Model external GL using the documented native-loader escape hatch.
    metagl::glActiveTexture(metagl::TextureUnit::Texture0);
    metagl::glBindTexture(metagl::TextureTarget::Texture2D,
                         metagl::TextureId{native.texture.native_handle()});
    auto& renderer = dynamic_cast<EasyGLRenderer&>(device->GetRenderer());
    renderer.InvalidateTextureBindingsEXT();
    const auto before = renderer.GetTextureBindCountEXT();
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    EXPECT_GT(renderer.GetTextureBindCountEXT(), before);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsRecoveryRestoresPixelsAndForgetsBindings)
{
    RenderTarget2D target(*device, 4, 4);
    Texture2D other(*device, 1, 1);
    const Color red = Color::Red, lime = Color::Lime;
    texture->SetData(&red, 1);
    other.SetData(&lime, 1);
    ASSERT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    device->GetRenderer().DebugSimulateContextLoss();
    auto& renderer = dynamic_cast<EasyGLRenderer&>(device->GetRenderer());
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    const auto afterRecoveryDraw = renderer.GetTextureBindCountEXT();
    DrawWith(SamplerState::PointClamp);
    EXPECT_EQ(renderer.GetTextureBindCountEXT(), afterRecoveryDraw);
    effect->setTextureProperty(&other);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), lime);
    effect->setTextureProperty(texture.get());
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsCompiledEffectsInterleaveWithStockAndUploads)
{
    if (!device->GetRenderer().SupportsCompiledEffects())
        GTEST_SKIP() << "CNA_EASYGL_COMPILED_EFFECTS is OFF in this build";
    using namespace CNA::Internal::Renderers;
    const auto path = CNA::TestSupport::CompiledEffectDirectory() / "CnaConformanceEffect.fxb";
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>()};
    auto& renderer = dynamic_cast<EasyGLRenderer&>(device->GetRenderer());
    auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
    ASSERT_NE(runtime, nullptr);
    RenderTarget2D target(*device, 4, 4);
    Texture2D other(*device, 1, 1);
    const Color red = Color::Red, lime = Color::Lime;
    texture->SetData(&red, 1);
    other.SetData(&lime, 1);
    runtime->SetParameterTexture(5, &other);
    const std::array<VertexPositionTexture, 3> triangle{{
        VertexPositionTexture(Vector3(-1, -1, 0), Vector2(0, 1)),
        VertexPositionTexture(Vector3(3, -1, 0), Vector2(1, 1)),
        VertexPositionTexture(Vector3(-1, 3, 0), Vector2(0, 0)),
    }};
    auto vb = renderer.CreateVertexBuffer(3);
    vb->SetVertexDeclaration(VertexPositionTexture::getVertexDeclarationStatic());
    vb->SetData(triangle.data(), 3, sizeof(VertexPositionTexture));
    const auto drawCompiled = [&]()
    {
        device->SetRenderTarget(&target);
        device->Clear(Color::Magenta);
        device->setBlendStateProperty(BlendState::Opaque);
        device->setRasterizerStateProperty(RasterizerState::CullNone);
        CompiledEffectDeviceState states;
        CompiledEffectPassStateChanges changes;
        runtime->SetTechnique(0);
        runtime->ApplyPass(0, states, changes);
        GpuDrawParams params{};
        params.compiledEffectRuntime = runtime.get();
        renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                  Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
        device->SetRenderTarget(nullptr);
        const Color pixel = BindingTargetCentre(target);
        // Fixture defaults multiply green by Tint.g * Gain = .2 * .25.
        EXPECT_NEAR(pixel.getRProperty(), 0, 2);
        EXPECT_NEAR(pixel.getGProperty(), 13, 3);
        EXPECT_NEAR(pixel.getBProperty(), 0, 2);
    };
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    drawCompiled();
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    other.SetData(&lime, 1);
    drawCompiled();
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
}


TEST_F(EasyGLRedundantStateTest, TextureBindingsNullStockFallbackDoesNotReusePreviousTexture)
{
    RenderTarget2D target(*device, 4, 4);
    const Color red = Color::Red;
    texture->SetData(&red, 1);
    ASSERT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
    effect->setTextureProperty(nullptr);
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), Color::Black);
    effect->setTextureProperty(texture.get());
    EXPECT_EQ(CentreAfterDrawing(*device, *effect, target, Color::White), red);
}

TEST_F(EasyGLRedundantStateTest, TextureBindingsUntrackedUnitsUseTheUncachedFallback)
{
    auto state = std::make_shared<EasyGLTextureBindingState>();
    EasyGLTrackedTexture native(state);
    native.create();
    // ES3 and desktop GL3 both expose at least 32 combined units; CNA uses 0..19.
    const auto unit = static_cast<metagl::TextureUnit>(
        static_cast<unsigned int>(metagl::TextureUnit::Texture0) + 20);
    native.active_bind(unit, metagl::TextureTarget::Texture2D);
    const auto first = state->BindCount();
    native.active_bind(unit, metagl::TextureTarget::Texture2D);
    EXPECT_EQ(state->BindCount(), first + 1);
    GLint name = 0;
    metagl::glGetIntegerv(metagl::GetParameter::TextureBinding2D, &name);
    EXPECT_EQ(static_cast<unsigned int>(name), native.native_handle());
    metagl::glActiveTexture(metagl::TextureUnit::Texture0);
}

namespace
{
    using CNA::Internal::Renderers::GpuDrawParams;
    using CNA::Internal::Renderers::IVertexBufferRenderer;
    using CNA::Internal::Renderers::EasyGL::EasyGLVertexBufferRenderer;

    struct LayoutColor
    {
        std::uint32_t value = 0;
        LayoutColor() = default;
        LayoutColor(const Color& color) : value(color.getPackedValueProperty()) {}
    };
    static_assert(sizeof(LayoutColor) == 4);

    // Declaration order intentionally differs from the stock program's input order.
    struct LayoutVertex
    {
        Vector3 position;
        LayoutColor first;
        LayoutColor second;
        Vector2 uv;
        float padding = 0;
    };
    static_assert(sizeof(LayoutVertex) == 32);

    VertexDeclaration ReorderedLayout(int colorOffset = 12)
    {
        return VertexDeclaration(sizeof(LayoutVertex), {
            VertexElement(colorOffset, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    }

    const char* const kLayoutVertex = R"(#version 300 es
precision highp float;
layout(location = 0) in vec4 aColor;
layout(location = 2) in vec3 aPosition;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
out vec4 vertexColor;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
    vertexColor = aColor;
}
)";
    const char* const kLayoutFragment = R"(#version 300 es
precision highp float;
in vec4 vertexColor;
out vec4 FragColor;
void main() { FragColor = vertexColor; }
)";
    const char* const kMultiLayoutVertex = R"(#version 300 es
precision highp float;
layout(location = 3) in vec4 aColor;
layout(location = 2) in vec3 aPosition;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
out vec4 vertexColor;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
    vertexColor = aColor;
}
)";

    struct VertexLayoutScene
    {
        GraphicsDevice& device;
        EasyGLRenderer& renderer;
        RenderTarget2D target;
        std::unique_ptr<IVertexBufferRenderer> buffer;
        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> indices;
        std::array<LayoutVertex, 6> vertices{{
            {Vector3(-1, -1, 0), Color::Red, Color::Lime, Vector2(0, 1)},
            {Vector3( 3, -1, 0), Color::Red, Color::Lime, Vector2(1, 1)},
            {Vector3(-1,  3, 0), Color::Red, Color::Lime, Vector2(0, 0)},
            {Vector3(-1, -1, 0), Color::Blue, Color::Yellow, Vector2(0, 1)},
            {Vector3( 3, -1, 0), Color::Blue, Color::Yellow, Vector2(1, 1)},
            {Vector3(-1,  3, 0), Color::Blue, Color::Yellow, Vector2(0, 0)},
        }};
        GpuDrawParams params{};

        explicit VertexLayoutScene(GraphicsDevice& graphics)
            : device(graphics), renderer(dynamic_cast<EasyGLRenderer&>(graphics.GetRenderer())),
              target(graphics, 4, 4), buffer(renderer.CreateVertexBuffer(6)),
              indices(renderer.CreateIndexBuffer16(3))
        {
            buffer->SetVertexDeclaration(ReorderedLayout());
            buffer->SetData(vertices.data(), 6, sizeof(LayoutVertex));
            const std::array<std::uint16_t, 3> data{0, 1, 2};
            indices->SetData16(data.data(), 3);
        }

        EasyGLVertexBufferRenderer& Native()
        {
            return static_cast<EasyGLVertexBufferRenderer&>(*buffer);
        }

        void Begin()
        {
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        Color End()
        {
            device.SetRenderTarget(nullptr);
            return BindingTargetCentre(target);
        }

        Color Draw(bool indexed = false, int instances = 0)
        {
            Begin();
            const auto& identity = Matrix::getIdentityProperty();
            if (instances > 0)
                renderer.DrawInstancedPrimitivesEx(*buffer, *indices, identity, identity, identity,
                    PrimitiveType::TriangleList, 1, instances, params);
            else if (indexed)
                renderer.DrawIndexedPrimitivesEx(*buffer, *indices, identity, identity, identity,
                    PrimitiveType::TriangleList, 1, params);
            else
                renderer.DrawPrimitivesEx(*buffer, identity, identity, identity,
                    PrimitiveType::TriangleList, 1, params);
            return End();
        }

        void MeshStream(int slot = 0)
        {
            auto& stream = params.vertexStreams[static_cast<std::size_t>(slot)];
            stream.buffer = buffer.get();
            stream.strideInBytes = sizeof(LayoutVertex);
            params.vertexStreamCount = std::max(params.vertexStreamCount, slot + 1);
        }
    };
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsIdenticalStockArraysAndIndexedDrawsReuseOneSpecification)
{
    VertexLayoutScene scene(*device);
    const auto before = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    ASSERT_EQ(scene.Draw(), Color::Red);
    const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    EXPECT_EQ(first, before + 1);
    for (int draw = 0; draw < 12; ++draw)
        EXPECT_EQ(scene.Draw(draw % 2 != 0), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first);
    EXPECT_EQ(scene.Native().GetVertexLayoutCacheSizeEXT(), 1u);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsAlternatingDeclarationsOnOneBufferChangePixels)
{
    VertexLayoutScene scene(*device);
    for (int draw = 0; draw < 6; ++draw)
    {
        scene.buffer->SetVertexDeclaration(ReorderedLayout(draw % 2 ? 16 : 12));
        EXPECT_EQ(scene.Draw(draw % 2 != 0), draw % 2 ? Color::Lime : Color::Red);
        const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
        EXPECT_EQ(scene.Draw(), draw % 2 ? Color::Lime : Color::Red);
        EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first);
    }
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsSetDataAllOptionsForgetsTheRemap)
{
    VertexLayoutScene scene(*device);
    ASSERT_EQ(scene.Draw(), Color::Red);
    for (const auto option : {SetDataOptions::None, SetDataOptions::Discard, SetDataOptions::NoOverwrite})
    {
        SCOPED_TRACE(static_cast<int>(option));
        for (auto& vertex : scene.vertices)
            vertex.first = option == SetDataOptions::Discard ? Color::Yellow : Color::Blue;
        const auto before = scene.renderer.GetVertexLayoutSpecificationCountEXT();
        scene.buffer->SetDataWithOptions(scene.vertices.data(), 6, sizeof(LayoutVertex), option);
        EXPECT_EQ(scene.Native().GetVertexLayoutCacheSizeEXT(), 0u);
        EXPECT_EQ(scene.Draw(), option == SetDataOptions::Discard ? Color::Yellow : Color::Blue);
        EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), before + 2);
    }
    scene.buffer->SetData(scene.vertices.data(), 6, sizeof(LayoutVertex));
    EXPECT_EQ(scene.Draw(true), Color::Blue);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsStockCustomAndSpriteBatchRestoreOnlyWhenNeeded)
{
    VertexLayoutScene scene(*device);
    ShaderEffect custom(*device, kLayoutVertex, kLayoutFragment);
    ASSERT_TRUE(custom.IsEffectValid());
    ASSERT_EQ(scene.Draw(), Color::Red);
    auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    scene.params.customEffectRenderer = custom.GetEffectRendererPtr();
    EXPECT_EQ(scene.Draw(), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first + 1);
    first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    EXPECT_EQ(scene.Draw(true), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first);
    scene.params.customEffectRenderer = nullptr;
    EXPECT_EQ(scene.Draw(true), Color::Red);
    SpriteBatch batch(*device);
    EXPECT_EQ(BindingSprite(*device, batch, scene.target, *texture), Color::White);
    first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    EXPECT_EQ(scene.Draw(), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first)
        << "SpriteBatch owns a different VAO";
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsCustomMultiStreamThenSingleStreamRestoresDeclarationOrder)
{
    VertexLayoutScene scene(*device);
    ShaderEffect single(*device, kLayoutVertex, kLayoutFragment);
    ShaderEffect multi(*device, kMultiLayoutVertex, kLayoutFragment);
    ASSERT_TRUE(single.IsEffectValid());
    ASSERT_TRUE(multi.IsEffectValid());
    auto colors = scene.renderer.CreateVertexBuffer(3);
    colors->SetVertexDeclaration(VertexDeclaration(4, {
        VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 1)}));
    const std::array<LayoutColor, 3> lime{Color::Lime, Color::Lime, Color::Lime};
    colors->SetData(lime.data(), 3, sizeof(LayoutColor));
    ASSERT_EQ(scene.Draw(), Color::Red);
    scene.MeshStream();
    scene.params.vertexStreamCount = 2;
    scene.params.vertexStreams[1].buffer = colors.get();
    scene.params.vertexStreams[1].strideInBytes = sizeof(LayoutColor);
    scene.params.customEffectRenderer = multi.GetEffectRendererPtr();
    EXPECT_EQ(scene.Draw(true), Color::Lime);
    scene.params.vertexStreamCount = 0;
    scene.params.customEffectRenderer = single.GetEffectRendererPtr();
    EXPECT_EQ(scene.Draw(), Color::Red);
    scene.params.customEffectRenderer = nullptr;
    EXPECT_EQ(scene.Draw(true), Color::Red);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsProgramInputsAndConvenienceDrawsRestoreOwnLayout)
{
    VertexLayoutScene scene(*device);
    scene.buffer->SetVertexDeclaration(VertexDeclaration(sizeof(LayoutVertex), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)}));
    for (const bool indexed : {false, true})
    {
        scene.params.textureEnabled = true;
        scene.params.vertexColorEnabled = false;
        scene.params.texture0 = &texture->GetRenderer();
        ASSERT_EQ(scene.Draw(indexed), Color::White);
        // The textured shader put UV at location 1, which is COLOR0 in declaration order.
        scene.Begin();
        const auto& identity = Matrix::getIdentityProperty();
        if (indexed)
            scene.renderer.DrawIndexedColoredPrimitives(*scene.buffer, *scene.indices,
                identity, identity, identity, PrimitiveType::TriangleList, 1);
        else
            scene.renderer.DrawColoredPrimitives(*scene.buffer, identity, identity, identity,
                PrimitiveType::TriangleList, 1);
        EXPECT_EQ(scene.End(), Color::Red);
        scene.params.textureEnabled = false;
        scene.params.vertexColorEnabled = true;
        EXPECT_EQ(scene.Draw(indexed), Color::Red);
    }
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsMissingAttributeDefaultsAreContextStateEvenOnHits)
{
    VertexLayoutScene scene(*device);
    scene.buffer->SetVertexDeclaration(VertexDeclaration(sizeof(LayoutVertex), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)}));
    ASSERT_EQ(scene.Draw(), Color::White);
    const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    metagl::glVertexAttrib4f(metagl::AttribLocation{1}, 0, 0, 0, 1);
    EXPECT_EQ(scene.Draw(), Color::White);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsStreamsIncludeOffsetStrideAndEffectiveSemanticMapping)
{
    VertexLayoutScene scene(*device);
    // Primary COLOR1 does not satisfy COLOR0, until the binding's collision metadata says it does.
    scene.buffer->SetVertexDeclaration(VertexDeclaration(sizeof(LayoutVertex), {
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 1),
        VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)}));
    auto colors = scene.renderer.CreateVertexBuffer(6);
    colors->SetVertexDeclaration(VertexDeclaration(4, {
        VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0)}));
    const std::array<LayoutColor, 6> data{Color::Lime, Color::Lime, Color::Lime,
                                  Color::Blue, Color::Blue, Color::Blue};
    colors->SetData(data.data(), 6, sizeof(LayoutColor));
    scene.MeshStream();
    scene.params.vertexStreamCount = 2;
    auto& secondary = scene.params.vertexStreams[1];
    secondary.buffer = colors.get();
    secondary.strideInBytes = sizeof(LayoutColor);
    ASSERT_EQ(scene.Draw(), Color::Lime);
    secondary.vertexOffset = 3;
    EXPECT_EQ(scene.Draw(true), Color::Blue);
    const auto afterOffset = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    EXPECT_EQ(scene.Draw(), Color::Blue);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), afterOffset);
    // A different stride is independently part of the pointer, even with identical texels.
    const std::array<LayoutColor, 6> allLime{Color::Lime, Color::Lime, Color::Lime,
                                    Color::Lime, Color::Lime, Color::Lime};
    colors->SetData(allLime.data(), 6, sizeof(LayoutColor));
    secondary.vertexOffset = 0;
    EXPECT_EQ(scene.Draw(), Color::Lime);
    const auto beforeStride = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    secondary.strideInBytes = 2 * sizeof(LayoutColor);
    EXPECT_EQ(scene.Draw(), Color::Lime);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), beforeStride + 1);
    auto& primary = scene.params.vertexStreams[0];
    primary.effectiveUsageIndexCount = 3;
    primary.effectiveUsageIndices = {0, 0, 0};
    EXPECT_EQ(scene.Draw(), Color::Red);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsSecondaryBufferLifetimeSurvivesAddressReuse)
{
    VertexLayoutScene scene(*device);
    scene.buffer->SetVertexDeclaration(VertexDeclaration(sizeof(LayoutVertex), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)}));
    scene.MeshStream();
    scene.params.vertexStreamCount = 2;
    std::optional<EasyGLVertexBufferRenderer> secondary;
    const auto replaceSource = [&](const Color& color)
    {
        secondary.emplace(3, nullptr);
        secondary->SetVertexDeclaration(VertexDeclaration(4, {
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0)}));
        const std::array<LayoutColor, 3> colors{color, color, color};
        secondary->SetData(colors.data(), 3, sizeof(LayoutColor));
        scene.params.vertexStreams[1].buffer = &*secondary;
        scene.params.vertexStreams[1].strideInBytes = sizeof(LayoutColor);
    };
    replaceSource(Color::Red);
    ASSERT_EQ(scene.Draw(), Color::Red);
    const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    const auto* address = &*secondary;
    replaceSource(Color::Lime);
    ASSERT_EQ(&*secondary, address);
    EXPECT_EQ(scene.Draw(), Color::Lime);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first + 1);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsInstancedStreamsCacheDivisorsAndOffsetsThenReturnToOrdinary)
{
    VertexLayoutScene scene(*device);
    auto firstColumns = scene.renderer.CreateVertexBuffer(3);
    auto lastColumns = scene.renderer.CreateVertexBuffer(3);
    const VertexDeclaration columns(32, {
        VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
        VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2)});
    firstColumns->SetVertexDeclaration(columns);
    lastColumns->SetVertexDeclaration(columns);
    const std::array<std::array<float, 8>, 3> first{{
        {1, 0, 0, 0, 0, 1, 0, 0}, {1, 0, 0, 0, 0, 1, 0, 0}, {1, 0, 0, 0, 0, 1, 0, 0}}};
    const std::array<std::array<float, 8>, 3> last{{
        {0, 0, 1, 0, 0, 0, 0, 1}, {0, 0, 1, 0, 4, 0, 0, 1}, {0, 0, 1, 0, 4, 0, 0, 1}}};
    firstColumns->SetData(first.data(), 3, 32);
    lastColumns->SetData(last.data(), 3, 32);
    scene.MeshStream();
    scene.params.vertexStreamCount = 3;
    auto& firstStream = scene.params.vertexStreams[1];
    auto& lastStream = scene.params.vertexStreams[2];
    firstStream.buffer = firstColumns.get();
    lastStream.buffer = lastColumns.get();
    firstStream.strideInBytes = lastStream.strideInBytes = 32;
    firstStream.instanceFrequency = lastStream.instanceFrequency = 1;
    ASSERT_EQ(scene.Draw(true, 2), Color::Red);
    const auto previous = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    EXPECT_EQ(scene.Draw(true, 2), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), previous);
    lastStream.instanceFrequency = 2;
    EXPECT_EQ(scene.Draw(true, 2), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), previous + 1);
    scene.Native().vao.bind();
    GLint divisor = 0;
    metagl::glGetVertexAttribiv(metagl::AttribLocation{15},
        metagl::VertexAttribParameter::ArrayDivisor, &divisor);
    EXPECT_EQ(divisor, 2);
    scene.Native().vao.unbind();
    lastStream.vertexOffset = 1;
    EXPECT_EQ(scene.Draw(true, 1), Color::Magenta);
    lastStream.vertexOffset = 0;
    EXPECT_EQ(scene.Draw(true, 1), Color::Red);
    ShaderEffect custom(*device, kLayoutVertex, kLayoutFragment);
    ASSERT_TRUE(custom.IsEffectValid());
    scene.params.customEffectRenderer = custom.GetEffectRendererPtr();
    EXPECT_EQ(scene.Draw(true, 1), Color::Red);
    scene.params.customEffectRenderer = nullptr;
    scene.params.vertexStreamCount = 0;
    EXPECT_EQ(scene.Draw(true), Color::Red);
    scene.Native().vao.bind();
    for (unsigned int location = 3; location < 16; ++location)
    {
        GLint enabled = 1;
        metagl::glGetVertexAttribiv(metagl::AttribLocation{location},
            metagl::VertexAttribParameter::ArrayEnabled, &enabled);
        metagl::glGetVertexAttribiv(metagl::AttribLocation{location},
            metagl::VertexAttribParameter::ArrayDivisor, &divisor);
        EXPECT_EQ(enabled, 0);
        EXPECT_EQ(divisor, 0);
    }
    scene.Native().vao.unbind();
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsBaseVertexFallbackRestoresPointersBeforeCacheHit)
{
    VertexLayoutScene scene(*device);
    ASSERT_EQ(scene.Draw(true), Color::Red);
    const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    scene.params.baseVertex = 3;
    EXPECT_EQ(scene.Draw(true), Color::Blue);
    EXPECT_EQ(scene.Draw(true), Color::Blue);
    scene.params.baseVertex = 0;
    EXPECT_EQ(scene.Draw(true), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsUserArrayAndIndexedStagingLeaveResidentBufferUntouched)
{
    VertexLayoutScene scene(*device);
    ASSERT_EQ(scene.Draw(), Color::Red);
    BasicEffect userEffect(*device);
    userEffect.setVertexColorEnabledProperty(true);
    const std::array<VertexPositionColor, 3> triangle{{
        {Vector3(-1, -1, 0), Color::Lime}, {Vector3(3, -1, 0), Color::Lime},
        {Vector3(-1, 3, 0), Color::Lime}}};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    for (const bool indexed : {false, true})
    {
        scene.Begin();
        userEffect.Apply();
        if (indexed)
            device->DrawUserIndexedPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 3,
                indices.data(), 0, 1);
        else
            device->DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
        EXPECT_EQ(scene.End(), Color::Lime);
        const auto afterStaging = scene.renderer.GetVertexLayoutSpecificationCountEXT();
        EXPECT_EQ(scene.Draw(), Color::Red);
        EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), afterStaging);
    }
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsContextRecoveryForgetsResidentProof)
{
    VertexLayoutScene scene(*device);
    ASSERT_EQ(scene.Draw(), Color::Red);
    const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    device->GetRenderer().DebugSimulateContextLoss();
    EXPECT_EQ(scene.Native().GetVertexLayoutCacheSizeEXT(), 0u);
    EXPECT_EQ(scene.Draw(true), Color::Red);
    const auto recovered = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    EXPECT_GT(recovered, first);
    EXPECT_EQ(scene.Draw(), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), recovered);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsExplicitInvalidationRepairsExternalVaoMutation)
{
    VertexLayoutScene scene(*device);
    ASSERT_EQ(scene.Draw(), Color::Red);
    const auto first = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    scene.Native().vao.bind();
    scene.Native().vao.disable_attribute(0);
    scene.Native().vao.set_attribute_divisor(1, 5);
    scene.Native().vao.unbind();
    scene.Native().InvalidateVertexLayoutEXT();
    EXPECT_EQ(scene.Native().GetVertexLayoutCacheSizeEXT(), 0u);
    EXPECT_EQ(scene.Draw(true), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), first + 1);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsManyLayoutsKeepOneBoundedRecordAndOneVao)
{
    VertexLayoutScene scene(*device);
    struct WideVertex
    {
        Vector3 position;
        std::array<LayoutColor, 60> colors;
    };
    std::array<WideVertex, 3> data{};
    for (std::size_t vertex = 0; vertex < data.size(); ++vertex)
    {
        data[vertex].position = scene.vertices[vertex].position;
        for (std::size_t color = 0; color < data[vertex].colors.size(); ++color)
            data[vertex].colors[color] = color % 2 ? Color::Lime : Color::Red;
    }
    const auto declaration = [](int color)
    {
        return VertexDeclaration(sizeof(WideVertex), {
            VertexElement(12 + 4 * color, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    };
    scene.buffer->SetVertexDeclaration(declaration(0));
    scene.buffer->SetData(data.data(), 3, sizeof(WideVertex));
    const auto nativeVao = scene.Native().vao.native_handle();
    const auto before = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    for (int draw = 0; draw < 120; ++draw)
    {
        SCOPED_TRACE(draw);
        scene.buffer->SetVertexDeclaration(declaration(draw % 60));
        EXPECT_EQ(scene.Draw(), draw % 2 ? Color::Lime : Color::Red);
        EXPECT_EQ(scene.Native().GetVertexLayoutCacheSizeEXT(), 1u);
        EXPECT_EQ(scene.Native().vao.native_handle(), nativeVao);
    }
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), before + 120);
    RecordProperty("distinctLayouts", 60);
    RecordProperty("residentRecordsPerBuffer", 1);
    RecordProperty("nativeVaosPerBuffer", 1);
}

TEST_F(EasyGLRedundantStateTest, VertexLayoutsStockCustomCompiledStockUseTheSameBufferSafely)
{
    if (!device->GetRenderer().SupportsCompiledEffects())
        GTEST_SKIP() << "CNA_EASYGL_COMPILED_EFFECTS is OFF in this build";
    VertexLayoutScene scene(*device);
    ShaderEffect custom(*device, kLayoutVertex, kLayoutFragment);
    ASSERT_TRUE(custom.IsEffectValid());
    const auto path = CNA::TestSupport::CompiledEffectDirectory() / "CnaConformanceEffect.fxb";
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                        std::istreambuf_iterator<char>()};
    auto runtime = scene.renderer.CreateCompiledEffect(bytes.data(), bytes.size());
    ASSERT_NE(runtime, nullptr);
    runtime->SetParameterTexture(5, texture.get());
    ASSERT_EQ(scene.Draw(), Color::Red);
    scene.params.customEffectRenderer = custom.GetEffectRendererPtr();
    EXPECT_EQ(scene.Draw(true), Color::Red);
    scene.params.customEffectRenderer = nullptr;
    CNA::Internal::Renderers::CompiledEffectDeviceState states;
    CNA::Internal::Renderers::CompiledEffectPassStateChanges changes;
    scene.Begin();
    runtime->SetTechnique(0);
    runtime->ApplyPass(0, states, changes);
    scene.params.compiledEffectRuntime = runtime.get();
    const auto& identity = Matrix::getIdentityProperty();
    scene.renderer.DrawPrimitivesEx(*scene.buffer, identity, identity, identity,
        PrimitiveType::TriangleList, 1, scene.params);
    const auto pixel = scene.End();
    EXPECT_NEAR(pixel.getRProperty(), 6, 2);
    EXPECT_NEAR(pixel.getGProperty(), 13, 2);
    EXPECT_NEAR(pixel.getBProperty(), 19, 2);
    scene.params.compiledEffectRuntime = nullptr;
    EXPECT_EQ(scene.Draw(), Color::Red);
    const auto after = scene.renderer.GetVertexLayoutSpecificationCountEXT();
    scene.Begin();
    runtime->ApplyPass(0, states, changes);
    scene.params.compiledEffectRuntime = runtime.get();
    scene.renderer.DrawIndexedPrimitivesEx(*scene.buffer, *scene.indices, identity, identity, identity,
        PrimitiveType::TriangleList, 1, scene.params);
    const auto indexedPixel = scene.End();
    EXPECT_EQ(indexedPixel, pixel);
    scene.params.compiledEffectRuntime = nullptr;
    EXPECT_EQ(scene.Draw(), Color::Red);
    EXPECT_EQ(scene.renderer.GetVertexLayoutSpecificationCountEXT(), after)
        << "compiled effects own a separate VAO";
}
