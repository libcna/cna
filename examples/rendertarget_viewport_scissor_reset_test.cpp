// SPDX-License-Identifier: MS-PL
// Task 338: verify SetRenderTarget(nullptr)/SetRenderTargets({}) resets Viewport/ScissorRectangle
// to the backbuffer, and that binding a render target resets them to the new target's size.
//
// FNA's GraphicsDevice.SetRenderTargets ALWAYS resets Viewport and ScissorRectangle to
// (0, 0, newWidth, newHeight) — the new render target's size when binding, or the backbuffer's
// size when unbinding (see GraphicsDevice.cs: "Set the viewport/scissor to the size of the
// backbuffer" / "...of the first render target"). CNA's SetRenderTarget/SetRenderTargets never
// touched Viewport or ScissorRectangle at all before this task (confirmed by code reading) — a
// game's previously-set Viewport/ScissorRectangle would incorrectly survive a render target
// switch, unlike real XNA/FNA.
//
// This test proves both the property-level fix AND a real GPU-level effect (ScissorRectangle is
// wired to the backend's actual scissor test, unlike Viewport which has no backend wiring yet on
// any backend - see the known-limitations note in NEXT.md):
//   1. Enable ScissorTestEnable=true with the scissor rect set to the RIGHT HALF of the
//      backbuffer; clear black, draw full-screen green - confirms the scissor genuinely clips
//      (left stays black, right is green), matching Task 209's own established methodology.
//   2. Bind a small RenderTarget2D, then immediately unbind it (no draws in between - the point
//      is the bind/unbind cycle itself, not what happens inside).
//   3. Property check: ScissorRectangle now equals (0,0,backbufferW,backbufferH), not the stale
//      right-half rect and not the RT's own size.
//   4. Clear black, draw full-screen green again (RasterizerState still has ScissorTestEnable=true
//      from step 1) - if the scissor rect was genuinely reset to the full backbuffer, BOTH halves
//      must now be green (no clipping); if the stale right-half rect survived the RT switch (the
//      pre-Task-338 bug), the left half would incorrectly stay black.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static Color readPixel(GraphicsDevice& dev, int x, int y)
{
    const Rectangle reg(x, y, 1, 1);
    Color px(0, 0, 0, 0);
    dev.GetBackBufferData(&reg, &px, 0, 1);
    return px;
}

static bool colorNear(Color a, Color b, int tol = 4)
{
    return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
           std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
           std::abs(a.getBProperty() - b.getBProperty()) <= tol;
}

static const Vector3 kTL(-1.0f,  1.0f, 0.0f);
static const Vector3 kBL(-1.0f, -1.0f, 0.0f);
static const Vector3 kBR( 1.0f, -1.0f, 0.0f);
static const Vector3 kTR( 1.0f,  1.0f, 0.0f);

static void drawFullScreen(GraphicsDevice& dev, Color col)
{
    BasicEffect fx(dev);
    fx.VertexColorEnabled = true;
    fx.Apply();

    const VertexPositionColor q[6] = {
        { kTL, col }, { kBL, col }, { kBR, col },
        { kTL, col }, { kBR, col }, { kTR, col },
    };
    dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
}

class RenderTargetViewportScissorResetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_   = 0;
    int  fail_   = 0;
    bool done_   = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();
        const int midX   = W / 2;
        const int midY   = H / 2;
        const int leftX  = W / 4;
        const int rightX = 3 * W / 4;

        RasterizerState withScissor = RasterizerState::CullNone;
        withScissor.setScissorTestEnableProperty(true);
        dev.setRasterizerStateProperty(withScissor);

        const Rectangle rightHalf(midX, 0, W - midX, H);
        dev.setScissorRectangleProperty(rightHalf);

        // --- 1. Sanity: scissor genuinely clips to the right half ---
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Green);
        check(colorNear(readPixel(dev, leftX,  midY), Color::Black),
              "Sanity: left pixel black (scissor clips before RT switch)");
        check(colorNear(readPixel(dev, rightX, midY), Color::Green),
              "Sanity: right pixel green (inside scissor before RT switch)");

        // --- 2. Bind then immediately unbind a small RenderTarget2D ---
        RenderTarget2D rt(dev, 16, 16);
        dev.SetRenderTarget(&rt);
        const Rectangle scissorWhileBound = dev.getScissorRectangleProperty();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- 3. Property checks: reset to RT size while bound, backbuffer size after unbind ---
        check(scissorWhileBound.X == 0 && scissorWhileBound.Y == 0 &&
              scissorWhileBound.Width == 16 && scissorWhileBound.Height == 16,
              "ScissorRectangle reset to (0,0,16,16) while RenderTarget2D is bound");

        const Rectangle scissorAfterUnbind = dev.getScissorRectangleProperty();
        check(scissorAfterUnbind.X == 0 && scissorAfterUnbind.Y == 0 &&
              scissorAfterUnbind.Width == W && scissorAfterUnbind.Height == H,
              "ScissorRectangle reset to (0,0,backbufferW,backbufferH) after unbinding");

        // --- 4. Real GPU-level proof: full-screen draw is no longer clipped to the stale rect ---
        dev.Clear(Color::Black);
        drawFullScreen(dev, Color::Green);
        check(colorNear(readPixel(dev, leftX,  midY), Color::Green),
              "After RT bind/unbind: left pixel green (stale right-half scissor did NOT survive)");
        check(colorNear(readPixel(dev, rightX, midY), Color::Green),
              "After RT bind/unbind: right pixel green (still inside the now-full-size scissor)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    RenderTargetViewportScissorResetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    RenderTargetViewportScissorResetTest game;
    game.Run();
    return game.getResult();
}
