// SPDX-License-Identifier: MS-PL
// Task 880: GraphicsDevice.Viewport GPU wiring — Bgfx sub-region (split-screen) viewport.
//
// GraphicsDevice.Viewport previously had zero GPU renderer effect on Bgfx: the shared 3D/2D
// view's rect was always reset to the full window size by EnsureViewState() (called from
// Clear()/Present()/SubmitSprite()), regardless of what Viewport was set to. This test proves a
// genuine sub-region Viewport (e.g. the left half of the window, as in a split-screen layout) now
// actually clips/scales rendering to that region: a full-NDC-range quad (-1..1 on both axes)
// drawn under a custom Viewport should fill exactly that Viewport's own rectangle, not the whole
// window.
//
// Direct port of easygl_viewport_subregion_test.cpp's structure, adapted to this family's
// single-Draw()-call/retry-loop pattern (mirrors bgfx_basiceffect_fog_test.cpp): Bgfx's
// GetBackBufferData only reliably reflects the first read per rendered frame, so each scenario
// retries (<=20 attempts) until a genuinely post-draw frame is captured.
//
// Bgfx-only note (Task 364/896 finding): RasterizerState::CullNone is required, matching every
// other Bgfx pixel test in this family.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static Color readAt(GraphicsDevice& dev, int x, int y)
{
    const Rectangle reg(x, y, 1, 1);
    Color px(0, 0, 0, 0);
    dev.GetBackBufferData(&reg, &px, 0, 1);
    return px;
}

static bool isRed(const Color& c)
{
    return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
}
static bool isGreen(const Color& c)
{
    return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
}
static bool isBlack(const Color& c)
{
    return c.getRProperty() < 30 && c.getGProperty() < 30 && c.getBProperty() < 30;
}

static void drawQuad(GraphicsDevice& dev, BasicEffect& fx, const Color& col)
{
    const VertexPositionColor verts[6] = {
        { Vector3(-1.f,  1.f, 0.f), col },
        { Vector3( 1.f,  1.f, 0.f), col },
        { Vector3(-1.f, -1.f, 0.f), col },
        { Vector3(-1.f, -1.f, 0.f), col },
        { Vector3( 1.f,  1.f, 0.f), col },
        { Vector3( 1.f, -1.f, 0.f), col },
    };
    fx.Apply();
    dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
}

class BgfxViewportSubregionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: (%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: (%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

protected:
    // Bgfx's GetBackBufferData() only reliably reflects the *first* read call per rendered
    // frame (documented, pre-existing Bgfx test-harness quirk — see NEXT.md §5); every
    // multi-point Bgfx pixel test in this project therefore reads exactly ONE rectangle per
    // draw+retry pass. This helper does a full Clear+state+draw pass, then a single readback.
    void renderOnce(GraphicsDevice& dev, BasicEffect& fx, const Color& drawColor,
                    bool useSubregionViewport, int leftHalfW, int H)
    {
        dev.Clear(Color::Black);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        if (useSubregionViewport)
            dev.setViewportProperty(Viewport(0, 0, leftHalfW, H));
        drawQuad(dev, fx, drawColor);
    }

    // Retries the full render+single-read pass until the sample point shows the expected
    // freshly-drawn (non-black) color -- used for points the quad IS expected to reach.
    Color renderAndReadFresh(GraphicsDevice& dev, BasicEffect& fx, const Color& drawColor,
                             bool useSubregionViewport, int leftHalfW, int H,
                             int sampleX, int sampleY)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            renderOnce(dev, fx, drawColor, useSubregionViewport, leftHalfW, H);
            got = readAt(dev, sampleX, sampleY);
            if (!isBlack(got)) break;
        }
        return got;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        static const Color kRed(255, 0, 0, 255);
        static const Color kGreen(0, 255, 0, 255);

        const Viewport fullVp = dev.getViewportProperty();
        const int W = fullVp.getWidthProperty();
        const int H = fullVp.getHeightProperty();
        const int leftHalfW = W / 2;

        const int leftX  = leftHalfW / 2;
        const int rightX = leftHalfW + (W - leftHalfW) / 2;
        const int midY   = H / 2;

        // ── 1. Sub-region Viewport (left half): draw must only reach the left half ─────────
        // Two separate render+read passes (one per sample point) — see renderOnce's comment on
        // Bgfx's single-reliable-read-per-frame quirk. The left-half (non-black-expected) point
        // uses the retrying variant to warm up the pipeline; the right-half (black-expected)
        // point immediately follows with a single, non-retried pass now that rendering is warm.
        Color left = renderAndReadFresh(dev, fx, kRed, true, leftHalfW, H, leftX, midY);
        check(isRed(left), "sub-region Viewport: left-half (drawn)", left, "RED");

        renderOnce(dev, fx, kRed, true, leftHalfW, H);
        Color right = readAt(dev, rightX, midY);
        check(isBlack(right), "sub-region Viewport: right-half (not drawn)", right, "BLACK");

        // ── 2. Restore full Viewport: draw must reach both halves ──────────────────────────
        dev.setViewportProperty(fullVp);

        Color left2 = renderAndReadFresh(dev, fx, kGreen, false, leftHalfW, H, leftX, midY);
        check(isGreen(left2), "full Viewport restored: left-half", left2, "GREEN");

        renderOnce(dev, fx, kGreen, false, leftHalfW, H);
        Color right2 = readAt(dev, rightX, midY);
        check(isGreen(right2), "full Viewport restored: right-half", right2, "GREEN");

        // ── 3. Viewport getter/setter round-trip ────────────────────────────────────────────
        const Viewport testVp(10, 20, 300, 200);
        dev.setViewportProperty(testVp);
        const Viewport got = dev.getViewportProperty();
        const bool roundTripOk =
            got.getXProperty() == testVp.getXProperty() &&
            got.getYProperty() == testVp.getYProperty() &&
            got.getWidthProperty() == testVp.getWidthProperty() &&
            got.getHeightProperty() == testVp.getHeightProperty();
        std::printf("[%s] Viewport get/set round-trip preserves all fields\n",
                    roundTripOk ? "PASS" : "FAIL");
        if (roundTripOk) ++pass_; else ++fail_;

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxViewportSubregionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxViewportSubregionTest game;
    game.Run();
    return game.getResult();
}
