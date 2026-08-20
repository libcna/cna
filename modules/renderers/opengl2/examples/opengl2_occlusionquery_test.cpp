// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact proof for OcclusionQuery on the native OpenGL 2.1 graphics
// renderer (GL_SAMPLES_PASSED / ARB_occlusion_query, core since GL 1.5).
//
// Check A -- a query wrapping a fully visible full-screen quad reports PixelCount() > 0 (close
//   to the full backbuffer pixel count).
// Check B -- a query wrapping a quad drawn entirely BEHIND a nearer opaque full-screen quad
//   (real depth-test occlusion) reports PixelCount() == 0.
// Check C -- IsComplete() reports true once PixelCount() has already forced the result to be
//   available (glGetQueryObjectuiv(GL_QUERY_RESULT) blocks until ready).
// Check D -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;
}

class OpenGL2OcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
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

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        const bool runChecks = (frame_ == 1);

        // Check A: fully visible quad.
        dev.Clear(Color::Black);
        OcclusionQuery visibleQuery(dev);
        visibleQuery.Begin();
        DrawFullscreenColorQuad(dev, 0.0f, Color::Red);
        visibleQuery.End();
        if (runChecks)
        {
            const int visiblePixels = visibleQuery.getPixelCountProperty();
            Check(visiblePixels > 1000,
                  "visible full-screen quad reports PixelCount() > 1000: got=" + std::to_string(visiblePixels));
            Check(visibleQuery.getIsCompleteProperty(),
                  "IsComplete() is true once PixelCount() has forced the result to be available");
        }

        // Check B: fully occluded quad (nearer opaque quad drawn first, real depth test).
        dev.Clear(Color::Black);
        DrawFullscreenColorQuad(dev, -0.9f, Color::Blue); // nearer, opaque, covers everything
        OcclusionQuery occludedQuery(dev);
        occludedQuery.Begin();
        DrawFullscreenColorQuad(dev, 0.0f, Color::Red); // farther -- entirely behind the blue quad
        occludedQuery.End();
        if (runChecks)
        {
            const int occludedPixels = occludedQuery.getPixelCountProperty();
            Check(occludedPixels == 0,
                  "fully-occluded quad (behind a nearer opaque one) reports PixelCount() == 0: got=" + std::to_string(occludedPixels));
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, std::to_string(kTotalFrames) + " frames render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 4);
            result_ = (passCount_ == 4) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2OcclusionQueryTest()
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

    OpenGL2OcclusionQueryTest game;
    game.Run();
    return game.getResult();
}
