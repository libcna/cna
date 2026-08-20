// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: real context-loss recovery proof via IGraphicsRenderer::DebugSimulateContextLoss()
// -- no existing EasyGL reference test exercises this specific channel directly (only
// directx3_no3d_test.cpp/directx9_smoke_test.cpp do, for their own very different -- 2D-only no-op, and
// D3D9's two-phase DeviceLostException -- models), so this scene is authored fresh for OpenGL2's
// own atomic loss+restore model (matches EasyGLRenderer::DebugSimulateContextLoss's
// identical "destroys the SDL GL context and immediately recreates it" design, NOT D3D9's
// real two-phase Lost/Reset lifecycle -- DebugRestoreContext() simply delegates back to
// DebugSimulateContextLoss() here, same as EasyGL).
//
// Until this task, DebugSimulateContextLoss()/DebugRestoreContext()/SetContextRecoveryEnabled()
// fell back to no-op base-class defaults on OpenGL2 -- destroying and recreating the SDL GL
// context (which a real driver-level device loss would also do) left every previously-created
// VertexBuffer/IndexBuffer/Texture2D/RenderTarget2D/RenderTargetCube/OcclusionQuery holding a
// stale, meaningless GL handle from the destroyed context, with no mechanism to restore them.
//
// Checks:
//   A -- baseline: a VertexBuffer+IndexBuffer (red quad) and a Texture2D (green pixel, sampled
//        onto a second quad) both render correctly BEFORE any context loss.
//   B -- DebugSimulateContextLoss() does not throw.
//   C -- the SAME VertexBuffer/IndexBuffer objects (no SetData call after the loss) still render
//        the correct red quad -- proves VB/IB content genuinely survived via their CPU shadow.
//   D -- the SAME Texture2D object (no SetData call after the loss) still renders the correct
//        green pixel -- proves Texture2D content genuinely survived via ShareCpuPixels().
//   E -- a BRAND NEW VertexBuffer/IndexBuffer/Texture2D created AFTER the loss also render
//        correctly -- proves the renderer itself (shaders, buffer/texture creation) is fully
//        functional post-recovery, not just previously-existing resources.
//   F -- a RenderTarget2D created AFTER the loss can be rendered into and sampled back correctly
//        (RenderTarget2D content is NOT expected to survive a loss -- matches
//        EasyGLRenderTargetRenderer's own real, pre-existing limitation -- but the renderer must
//        still be able to create and use a fresh one afterward).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool ColorClose(const Color& got, const Color& want, int tol = 20)
    {
        return std::abs(static_cast<int>(got.getRProperty()) - static_cast<int>(want.getRProperty())) <= tol
            && std::abs(static_cast<int>(got.getGProperty()) - static_cast<int>(want.getGProperty())) <= tol
            && std::abs(static_cast<int>(got.getBProperty()) - static_cast<int>(want.getBProperty())) <= tol;
    }
}

class OpenGL2ContextLossRecoveryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<Texture2D> tex_;
    bool done_ = false;
    int pass_ = 0;
    int fail_ = 0;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    Color ReadPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    // Draws vb/ib (a red quad, VertexPositionColor) as a full-screen triangle list, then samples
    // the centre pixel.
    Color DrawQuadAndSample(VertexBuffer& vb, IndexBuffer& ib)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 10, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        dev.SetVertexBuffer(&vb);
        dev.setIndicesProperty(&ib);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        dev.SetVertexBuffer(nullptr);

        const auto& vp = dev.getViewportProperty();
        return ReadPixel(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2);
    }

    // Draws a full-screen textured quad sampling tex, then samples the centre pixel.
    Color DrawTexturedQuadAndSample(Texture2D& tex)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 10, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = false;
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const auto& vp = dev.getViewportProperty();
        return ReadPixel(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        const Color red(255, 0, 0, 255);
        const VertexPositionColor verts[4] = {
            { Vector3(-1.0f,  1.0f, 0.0f), red },
            { Vector3(-1.0f, -1.0f, 0.0f), red },
            { Vector3( 1.0f, -1.0f, 0.0f), red },
            { Vector3( 1.0f,  1.0f, 0.0f), red },
        };
        vb_ = std::make_unique<VertexBuffer>(dev, 4);
        vb_->SetData(verts, 0, 4);

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(dev, 6);
        ib_->SetData(indices, 6);

        const std::vector<std::uint8_t> greenPixel = { 0, 255, 0, 255 };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, greenPixel));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const Color kRed(255, 0, 0, 255);
        const Color kGreen(0, 255, 0, 255);

        // --- Check A: baseline, before any context loss ---
        Check("baseline VB/IB quad renders red", ColorClose(DrawQuadAndSample(*vb_, *ib_), kRed));
        Check("baseline Texture2D quad renders green", ColorClose(DrawTexturedQuadAndSample(*tex_), kGreen));

        // --- Check B: DebugSimulateContextLoss() itself doesn't throw ---
        bool threw = false;
        try { dev.GetRenderer().DebugSimulateContextLoss(); }
        catch (const std::exception& e)
        {
            threw = true;
            std::printf("[INFO] DebugSimulateContextLoss threw: %s\n", e.what());
        }
        Check("DebugSimulateContextLoss() does not throw", !threw);
        if (threw) { FinishAndExit(); return; }

        // --- Check C/D: pre-existing VB/IB/Texture2D content survives (CPU-shadow restore) ---
        Check("pre-existing VB/IB quad STILL renders red after context loss",
              ColorClose(DrawQuadAndSample(*vb_, *ib_), kRed));
        Check("pre-existing Texture2D STILL renders green after context loss",
              ColorClose(DrawTexturedQuadAndSample(*tex_), kGreen));

        // --- Check E: brand new resources created AFTER the loss also work ---
        const Color blue(0, 0, 255, 255);
        const VertexPositionColor newVerts[4] = {
            { Vector3(-1.0f,  1.0f, 0.0f), blue },
            { Vector3(-1.0f, -1.0f, 0.0f), blue },
            { Vector3( 1.0f, -1.0f, 0.0f), blue },
            { Vector3( 1.0f,  1.0f, 0.0f), blue },
        };
        VertexBuffer newVb(dev, 4);
        newVb.SetData(newVerts, 0, 4);
        const std::uint16_t newIndices[6] = { 0, 1, 2, 0, 2, 3 };
        IndexBuffer newIb(dev, 6);
        newIb.SetData(newIndices, 6);
        Check("brand-new VB/IB created after context loss renders blue",
              ColorClose(DrawQuadAndSample(newVb, newIb), blue));

        const std::vector<std::uint8_t> whitePixel = { 255, 255, 255, 255 };
        Texture2D newTex = Texture2D::CreateFromPixels(dev, 1, 1, whitePixel);
        Check("brand-new Texture2D created after context loss renders white",
              ColorClose(DrawTexturedQuadAndSample(newTex), Color(255, 255, 255, 255)));

        // --- Check F: a RenderTarget2D created after the loss can be rendered into and sampled ---
        RenderTarget2D rt(dev, 8, 8);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(200, 100, 0, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        BasicEffect blitFx(dev);
        blitFx.VertexColorEnabled = false;
        blitFx.setTextureEnabledProperty(true);
        blitFx.setTextureProperty(&rt);
        blitFx.setWorldProperty(Matrix::getIdentityProperty());
        blitFx.setViewProperty(Matrix::getIdentityProperty());
        blitFx.setProjectionProperty(Matrix::getIdentityProperty());
        blitFx.Apply();
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        const VertexPositionTexture rtQuad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, rtQuad, 0, 2);
        const auto& vp = dev.getViewportProperty();
        Check("RenderTarget2D created after context loss renders+samples correctly",
              ColorClose(ReadPixel(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2), Color(200, 100, 0, 255)));

        FinishAndExit();
    }

    void FinishAndExit()
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL2ContextLossRecoveryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2ContextLossRecoveryTest game;
    game.Run();
    return game.getResult();
}
