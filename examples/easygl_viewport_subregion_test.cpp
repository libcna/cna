// SPDX-License-Identifier: MS-PL
// Task 880: GraphicsDevice.Viewport GPU wiring — sub-region (split-screen) viewport.
//
// GraphicsDevice.Viewport previously had zero GPU backend effect: every backend hardcoded its
// actual viewport to the full render-target/window size regardless of what Viewport was set to.
// This test proves a genuine sub-region Viewport (e.g. the left half of the window, as in a
// split-screen layout) now actually clips/scales rendering to that region: a full-NDC-range quad
// (-1..1 on both axes) drawn under a custom Viewport should fill exactly that Viewport's own
// rectangle, not the whole window.
//
// Method: Clear() the WHOLE window black first (Viewport doesn't gate Clear(), matching real
// GPU/FNA semantics — glClear ignores the viewport, only the scissor rect if enabled). Then set
// Viewport to the LEFT HALF of the window and draw a full-NDC red quad. If Viewport has real GPU
// effect, the right half (outside the custom Viewport) stays the earlier black clear; the left
// half becomes red. Pre-fix, since the backend always used the full window regardless of
// Viewport, BOTH halves would show red.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
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

class ViewportSubregionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;
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
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Viewport fullVp = dev.getViewportProperty();
        const int W = fullVp.getWidthProperty();
        const int H = fullVp.getHeightProperty();
        const int leftHalfW = W / 2;

        // Sample points: well inside the left half (custom Viewport) and well inside the right
        // half (outside it).
        const int leftX  = leftHalfW / 2;
        const int rightX = leftHalfW + (W - leftHalfW) / 2;
        const int midY   = H / 2;

        // ── 1. Full-window clear, then draw under a LEFT-HALF sub-region Viewport ──────────
        dev.Clear(Color::Black);
        dev.setViewportProperty(Viewport(0, 0, leftHalfW, H));
        drawFullScreen(dev, Color::Red);

        check(colorNear(readPixel(dev, leftX, midY), Color::Red),
              "sub-region Viewport (left half): left-half pixel is red (drawn)");
        check(colorNear(readPixel(dev, rightX, midY), Color::Black),
              "sub-region Viewport (left half): right-half pixel stays black (not drawn outside Viewport)");

        // ── 2. Restore full-window Viewport: draw fills the whole window again ─────────────
        dev.Clear(Color::Black);
        dev.setViewportProperty(fullVp);
        drawFullScreen(dev, Color::Green);

        check(colorNear(readPixel(dev, leftX, midY), Color::Green),
              "full Viewport restored: left-half pixel is green");
        check(colorNear(readPixel(dev, rightX, midY), Color::Green),
              "full Viewport restored: right-half pixel is green");

        // ── 3. Viewport getter round-trip ───────────────────────────────────────────────────
        const Viewport testVp(10, 20, 300, 200);
        dev.setViewportProperty(testVp);
        const Viewport got = dev.getViewportProperty();
        check(got.getXProperty() == testVp.getXProperty() &&
              got.getYProperty() == testVp.getYProperty() &&
              got.getWidthProperty() == testVp.getWidthProperty() &&
              got.getHeightProperty() == testVp.getHeightProperty(),
              "Viewport get/set round-trip preserves all fields");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    ViewportSubregionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ViewportSubregionTest game;
    game.Run();
    return game.getResult();
}
