// SPDX-License-Identifier: MS-PL

// plans/plan_glbackends.md GLB-41: GraphicsDevice re-applies all sixteen pixel samplers and
// the rasterizer state before every draw. EasyGL records what it last wrote, so an unchanged state
// costs no GL call -- under WebGL every call crosses into JavaScript, and the anisotropy limit and
// viewport reads were synchronous round trips to the GPU process. These tests pin both halves of
// that contract against a real context: the GL objects always hold exactly what the latest
// application asked for, and an unchanged application really is not re-sent.

#include <algorithm>
#include <array>
#include <memory>

#include <gtest/gtest.h>
#include <metagl/metagl.hpp>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

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
            // Sampler objects are an ES 3.0-generation feature; the ES 2.0 profiles write sampling
            // state onto textures instead and are not covered here.
            if (!CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
                GTEST_SKIP() << "the selected renderer has no EasyGL sampler objects";

            device = std::make_unique<GraphicsDevice>(
                GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                PresentationParameters());
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
