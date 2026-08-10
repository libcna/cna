// SPDX-License-Identifier: MS-PL
// Task 209: Scissor rectangle behavior — ScissorTestEnable false vs true.
//
// Checks:
//   1. ScissorTestEnable=false (default): Draw fills the full viewport; pixels
//      outside a notional scissor rect are NOT clipped.
//   2. ScissorTestEnable=true + SetScissorRectangle(right half): Draw fills
//      only the right half; left-half pixels remain from the prior clear.
//   3. ScissorTestEnable=false after it was true: Draw fills full viewport again.
//   4. ScissorRectangle getter round-trips the stored value.
//
// Method: pixel readback via GetBackBufferData at two sample points:
//   "left"  — horizontally centred in the left half of the backbuffer
//   "right" — horizontally centred in the right half of the backbuffer
//
// RasterizerState uses CullNone throughout so that winding order does not
// interfere with the scissor-specific checks.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cstdio>
#include <cmath>
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

// Full-screen NDC quad (two CW-ordered triangles matching the convention used
// by existing EasyGL tests: TL→BL→BR then TL→BR→TR).
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

class ScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

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
        const int W    = vp.getWidthProperty();
        const int H    = vp.getHeightProperty();
        const int midX = W / 2;
        const int midY = H / 2;
        const int leftX  = W / 4;       // centre of left half
        const int rightX = 3 * W / 4;   // centre of right half

        // Use CullNone so winding order does not affect scissor-test results.
        RasterizerState noScissor  = RasterizerState::CullNone;   // ScissorTestEnable=false
        RasterizerState withScissor = RasterizerState::CullNone;
        withScissor.setScissorTestEnableProperty(true);

        // Scissor rect = right half of the viewport.
        const Rectangle rightHalf(midX, 0, W - midX, H);

        // ── 1. ScissorTestEnable=false: full draw ─────────────────────────
        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(noScissor);
        dev.setScissorRectangleProperty(rightHalf);  // set rect — but test is OFF

        drawFullScreen(dev, Color::Red);

        check(colorNear(readPixel(dev, leftX,  midY), Color::Red),
              "ScissorOff: left pixel is red (not clipped)");
        check(colorNear(readPixel(dev, rightX, midY), Color::Red),
              "ScissorOff: right pixel is red (not clipped)");

        // ── 2. ScissorTestEnable=true: only right half rendered ──────────
        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(withScissor);
        // scissor rect already set to rightHalf

        drawFullScreen(dev, Color::Green);

        check(colorNear(readPixel(dev, leftX,  midY), Color::Black),
              "ScissorOn (right half): left pixel is black (clipped)");
        check(colorNear(readPixel(dev, rightX, midY), Color::Green),
              "ScissorOn (right half): right pixel is green (inside scissor)");

        // ── 3. ScissorTestEnable=false again: full draw restored ──────────
        dev.Clear(Color::Black);
        dev.setRasterizerStateProperty(noScissor);

        drawFullScreen(dev, Color::Blue);

        check(colorNear(readPixel(dev, leftX,  midY), Color::Blue),
              "ScissorOff again: left pixel is blue (not clipped)");
        check(colorNear(readPixel(dev, rightX, midY), Color::Blue),
              "ScissorOff again: right pixel is blue (not clipped)");

        // ── 4. ScissorRectangle getter round-trip ─────────────────────────
        const Rectangle testRect(10, 20, 300, 200);
        dev.setScissorRectangleProperty(testRect);
        const Rectangle got = dev.getScissorRectangleProperty();
        check(got.X == testRect.X && got.Y == testRect.Y &&
              got.Width == testRect.Width && got.Height == testRect.Height,
              "ScissorRectangle get/set round-trip preserves all fields");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    ScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ScissorTest game;
    game.Run();
    return game.getResult();
}
