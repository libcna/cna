// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md: real chronological draw-order proof for the SDL_GPU graphics renderer
// (adversarial-review finding #4) -- the renderer used to always render all 7 3D shader families
// first and every SpriteBatch sprite last, regardless of the real order the game called
// Draw()/SpriteBatch.Draw() in. drawOrder_ + RenderQueuedDraws() now replay every queued draw in
// real chronological issue order instead.
//
// Both checks draw a full-screen opaque 3D quad and a full-screen opaque sprite into the SAME
// render target, with depth testing off -- so whichever one was issued LAST (in real API-call
// order) must win the final pixel, a clean unambiguous "last write wins" discriminator (no blend
// math to reason about).
//
// Check A -- sprite (red) drawn FIRST, 3D quad (green) drawn SECOND: correct chronological order
//   means the 3D quad, drawn later, overwrites the sprite -- the readback must be GREEN.
// Check B -- 3D quad (green) drawn FIRST, sprite (red) drawn SECOND: correct order means the
//   sprite, drawn later, overwrites the 3D quad -- the readback must be RED.
//
// Together these two checks are the real discriminator: the OLD fixed-family-order bug always
// rendered every sprite after every 3D draw regardless of real call order, so it would have read
// back RED for BOTH checks (sprites always "win"), not GREEN-then-RED.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 10)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }
}

class SdlGpuDrawOrderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rtSpriteThen3D_;
    std::unique_ptr<RenderTarget2D> rt3DThenSprite_;
    std::unique_ptr<VertexBuffer> quadVb_;
    std::unique_ptr<Texture2D> redTex_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void DrawSprite(GraphicsDevice& dev)
    {
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(*redTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
    }

    void Draw3DQuad(GraphicsDevice& dev)
    {
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetDepthTestEnabled(false);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(quadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        rtSpriteThen3D_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rt3DThenSprite_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        const VertexPositionColor quadVerts[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Green }, { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
            { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
        };
        quadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadVb_->SetData(quadVerts, 0, 6);

        const std::vector<std::uint8_t> redPixels = {255, 0, 0, 255};
        redTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, redPixels));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A: sprite (red) issued first, 3D quad (green) issued second -- real order means
        // the 3D quad, drawn later, must win.
        dev.SetRenderTarget(rtSpriteThen3D_.get());
        dev.Clear(Color::Black);
        DrawSprite(dev);
        Draw3DQuad(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // Check B: 3D quad (green) issued first, sprite (red) issued second -- real order means
        // the sprite, drawn later, must win.
        dev.SetRenderTarget(rt3DThenSprite_.get());
        dev.Clear(Color::Black);
        Draw3DQuad(dev);
        DrawSprite(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color gotA(0, 0, 0, 0), gotB(0, 0, 0, 0);
        rtSpriteThen3D_->GetData(0, &centre, &gotA, 0, 1);
        rt3DThenSprite_->GetData(0, &centre, &gotB, 0, 1);

        Check(Matches(gotA, Color::Green),
              "Check A: sprite-then-3D real order -> 3D (issued later) wins, got=(" +
              std::to_string(gotA.getRProperty()) + "," + std::to_string(gotA.getGProperty()) + "," +
              std::to_string(gotA.getBProperty()) + ")");
        Check(Matches(gotB, Color::Red),
              "Check B: 3D-then-sprite real order -> sprite (issued later) wins, got=(" +
              std::to_string(gotB.getRProperty()) + "," + std::to_string(gotB.getGProperty()) + "," +
              std::to_string(gotB.getBProperty()) + ")");

        std::printf("=== %d/2 PASS ===\n", passCount_);
        result_ = (passCount_ == 2) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuDrawOrderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuDrawOrderTest game;
    game.Run();
    return game.getResult();
}
