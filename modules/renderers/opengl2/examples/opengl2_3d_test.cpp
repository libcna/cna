// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact 3D vertex-format proof for the native OpenGL 2.1 graphics renderer
// -- colored3d, textured3d, colored_textured3d, and a real depth-test occlusion proof, all
// drawn through the real public XNA API (VertexBuffer/BasicEffect/GraphicsDevice.DrawPrimitives)
// and verified via ReadBackbuffer, mirroring opengl2_2d_test.cpp's rigor for the 3D path.
//
// Checks A-D use World=View=Projection=Identity, so vertex XYZ IS clip-space XYZ directly
// (matches sdlgpu_samplerstate_test.cpp's own established convention for this kind of test).
//
// Check A -- a colored3d triangle (VertexPositionColor, BasicEffect.VertexColorEnabled=true)
//   reads back its own vertex color exactly.
// Check B -- real depth-test occlusion: a NEARER blue quad (z=-0.5) drawn BEFORE a FARTHER red
//   quad (z=0.5) still shows blue (expected -- draw order matches depth order); a NEARER blue
//   quad drawn AFTER a farther red quad still shows blue too (depth test, not draw order, decides
//   the winner) -- proves the depth buffer is real, not just "didn't throw".
// Check C -- a textured3d quad (VertexPositionTexture, BasicEffect.Texture) reads back the
//   sampled texture color exactly.
// Check D -- a colored_textured3d quad (VertexPositionColorTexture) reads back texture*vertex
//   color exactly.
// Check E -- a real LookAt + perspective WVP keeps a unit quad centered and bounded. This catches
//   a second transpose of CNA's row-major-flat Matrix upload, which identity-only checks cannot.
// Check F -- 120 frames of the whole scene render with no exception.
//
// BasicEffect lighting (VertexPositionNormalTexture/EnableDefaultLighting) is deliberately not
// covered here -- this renderer's 3D shaders are unlit (plans/plan_opengl2.md's own documented
// follow-up work), so a lit-scene pixel assertion would not be meaningful yet.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 120;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
}

class OpenGL2ThreeDTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> texWhite_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    // Full-viewport quad (CW winding, visible under the default RasterizerState.
    // CullCounterClockwise with Identity view/projection -- matches
    // sdlgpu_samplerstate_test.cpp's own documented convention for this exact case).
    static void FullScreenQuad(Vector3 tl, Vector3 tr, Vector3 bl, Vector3 br,
                               VertexPositionTexture out[6])
    {
        out[0] = {tl, Vector2(0, 0)}; out[1] = {br, Vector2(1, 1)}; out[2] = {bl, Vector2(0, 1)};
        out[3] = {tl, Vector2(0, 0)}; out[4] = {tr, Vector2(1, 0)}; out[5] = {br, Vector2(1, 1)};
    }

    void DrawColoredTriangle(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionColor verts[3] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Color(255, 128, 0, 255)},
            {Vector3(1.0f, 1.0f, 0.0f), Color(255, 128, 0, 255)},
            {Vector3(-1.0f, -1.0f, 0.0f), Color(255, 128, 0, 255)},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        vb.SetData(verts, 0, 3);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawDepthQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionColor verts[6] = {
            {Vector3(-1.0f, 1.0f, z), color}, {Vector3(1.0f, -1.0f, z), color}, {Vector3(-1.0f, -1.0f, z), color},
            {Vector3(-1.0f, 1.0f, z), color}, {Vector3(1.0f, 1.0f, z), color},  {Vector3(1.0f, -1.0f, z), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawTexturedQuad(GraphicsDevice& dev, Texture2D& tex, const Color* tint)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.VertexColorEnabled = (tint != nullptr);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        if (tint)
        {
            const VertexPositionColorTexture verts[6] = {
                {Vector3(-1.0f, 1.0f, 0.0f), *tint, Vector2(0, 0)}, {Vector3(1.0f, -1.0f, 0.0f), *tint, Vector2(1, 1)}, {Vector3(-1.0f, -1.0f, 0.0f), *tint, Vector2(0, 1)},
                {Vector3(-1.0f, 1.0f, 0.0f), *tint, Vector2(0, 0)}, {Vector3(1.0f, 1.0f, 0.0f), *tint, Vector2(1, 0)},  {Vector3(1.0f, -1.0f, 0.0f), *tint, Vector2(1, 1)},
            };
            VertexBuffer vb(dev, VertexPositionColorTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        else
        {
            VertexPositionTexture verts[6];
            FullScreenQuad(Vector3(-1, 1, 0), Vector3(1, 1, 0), Vector3(-1, -1, 0), Vector3(1, -1, 0), verts);
            VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 0, 6);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        dev.SetVertexBuffer(nullptr);
    }

    void DrawPerspectiveQuad(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 6.0f), Vector3::Zero, Vector3::Up));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            0.78539816339f, 4.0f / 3.0f, 0.1f, 100.0f));
        fx.Apply();

        const Color yellow(255, 220, 0, 255);
        const VertexPositionColor verts[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), yellow},
            {Vector3( 1.0f, -1.0f, 0.0f), yellow},
            {Vector3(-1.0f, -1.0f, 0.0f), yellow},
            {Vector3(-1.0f,  1.0f, 0.0f), yellow},
            {Vector3( 1.0f,  1.0f, 0.0f), yellow},
            {Vector3( 1.0f, -1.0f, 0.0f), yellow},
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        texWhite_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 200, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        const bool runChecks = (frame_ == 1);

        // Check A: colored3d.
        dev.Clear(Color::Black);
        DrawColoredTriangle(dev);
        if (runChecks)
        {
            const Color got = ReadPixel(80, 60);
            Check(Matches(got, Color(255, 128, 0, 255), 10), "colored3d triangle reads back its vertex color: got=" + ColorStr(got));
        }

        // Check B: depth-test occlusion, both draw orders.
        dev.Clear(Color::Black);
        DrawDepthQuad(dev, -0.5f, Color::Blue);   // nearer, drawn first
        DrawDepthQuad(dev, 0.5f, Color::Red);     // farther, drawn second
        const Color nearFirst = ReadPixel(160, 120);

        dev.Clear(Color::Black);
        DrawDepthQuad(dev, 0.5f, Color::Red);     // farther, drawn first
        DrawDepthQuad(dev, -0.5f, Color::Blue);   // nearer, drawn second
        const Color nearSecond = ReadPixel(160, 120);
        if (runChecks)
        {
            Check(Matches(nearFirst, Color::Blue, 10),
                  "depth test: nearer quad drawn first still wins: got=" + ColorStr(nearFirst));
            Check(Matches(nearSecond, Color::Blue, 10),
                  "depth test: nearer quad drawn second still wins (real depth buffer, not draw order): got=" + ColorStr(nearSecond));
        }

        // Check C: textured3d.
        dev.Clear(Color::Black);
        DrawTexturedQuad(dev, *texWhite_, nullptr);
        if (runChecks)
        {
            const Color got = ReadPixel(160, 120);
            Check(Matches(got, Color(0, 200, 255, 255), 10), "textured3d quad reads back the sampled texture color: got=" + ColorStr(got));
        }

        // Check D: colored_textured3d (texture * vertex-color tint).
        dev.Clear(Color::Black);
        const Color tint(128, 128, 128, 255);
        DrawTexturedQuad(dev, *texWhite_, &tint);
        if (runChecks)
        {
            const Color got = ReadPixel(160, 120);
            // (0,200,255) * (128,128,128)/255 ~= (0,100,128).
            Check(Matches(got, Color(0, 100, 128, 255), 15),
                  "colored_textured3d quad reads back texture*vertexColor: got=" + ColorStr(got));
        }

        // Check E: non-identity camera/projection matrix packing.
        dev.Clear(Color::Black);
        DrawPerspectiveQuad(dev);
        if (runChecks)
        {
            const Color center = ReadPixel(160, 120);
            const Color corner = ReadPixel(20, 20);
            Check(Matches(center, Color(255, 220, 0, 255), 10)
                      && Matches(corner, Color::Black, 10),
                  "LookAt+perspective WVP is centered and bounded: center=" + ColorStr(center)
                      + ", corner=" + ColorStr(corner));
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, "120 frames of the 3D scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 7);
            result_ = (passCount_ == 7) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2ThreeDTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2ThreeDTest game;
    game.Run();
    return game.getResult();
}
