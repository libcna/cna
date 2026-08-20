// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact RenderTargetCube/FBO proof for the native OpenGL 2.1 graphics
// renderer -- render a distinct color into each of the 6 faces, read each back directly, and
// prove a real depth-test occlusion + mipmap generation inside the shared cube FBO.
//
// Check A -- RenderTargetCube construction succeeds and reports the requested size.
// Check B -- each of the 6 faces, drawn with its own solid color via BasicEffect and unbound,
//   reads back its own color exactly via GetData() -- proves the FBO's color attachment is
//   really re-pointed at the correct GL_TEXTURE_CUBE_MAP_POSITIVE_X.. target per face, not all 6
//   landing on the same one.
// Check C -- real depth-test occlusion inside one face: a nearer quad wins regardless of draw
//   order (proves the shared depth/stencil renderbuffer is real for cube rendering too).
// Check D -- mipmap generation: a mipMap=true cube's level-1 mip (on the face last rendered)
//   matches the level-0 drawn color.
// Check E -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;
    constexpr int kSize = 8;

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

class OpenGL2RenderTargetCubeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTargetCube> rtc_;
    std::unique_ptr<RenderTargetCube> rtcDepth_;
    std::unique_ptr<RenderTargetCube> rtcMip_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void DrawFullscreenColorQuad(GraphicsDevice& dev, float z, const Color& color)
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

    Color ReadFacePixel(RenderTargetCube& rtc, CubeMapFace face, int x, int y, int level = 0)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rtc.GetData(face, level, &rect, &px, 0, 1);
        return px;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();

        bool threwOnConstruct = false;
        try
        {
            rtc_ = std::make_unique<RenderTargetCube>(dev, kSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception&) { threwOnConstruct = true; }
        Check("RenderTargetCube construction reports the requested size",
              !threwOnConstruct && rtc_->getWidthProperty() == kSize && rtc_->getHeightProperty() == kSize);

        rtcDepth_ = std::make_unique<RenderTargetCube>(dev, kSize, false, SurfaceFormat::Color,
                                                        DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        rtcMip_ = std::make_unique<RenderTargetCube>(dev, kSize, true, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check B: each face gets its own distinct color.
        const std::array<Color, 6> faceColors = {
            Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255),
            Color(255, 255, 0, 255), Color(255, 0, 255, 255), Color(0, 255, 255, 255),
        };
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (int i = 0; i < 6; ++i)
        {
            dev.SetRenderTarget(rtc_.get(), faces[i]);
            dev.Clear(Color::Black);
            DrawFullscreenColorQuad(dev, 0.0f, faceColors[i]);
        }
        dev.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);
        if (runChecks)
        {
            bool allFacesMatch = true;
            for (int i = 0; i < 6; ++i)
            {
                const Color got = ReadFacePixel(*rtc_, faces[i], kSize / 2, kSize / 2);
                if (!Matches(got, faceColors[i], 10))
                {
                    allFacesMatch = false;
                    std::printf("  face %d: expected %s got %s\n", i, ColorStr(faceColors[i]).c_str(), ColorStr(got).c_str());
                }
            }
            Check("each of the 6 faces reads back its own distinct color", allFacesMatch);
        }

        // Check C: depth-test occlusion inside one face.
        dev.SetRenderTarget(rtcDepth_.get(), CubeMapFace::PositiveX);
        dev.Clear(Color::Black);
        DrawFullscreenColorQuad(dev, -0.5f, Color::Blue); // nearer, first
        DrawFullscreenColorQuad(dev, 0.5f, Color::Red);   // farther, second
        dev.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);
        if (runChecks)
        {
            const Color got = ReadFacePixel(*rtcDepth_, CubeMapFace::PositiveX, kSize / 2, kSize / 2);
            Check("depth test inside a RenderTargetCube face: nearer quad wins: got=" + ColorStr(got),
                  Matches(got, Color::Blue, 10));
        }

        // Check D: mipmap generation.
        dev.SetRenderTarget(rtcMip_.get(), CubeMapFace::PositiveX);
        dev.Clear(Color::Black);
        DrawFullscreenColorQuad(dev, 0.0f, Color(0, 200, 255, 255));
        dev.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);
        if (runChecks)
        {
            const Color mip1 = ReadFacePixel(*rtcMip_, CubeMapFace::PositiveX, 1, 1, /*level=*/1);
            Check("mipMap=true RenderTargetCube's level-1 mip matches the level-0 color: got=" + ColorStr(mip1),
                  Matches(mip1, Color(0, 200, 255, 255), 20));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(std::to_string(kTotalFrames) + " frames render with no exception", true);
            std::printf("=== %d/%d PASS ===\n", passCount_, 5);
            result_ = (passCount_ == 5) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2RenderTargetCubeTest()
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

    OpenGL2RenderTargetCubeTest game;
    game.Run();
    return game.getResult();
}
