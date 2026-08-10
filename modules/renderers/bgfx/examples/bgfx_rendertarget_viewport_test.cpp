// SPDX-License-Identifier: MS-PL
// REMED-GFX-063: Bgfx GraphicsDevice.Viewport must be honored for draws issued while a
// RenderTarget2D is bound — not hardcoded to the render target's full extent.
//
// Before the fix, BgfxRenderer::ApplyViewportOverride() gated the custom viewport on
// currentViewId_ == 0 (the backbuffer view), so a custom sub-region Viewport set while a render
// target was bound was ignored: RT views stayed at their BindAsRenderTarget()/EnsureViewState()
// full-RT-size setViewRect(). The backbuffer pass honored it (Task 880); render targets did not.
//
// Bgfx state model (why the fix differs from Vulkan GFX-062): bgfx view state (setViewRect) is
// PER-VIEW, evaluated once per view per bgfx::frame() — the last setViewRect(viewId,...) before the
// frame advance applies to every draw submitted to that view. It is NOT per-draw. Each
// RenderTarget2D/Cube/MRT owns a distinct bgfx view id. So honoring a custom Viewport for the
// common one-Viewport-per-RT-pass case = setViewRect(rtViewId, vp) via ApplyViewportOverride for
// RT views too. (Two DIFFERENT viewports on one RT view within a single frame cannot be represented
// by bgfx's per-view rect — a pre-existing global limitation shared with the backbuffer, tracked as
// REMED-GFX-065; not exercised here.)
//
// Methodology (the proven Bgfx RT-sample path from bgfx_rendertarget2d_msaa_test.cpp): render a
// full-NDC quad into a 64x48 RenderTarget2D under a custom Viewport, unbind, blit the RT 1:1 onto
// the backbuffer via SpriteBatch, then read the WHOLE backbuffer region in ONE GetBackBufferData
// call (Bgfx only reliably reflects the first read per rendered frame) and probe individual pixels.
//
// The 64x48 RT is deliberately non-square and != the 64x64 backbuffer, so a full-RT-hardcoded
// viewport is clearly distinguishable from the custom sub-region. (Some other RT sizes read back
// Y-mirrored through the SpriteBatch-RT-sample path — a separate, pre-existing, dimension-dependent
// Bgfx RT-sample/readback quirk, REMED-GFX-067, unrelated to this Viewport wiring; 64x48 reads
// upright, matching every existing Bgfx RT pixel test's methodology.)
//
// Geometry (asymmetric on every axis to catch origin/width/height/Y-mirror errors):
//   RenderTarget2D = 64 x 48        Viewport = (11, 7, 29, 18)  => filled x in [11,40), y in [7,25)
//
//   A — custom sub-region Viewport: full-NDC red quad fills ONLY the viewport rectangle.
//       inside (25,16) RED; left (4,16) BLACK; right (52,16) BLACK; above (25,3) BLACK;
//       below (25,38) BLACK. Distinguishes viewport ignored/full-target, wrong X/Y origin,
//       vertical mirror, wrong W/H.
//   B — full Viewport control (RT reset value): whole target red — guards against over-clipping.
//
// Custom Viewport + ScissorRectangle interaction is NOT tested here: Bgfx's per-draw scissor does
// not clip on FBO-bound (render-target) views at all — a separate pre-existing defect (REMED-GFX-066),
// independent of this Viewport wiring. Backbuffer Viewport is Bgfx_Viewport_Subregion (Task 880).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64;   // backbuffer width
static constexpr int kBBH = 64;   // backbuffer height
static constexpr int kRTW = 64;   // render target width
static constexpr int kRTH = 48;   // render target height (asymmetric, upright readback)

static constexpr int kVpX = 11, kVpY = 7, kVpW = 29, kVpH = 18; // x[11,40) y[7,25)

class BgfxRenderTargetViewportTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_;
    int pass_ = 0;
    int fail_ = 0;

    static bool isRed(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
    }
    static bool isBlack(const Color& c)
    {
        return c.getRProperty() < 40 && c.getGProperty() < 40 && c.getBProperty() < 40;
    }

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    static void drawFullNdcRedQuad(GraphicsDevice& dev)
    {
        static const Color red(255, 0, 0, 255);
        const VertexPositionColor q[6] = {
            { Vector3(-1.f,  1.f, 0.f), red },
            { Vector3(-1.f, -1.f, 0.f), red },
            { Vector3( 1.f, -1.f, 0.f), red },
            { Vector3(-1.f,  1.f, 0.f), red },
            { Vector3( 1.f, -1.f, 0.f), red },
            { Vector3( 1.f,  1.f, 0.f), red },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    // Render the red quad into rt_ under an optional custom viewport, unbind, blit rt_ 1:1 onto the
    // backbuffer, then read the WHOLE backbuffer region in one call (Bgfx first-read quirk). Retries
    // until the inside pixel is fresh (non-black).
    std::vector<Color> renderRTAndReadFrame(GraphicsDevice& dev, bool customViewport)
    {
        std::vector<Color> buf(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        SamplerState point = SamplerState::PointClamp;

        for (int i = 0; i < 20; ++i)
        {
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;

            // --- render into the RT ---
            dev.SetRenderTarget(rt_.get());               // resets Viewport to full RT
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            if (customViewport)
                dev.setViewportProperty(Viewport(kVpX, kVpY, kVpW, kVpH));
            fx.Apply();
            drawFullNdcRedQuad(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); // resets to backbuffer

            // --- blit the RT 1:1 onto the backbuffer, unclipped ---
            dev.Clear(Color(0, 0, 0, 255));
            dev.setRasterizerStateProperty(RasterizerState());
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*rt_, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, kRTW, kRTH), Color::White);
            sb_->End();

            dev.GetBackBufferData(&whole, buf.data(), 0, kBBW * kBBH);
            if (isRed(buf[static_cast<std::size_t>(16) * kBBW + 25])) break; // inside = fresh frame
        }
        return buf;
    }

    static Color at(const std::vector<Color>& buf, int x, int y)
    {
        return buf[static_cast<std::size_t>(y) * kBBW + x];
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTW, kRTH);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // ── A: custom sub-region viewport — red survives only inside the viewport rectangle ──
        {
            std::vector<Color> b = renderRTAndReadFrame(dev, /*customViewport=*/true);
            check(isRed  (at(b, 25, 16)), "RT viewport: inside (25,16)", at(b, 25, 16), "RED");
            check(isBlack(at(b,  4, 16)), "RT viewport: left   ( 4,16)", at(b,  4, 16), "BLACK");
            check(isBlack(at(b, 52, 16)), "RT viewport: right  (52,16)", at(b, 52, 16), "BLACK");
            check(isBlack(at(b, 25,  3)), "RT viewport: above  (25, 3)", at(b, 25,  3), "BLACK");
            check(isBlack(at(b, 25, 38)), "RT viewport: below  (25,38)", at(b, 25, 38), "BLACK");
        }

        // ── B: full viewport (RT reset value) — the whole target is red, no clip ──
        {
            std::vector<Color> b = renderRTAndReadFrame(dev, /*customViewport=*/false);
            check(isRed(at(b, 25, 16)), "RT full viewport: inside (25,16)", at(b, 25, 16), "RED");
            check(isRed(at(b,  4, 16)), "RT full viewport: left   ( 4,16)", at(b,  4, 16), "RED");
            check(isRed(at(b, 52, 16)), "RT full viewport: right  (52,16)", at(b, 52, 16), "RED");
            check(isRed(at(b, 25,  3)), "RT full viewport: above  (25, 3)", at(b, 25,  3), "RED");
            check(isRed(at(b, 25, 38)), "RT full viewport: below  (25,38)", at(b, 25, 38), "RED");
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxRenderTargetViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxRenderTargetViewportTest game;
    game.Run();
    return game.getResult();
}
