// SPDX-License-Identifier: MS-PL
//
// CNA-ISLAND-002, on a real GL context: stock SkinnedEffect programs draw the whole 72-bone XNA
// palette, and a stock program that fails to compile or link stops there and refuses loudly
// instead of being queried, marked ready and drawn as nothing.
//
// Runs natively on OPENGLES3/OPENGL33 (tools/platform/run_gpu_tests_private.sh --exec ...
// CnaRendererTests) and in a browser WebGL 2 context through cna_browser_easygl_stock_program_page
// (scripts/run_browser_tests.mjs). Under Node, which has no WebGL, it skips.

#include <gtest/gtest.h>

#if defined(CNA_RENDERER_EASYGL) || defined(CNA_RENDERER_PRESENT_OPENGLES3) \
    || defined(CNA_RENDERER_PRESENT_OPENGL33) || defined(CNA_RENDERER_PRESENT_WEBGL2)

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <metagl/metagl.hpp>

#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/Internal/Renderers/EasyGL/GlStockShaderSources.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/InvalidOperationException.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
// A browser page has a document; Node, which runs the wasm unit-test executables, has no WebGL.
EM_JS(int, CnaStockProgramTestHasDocument, (), { return typeof document !== 'undefined' ? 1 : 0; });
#endif

namespace
{
    using namespace CNA::Testing::Renderers;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework;  // NOLINT(google-build-using-namespace)
    using namespace Microsoft::Xna::Framework::Graphics;  // NOLINT(google-build-using-namespace)
    using CNA::Internal::Renderers::EasyGL::EasyGLRenderer;

    /// The 52-byte XNA skinned record: Position, Normal, TextureCoordinate, BlendWeight, BlendIndices.
    struct SkinnedVertex
    {
        float position[3];
        float normal[3];
        float uv[2];
        float weights[4];
        std::uint8_t indices[4];
    };
    static_assert(sizeof(SkinnedVertex) == 52);

    VertexDeclaration SkinnedDeclaration()
    {
        return VertexDeclaration(52, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        });
    }

    /// Two triangles covering [x0,x1] x [y0,y1] at z = 0.5, weighted to @p boneA and @p boneB.
    void AppendQuad(std::vector<SkinnedVertex>& vertices, float x0, float y0, float x1, float y1,
                    std::uint8_t boneA, float weightA, std::uint8_t boneB, float weightB)
    {
        const auto at = [&](float x, float y) {
            return SkinnedVertex{{x, y, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
                                 {weightA, weightB, 0.0f, 0.0f}, {boneA, boneB, 0, 0}};
        };
        vertices.push_back(at(x0, y1));
        vertices.push_back(at(x1, y1));
        vertices.push_back(at(x0, y0));
        vertices.push_back(at(x1, y1));
        vertices.push_back(at(x1, y0));
        vertices.push_back(at(x0, y0));
    }

    // GLSL ES 3.00 sources that compile and then fail to link: the two stages declare one varying
    // with different types. GLSL ES 3.00 and desktop GLSL 3.30 (section 4.3.4 of each) both make
    // that a link error. An input the vertex stage merely never declares is not portable: GLSL ES
    // refuses it, Mesa's desktop 4.6 core profile links it. The name is what a linker's log has
    // to mention.
    const char* const kUnlinkableVertex = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 aPos;
uniform mat4 uWVP;
out vec3 vCnaIsland002Mismatch;
void main(){ vCnaIsland002Mismatch=aPos; gl_Position=uWVP*vec4(aPos,1.0); }
)";
    const char* const kUnlinkableFragment = R"(#version 300 es
precision highp float;
in vec4 vCnaIsland002Mismatch;
out vec4 FragColor;
void main(){ FragColor=vCnaIsland002Mismatch; }
)";

    class EasyGLStockProgramTest : public ::testing::Test
    {
    protected:
        static constexpr int kSize = 32;
        std::unique_ptr<GraphicsDevice> device;
        std::unique_ptr<RenderTarget2D> target;
        std::unique_ptr<Texture2D> white;

        void SetUp() override
        {
            if (!CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
                GTEST_SKIP() << "the stock GL programs belong to the EasyGL renderers";
#if defined(__EMSCRIPTEN__)
            if (CnaStockProgramTestHasDocument() == 0)
                GTEST_SKIP() << "needs a browser WebGL 2 context; Node has none";
#endif
            device = std::make_unique<GraphicsDevice>(
                GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                PresentationParameters::CreateWindowedEXT(64, 64));
            target = std::make_unique<RenderTarget2D>(*device, kSize, kSize);
            white = std::make_unique<Texture2D>(*device, 1, 1);
            const Color opaque = Color::White;
            white->SetData(&opaque, 1);
        }

        [[nodiscard]] EasyGLRenderer& Renderer() const
        {
            auto* renderer = dynamic_cast<EasyGLRenderer*>(&device->GetRenderer());
            if (renderer == nullptr)
                throw std::runtime_error("the active renderer is not EasyGL");
            return *renderer;
        }

        void BeginTarget()
        {
            device->SetRenderTarget(target.get());
            device->Clear(Color::Black);
            device->setBlendStateProperty(BlendState::Opaque);
            device->setDepthStencilStateProperty(DepthStencilState::None);
            device->setRasterizerStateProperty(RasterizerState::CullNone);
        }

        [[nodiscard]] std::vector<Color> EndTarget()
        {
            device->SetRenderTarget(nullptr);
            std::vector<Color> pixels(kSize * kSize);
            target->GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        }

        /**
         * Draws three quads through a stock SkinnedEffect, each with its own emissive colour and
         * otherwise unlit, so a covered pixel is exactly that colour. Clip space is the identity
         * World*View*Projection, and a 32-pixel target maps x to column (x+1)*16 and y to row
         * (1-y)*16.
         *
         *  red:   the square [-0.25,0.25]^2 on bone 71 = Translation(-0.5, 0.5, 0)
         *         -> [-0.75,-0.25] x [0.25,0.75] -> columns 4..12, rows 4..12.
         *  green: the rectangle [0,0.5] x [0,0.25] on bone 40 = RotationZ(90 deg) *
         *         Translation(0.5,-0.75,0). XNA's v*M turns (x,y) into (-y,x) first:
         *         [-0.25,0] x [0,0.5], then [0.25,0.5] x [-0.75,-0.25] -> columns 20..24,
         *         rows 20..28. A transposed palette rotates the other way and loses the
         *         translation.
         *  blue:  the square [-0.25,0.25]^2 weighted 0.5/0.5 to bone 3 = Translation(0.25,0.75,0)
         *         and bone 70 = Translation(0.75,0.25,0): (0.5,0.5) -> [0.25,0.75]^2 ->
         *         columns 20..28, rows 4..12.
         *
         * Any bone left at identity draws its quad at the bind pose around the centre instead.
         *
         * @p errorAfterDraws, when given, receives glGetError() right after the draw calls --
         * thrown or not -- and before the readback, which may consume GL's error flags itself.
         */
        [[nodiscard]] std::vector<Color> DrawSkinnedScene(
            bool preferPerPixelLighting, metagl::ErrorCode* errorAfterDraws = nullptr)
        {
            std::vector<Matrix> palette(SkinnedEffect::MaxBones, Matrix::getIdentityProperty());
            palette[71] = Matrix::CreateTranslation(-0.5f, 0.5f, 0.0f);
            palette[40] = Matrix::CreateRotationZ(1.5707963267948966f) *
                          Matrix::CreateTranslation(0.5f, -0.75f, 0.0f);
            palette[3] = Matrix::CreateTranslation(0.25f, 0.75f, 0.0f);
            palette[70] = Matrix::CreateTranslation(0.75f, 0.25f, 0.0f);

            std::vector<SkinnedVertex> vertices;
            AppendQuad(vertices, -0.25f, -0.25f, 0.25f, 0.25f, 71, 1.0f, 0, 0.0f);
            AppendQuad(vertices, 0.0f, 0.0f, 0.5f, 0.25f, 40, 1.0f, 0, 0.0f);
            AppendQuad(vertices, -0.25f, -0.25f, 0.25f, 0.25f, 3, 0.5f, 70, 0.5f);
            VertexBuffer buffer(*device, SkinnedDeclaration(), static_cast<int>(vertices.size()),
                                BufferUsage::None);
            buffer.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                              static_cast<int>(sizeof(SkinnedVertex)));

            SkinnedEffect effect(*device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.DirectionalLight0.setEnabledProperty(false);
            effect.setTextureProperty(white.get());
            effect.setPreferPerPixelLightingProperty(preferPerPixelLighting);
            effect.SetBoneTransforms(palette);

            BeginTarget();
            device->SetVertexBuffer(&buffer);
            if (errorAfterDraws != nullptr)
                DrainGlErrors();
            const Vector3 colours[3] = {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
            try
            {
                for (int quad = 0; quad < 3; ++quad)
                {
                    effect.setEmissiveColorProperty(colours[quad]);
                    for (auto& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
                        pass.Apply();
                    device->DrawPrimitives(PrimitiveType::TriangleList, quad * 6, 2);
                }
            }
            catch (...)
            {
                if (errorAfterDraws != nullptr)
                    *errorAfterDraws = metagl::glGetError();
                device->SetVertexBuffer(nullptr);
                device->SetRenderTarget(nullptr);
                throw;
            }
            if (errorAfterDraws != nullptr)
                *errorAfterDraws = metagl::glGetError();
            device->SetVertexBuffer(nullptr);
            return EndTarget();
        }

        static void DrainGlErrors()
        {
            for (int i = 0; i < 16 && metagl::glGetError() != metagl::ErrorCode::NoError; ++i)
            {
            }
        }

        [[nodiscard]] static std::string Rgba(const Color& colour)
        {
            return std::to_string(colour.getRProperty()) + "," +
                   std::to_string(colour.getGProperty()) + "," +
                   std::to_string(colour.getBProperty()) + "," +
                   std::to_string(colour.getAProperty());
        }

        static void ExpectSkinnedScene(const std::vector<Color>& pixels)
        {
            const auto at = [&pixels](int column, int row) {
                return Rgba(pixels[static_cast<std::size_t>(row * kSize + column)]);
            };
            EXPECT_EQ(at(8, 8), "255,0,0,255") << "bone 71 (red)";
            EXPECT_EQ(at(22, 24), "0,255,0,255") << "bone 40 (green)";
            EXPECT_EQ(at(24, 8), "0,0,255,255") << "bones 3 + 70 (blue)";
            // The bind poses: red/blue around columns/rows 12..20, green at columns 16..24,
            // rows 12..16. Nothing may remain there, nor in the empty lower-left quadrant.
            EXPECT_EQ(at(14, 14), "0,0,0,255") << "a bind-pose square";
            EXPECT_EQ(at(20, 14), "0,0,0,255") << "the bind-pose rectangle";
            EXPECT_EQ(at(8, 24), "0,0,0,255") << "empty quadrant";
        }
    };
}

TEST_F(EasyGLStockProgramTest, VertexLitSkinnedEffectDrawsBonesBeyondFourteenUpToSeventyOne)
{
    // XNA's default, PreferPerPixelLighting = false: the program Android's SwiftShader refused.
    ExpectSkinnedScene(DrawSkinnedScene(false));
}

TEST_F(EasyGLStockProgramTest, PixelLitSkinnedEffectDrawsBonesBeyondFourteenUpToSeventyOne)
{
    ExpectSkinnedScene(DrawSkinnedScene(true));
}

TEST_F(EasyGLStockProgramTest, ALinkFailureStopsInitializationAndRefusesWithTheInfoLog)
{
    Renderer().OverrideStockProgramSourceForTestingEXT(
        "skinned (vertex-lit)", kUnlinkableVertex, kUnlinkableFragment);
    // The test executable links its own meta-gl copy (libcna.so does not export it). Its function
    // table must be loaded from the renderer's context before glGetError is read directly, as in
    // EasyGLRedundantStateTests; otherwise this test only works after another one has loaded it.
    if (!::metagl::IsInitialized())
        ASSERT_TRUE(::metagl::Initialize(Renderer().GetProcAddressLoaderEXT()));

    std::string first;
    metagl::ErrorCode errorAfterDraws = metagl::ErrorCode::NoError;
    try
    {
        (void)DrawSkinnedScene(false, &errorAfterDraws);
        ADD_FAILURE() << "a draw through an unlinked stock program must throw";
    }
    catch (const System::InvalidOperationException& refusal)
    {
        first = refusal.what();
    }
    EXPECT_NE(first.find("'skinned (vertex-lit)'"), std::string::npos) << first;
    EXPECT_NE(first.find("failed to link"), std::string::npos) << first;
    // GL's own info log is carried, not merely a status: it names the offending varying.
    EXPECT_NE(first.find("vCnaIsland002Mismatch"), std::string::npos) << first;
    // Initialization stopped at the link: the unlinked program was neither asked for a uniform
    // location nor made current, each of which raises GL_INVALID_OPERATION.
    EXPECT_EQ(errorAfterDraws, metagl::ErrorCode::NoError)
        << "GL error 0x" << std::hex << static_cast<unsigned>(errorAfterDraws);

    // Every later use refuses the same way; it never quietly draws nothing.
    try
    {
        (void)DrawSkinnedScene(false);
        ADD_FAILURE() << "the second draw must throw too";
    }
    catch (const System::InvalidOperationException& refusal)
    {
        EXPECT_EQ(std::string(refusal.what()), first);
    }

    // A program that links is unaffected: the per-pixel skinned one and BasicEffect still draw.
    ExpectSkinnedScene(DrawSkinnedScene(true));

    const VertexPositionColor quad[6] = {
        VertexPositionColor(Vector3(-1, 1, 0.5f), Color::Yellow),
        VertexPositionColor(Vector3(1, 1, 0.5f), Color::Yellow),
        VertexPositionColor(Vector3(-1, -1, 0.5f), Color::Yellow),
        VertexPositionColor(Vector3(1, 1, 0.5f), Color::Yellow),
        VertexPositionColor(Vector3(1, -1, 0.5f), Color::Yellow),
        VertexPositionColor(Vector3(-1, -1, 0.5f), Color::Yellow),
    };
    VertexBuffer buffer(*device, VertexPositionColor::getVertexDeclarationStatic(), 6,
                        BufferUsage::None);
    buffer.SetData(quad, 6);
    BasicEffect basic(*device);
    basic.setVertexColorEnabledProperty(true);
    BeginTarget();
    device->SetVertexBuffer(&buffer);
    for (auto& pass : basic.getCurrentTechniqueProperty()->getPassesProperty())
        pass.Apply();
    device->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device->SetVertexBuffer(nullptr);
    const std::vector<Color> pixels = EndTarget();
    EXPECT_EQ(Rgba(pixels[16 * kSize + 16]), "255,255,0,255");
}

TEST_F(EasyGLStockProgramTest, ACompileFailureRefusesWithTheCompilerLog)
{
    Renderer().OverrideStockProgramSourceForTestingEXT(
        "skinned", "#version 300 es\nvoid main(){ cnaIsland002Undeclared(); }\n",
        kUnlinkableFragment);
    try
    {
        (void)DrawSkinnedScene(true);
        ADD_FAILURE() << "a draw through an uncompiled stock program must throw";
    }
    catch (const System::InvalidOperationException& refusal)
    {
        const std::string message = refusal.what();
        EXPECT_NE(message.find("'skinned' vertex shader failed to compile"), std::string::npos)
            << message;
        EXPECT_NE(message.find("cnaIsland002Undeclared"), std::string::npos) << message;
    }
}

#endif
