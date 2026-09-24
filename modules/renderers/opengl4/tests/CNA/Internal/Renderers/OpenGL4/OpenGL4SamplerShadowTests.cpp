// SPDX-License-Identifier: MS-PL
//
// plans/plan_street_opengl4.md STREETGL4-0001: GraphicsDevice applies all sixteen sampler slots
// before every draw, and OpenGL4 wrote each slot object's ten parameters and bound it every time,
// whether anything had changed or not. cna-street's ~2 000 draws a frame made that ~500 000 GL
// calls, half of the frame: the street ran at 15 fps on OpenGL4 and 32 on EasyGL, same driver.
// A value the sampler object already holds is now not written again, and a slot already bound to
// its own unit is not bound again -- but every value that did change still reaches GL, which the
// alternating case below checks by what the draw actually samples.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    namespace GL4 = CNA::Internal::Renderers::OpenGL4::GL4;

    constexpr int kWidth = 16;

    /// Counts the renderer's sampler-object writes and binds while it exists.
    class SamplerCallCounter
    {
    public:
        SamplerCallCounter()
        {
            realParameteri_ = GL4::gl4_glSamplerParameteri;
            realParameterf_ = GL4::gl4_glSamplerParameterf;
            realBind_ = GL4::gl4_glBindSampler;
            GL4::gl4_glSamplerParameteri = &CountParameteri;
            GL4::gl4_glSamplerParameterf = &CountParameterf;
            GL4::gl4_glBindSampler = &CountBind;
            parameterWrites_ = 0;
            binds_ = 0;
        }
        ~SamplerCallCounter()
        {
            GL4::gl4_glSamplerParameteri = realParameteri_;
            GL4::gl4_glSamplerParameterf = realParameterf_;
            GL4::gl4_glBindSampler = realBind_;
        }
        SamplerCallCounter(const SamplerCallCounter&) = delete;
        SamplerCallCounter& operator=(const SamplerCallCounter&) = delete;

        [[nodiscard]] int ParameterWrites() const { return parameterWrites_; }
        [[nodiscard]] int Binds() const { return binds_; }

    private:
        static void CountParameteri(GLuint sampler, GLenum pname, GLint value)
        {
            ++parameterWrites_;
            realParameteri_(sampler, pname, value);
        }
        static void CountParameterf(GLuint sampler, GLenum pname, GLfloat value)
        {
            ++parameterWrites_;
            realParameterf_(sampler, pname, value);
        }
        static void CountBind(GLuint unit, GLuint sampler)
        {
            ++binds_;
            realBind_(unit, sampler);
        }

        static inline GL4::PFNGL4SAMPLERPARAMETERIPROC realParameteri_ = nullptr;
        static inline GL4::PFNGL4SAMPLERPARAMETERFPROC realParameterf_ = nullptr;
        static inline GL4::PFNGL4BINDSAMPLERPROC realBind_ = nullptr;
        static inline int parameterWrites_ = 0;
        static inline int binds_ = 0;
    };

    /// A 2x1 black|white texture stretched over a kWidth x 1 target through slot 0.
    class StretchedTexel
    {
    public:
        explicit StretchedTexel(GraphicsDevice& device)
            : device_(device), target_(device, kWidth, 1),
              texture_(Texture2D::CreateFromPixels(device, 2, 1,
                                                   std::vector<std::uint8_t>{0, 0, 0, 255,
                                                                             255, 255, 255, 255})),
              effect_(device)
        {
            effect_.setTextureProperty(&texture_);
            effect_.setTextureEnabledProperty(true);
            effect_.setWorldProperty(Matrix::getIdentityProperty());
            effect_.setViewProperty(Matrix::getIdentityProperty());
            effect_.setProjectionProperty(Matrix::getIdentityProperty());
        }

        /// Draws with @p sampler in slot 0 and returns the target's red channel, left to right.
        std::vector<int> Draw(const SamplerState& sampler)
        {
            device_.SetRenderTarget(&target_);
            device_.setBlendStateProperty(BlendState::Opaque);
            device_.setRasterizerStateProperty(RasterizerState::CullNone);
            device_.Clear(Color(255, 0, 255, 255));
            device_.getSamplerStatesProperty()[0] = sampler;
            effect_.Apply();
            const VertexPositionTexture quad[6] = {
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            };
            device_.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            device_.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> pixels(kWidth);
            target_.GetData(pixels.data(), kWidth);
            std::vector<int> red;
            for (const Color& pixel : pixels) red.push_back(pixel.getRProperty());
            return red;
        }

    private:
        GraphicsDevice& device_;
        RenderTarget2D target_;
        Texture2D texture_;
        BasicEffect effect_;
    };

    /// Point sampling of the two texels gives black and white and nothing in between. Where the
    /// edge falls is the pixel-centre convention's business, not this test's.
    bool IsPointSampled(const std::vector<int>& red)
    {
        bool black = false, white = false;
        for (const int value : red)
        {
            if (value == 0) black = true;
            else if (value == 255) white = true;
            else return false;
        }
        return black && white;
    }

    /// Linear sampling of the two texels blends them between the texel centres.
    bool IsLinearSampled(const std::vector<int>& red)
    {
        int blended = 0;
        for (const int value : red)
            if (value > 24 && value < 231) ++blended;
        return blended >= kWidth / 4;
    }
}

TEST(OpenGL4SamplerShadow, ReapplyingAnUnchangedSamplerStateIssuesNoGl)
{
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    StretchedTexel scene(device);
    (void)scene.Draw(SamplerState::PointClamp);   // every slot's first application writes

    SamplerCallCounter counter;
    EXPECT_TRUE(IsPointSampled(scene.Draw(SamplerState::PointClamp)));
    EXPECT_EQ(0, counter.ParameterWrites()) << "a draw re-applying the same sixteen slots wrote "
                                               "sampler parameters the objects already held";
    EXPECT_EQ(0, counter.Binds()) << "slots already bound to their own units were bound again";
}

TEST(OpenGL4SamplerShadow, AChangedSamplerStateWritesOnlyWhatChanged)
{
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    StretchedTexel scene(device);
    (void)scene.Draw(SamplerState::PointClamp);

    // PointClamp -> LinearClamp changes slot 0's minification and magnification filters and
    // nothing else: the addressing, the LOD range and bias, the comparison mode, the anisotropy
    // (1 for both) and the other fifteen slots all stay.
    SamplerCallCounter counter;
    EXPECT_TRUE(IsLinearSampled(scene.Draw(SamplerState::LinearClamp)));
    EXPECT_EQ(2, counter.ParameterWrites());
    EXPECT_EQ(0, counter.Binds());
}

TEST(OpenGL4SamplerShadow, AlternatingSamplerStatesStillSampleWhatEachDrawAsks)
{
    // The guard on the shadow itself: a record that ever disagreed with the sampler object would
    // skip a write that mattered, and the draw would sample with the previous state.
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    StretchedTexel scene(device);
    for (int round = 0; round < 3; ++round)
    {
        EXPECT_TRUE(IsLinearSampled(scene.Draw(SamplerState::LinearClamp))) << "round " << round;
        EXPECT_TRUE(IsPointSampled(scene.Draw(SamplerState::PointClamp))) << "round " << round;
        EXPECT_TRUE(IsLinearSampled(scene.Draw(SamplerState::LinearWrap))) << "round " << round;
        EXPECT_TRUE(IsPointSampled(scene.Draw(SamplerState::PointWrap))) << "round " << round;
    }
}

#endif
