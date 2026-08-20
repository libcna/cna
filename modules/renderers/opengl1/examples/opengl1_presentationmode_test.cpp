// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: virtual-resolution/presentation-mode scaling (plans/plan_opengl1.md item 13,
// EasyGL parity).
//
// Before this, EffectiveWidth()/EffectiveHeight() (OpenGL1SpriteBatchRenderer::Begin()'s own glOrtho
// range) always returned the raw, never-recomputed virtualWidth_/virtualHeight_ from construction
// -- if a game resized its window, the physical viewport genuinely grew/shrank, but SpriteBatch's
// own internal logical-to-screen mapping stayed frozen at the original size, and
// TransformWindowToLogical/TransformLogicalToWindow (input coordinate mapping) were never
// overridden at all (always "window==logical", the shared interface's own no-op default).
//
// This renderer's fix is deliberately narrow: only presentationMode==FixedHeightDynamicWidth (the
// default) recomputes; every other mode keeps today's original (unrecomputed) behavior --
// Stretch/NativeBackBuffer are honestly already correct that way, and true Letterbox/Overscan
// (real bars/cropping) would need the actual glViewport sub-rectangle adjusted, not just this
// SpriteBatch-facing logical size -- a documented, intentional gap (plans/plan_opengl1.md item 13).
//
// Method: construct at 320x240 (4:3), draw a thin stripe at logical X=160 (half of 320) and
// confirm it lands at physical X=~160 (baseline, unscaled). Resize the real SDL window to 480x480
// (a genuinely aspect-changing resize -- 4:3 to 1:1) via SDL_SetWindowSize, then draw the same
// stripe at logical X=120: under the FIX, EffectiveWidth() recomputes to
// round(480*240/480)=240, so X=120 (its own logical half-width) lands at physical X=(120/240)*480
// = 240 (the real physical centre). Under the pre-existing bug (EffectiveWidth() stuck at the
// original 320), the SAME logical X=120 would instead land at physical X=(120/320)*480=180 -- a
// clearly distinguishable 60-pixel difference. TransformWindowToLogical/TransformLogicalToWindow
// are checked directly against the same post-resize scale (240/480=0.5 uniformly, matching
// Mouse::logical_to_window's own documented EasyGL behaviour for this exact presentation model).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kOrigW = 320, kOrigH = 240;
    constexpr int kNewW = 480, kNewH = 480;
    constexpr int kStripeW = 8;
}

class OpenGL1PresentationModeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;
    int  frame_ = 0;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    // Draws a thin vertical stripe at logical X (centered on the current window/backbuffer height)
    // and returns the physical-pixel X of its centre by scanning the read-back row for it.
    int DrawStripeAndFindPhysicalX(GraphicsDevice& dev, int logicalX, int physW, int physH)
    {
        dev.Clear(Color::Black);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(*whiteTex_, Rectangle(logicalX, 0, kStripeW, physH), Color::White);
        sb_->End();

        // GFX-165: GetBackBufferData validates its rectangle against PresentationParameters'
        // backbuffer size, which this test's own raw SDL_SetWindowSize deliberately does not
        // update -- XNA's backbuffer does not follow the OS window until a device reset. What
        // this test needs is the REAL framebuffer row, which is exactly the renderer's own
        // ReadBackbuffer (the same top-down row convention GetBackBufferData itself wraps).
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(physW) * 4, 0);
        dev.GetRenderer().ReadBackbuffer(0, physH / 2, physW, 1, pixels.data());
        for (int x = 0; x < physW; ++x)
            if (pixels[static_cast<std::size_t>(x) * 4] >= 200) return x;
        return -1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        whiteTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        Color white[1] = { Color::White };
        whiteTex_->SetData(white, 1);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 1)
        {
            // Baseline, before any resize: physical == logical == 320x240.
            const int x = DrawStripeAndFindPhysicalX(dev, kOrigW / 2, kOrigW, kOrigH);
            std::printf("baseline stripe at logical X=%d -> physical X=%d (expect ~%d)\n", kOrigW / 2, x, kOrigW / 2);
            Check(x >= kOrigW / 2 - 4 && x <= kOrigW / 2 + 4, "baseline (unresized): stripe lands where requested");
            return; // Let the resize below take effect before the next Draw() call.
        }

        if (frame_ == 2)
        {
            auto* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
            SDL_SetWindowSize(window, kNewW, kNewH);
            SDL_SyncWindow(window);
            SDL_PumpEvents();
            int gotW = 0, gotH = 0;
            SDL_GetWindowSizeInPixels(window, &gotW, &gotH);
            std::printf("after SDL_SetWindowSize(%d,%d): SDL_GetWindowSizeInPixels=(%d,%d)\n", kNewW, kNewH, gotW, gotH);
            Check(gotW == kNewW && gotH == kNewH, "SDL_SetWindowSize took effect (sandbox/window-manager sanity)");
            return; // Draw again next frame once the resize has genuinely landed.
        }

        if (frame_ == 3)
        {
            const int logicalHalfWidthAfterResize = 120; // round(480*240/480)=240 -> half is 120.
            const int x = DrawStripeAndFindPhysicalX(dev, logicalHalfWidthAfterResize, kNewW, kNewH);
            std::printf("post-resize stripe at logical X=%d -> physical X=%d (expect ~240, bug would give ~180)\n",
                        logicalHalfWidthAfterResize, x);
            Check(x >= 220 && x <= 260,
                  "EffectiveWidth() recomputed to 240 post-resize (FixedHeightDynamicWidth) -- "
                  "stripe lands at the real physical centre, not offset to ~180 (the pre-existing "
                  "stuck-at-320 bug's answer)");

            auto& renderer = dev.GetRenderer();
            float logX = -1.0f, logY = -1.0f;
            const bool wOk = renderer.TransformWindowToLogical(240.0f, 240.0f, logX, logY);
            std::printf("TransformWindowToLogical(240,240) -> ok=%d (%.1f,%.1f) (expect ok=true, ~120,120)\n",
                        (int)wOk, logX, logY);
            Check(wOk, "TransformWindowToLogical engages (no longer the shared no-op default)");
            Check(logX >= 110.0f && logX <= 130.0f && logY >= 110.0f && logY <= 130.0f,
                  "TransformWindowToLogical: physical window centre (240,240) -> logical (~120,120), "
                  "matching the same 0.5 scale EffectiveWidth() itself now uses");

            float winX = -1.0f, winY = -1.0f;
            const bool lOk = renderer.TransformLogicalToWindow(120.0f, 120.0f, winX, winY);
            std::printf("TransformLogicalToWindow(120,120) -> ok=%d (%.1f,%.1f) (expect ok=true, ~240,240)\n",
                        (int)lOk, winX, winY);
            Check(lOk, "TransformLogicalToWindow engages");
            Check(winX >= 230.0f && winX <= 250.0f && winY >= 230.0f && winY <= 250.0f,
                  "TransformLogicalToWindow: logical (120,120) -> physical window (~240,240), the "
                  "inverse of TransformWindowToLogical above");

            std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    OpenGL1PresentationModeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kOrigW);
        gdm_->setPreferredBackBufferHeightProperty(kOrigH);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1PresentationModeTest game;
    game.Run();
    return game.getResult();
}
