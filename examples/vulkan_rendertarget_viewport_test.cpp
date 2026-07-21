// SPDX-License-Identifier: MS-PL
// REMED-GFX-062: Vulkan GraphicsDevice.Viewport must be honored for draws issued while a
// RenderTarget2D is bound — not hardcoded to the render target's full extent.
//
// Before the fix, VulkanGraphicsBackend::RecordCommandBuffer()'s render-target pass (Phase 1)
// hardcoded a full-target viewport (VkViewport{0,0,rtW,rtH,0,1}) at render-pass begin and never
// read the captured viewportSet_/viewportX_/Y_/W_/H_/minDepth/maxDepth state, so a custom
// sub-region Viewport set while a render target was bound was a silent no-op — even though the
// backbuffer pass (Phase 2) honored it correctly (Task 880).
//
// This is the exact same deferred-model root cause GFX-013 fixed for ScissorRectangle: the Vulkan
// backend defers every draw to a single Present()-time record, and SetRenderTarget resets the
// Viewport to the target's full size (GraphicsDevice::ResetViewportAndScissorForRenderTarget, the
// FNA-parity analog of Task 338's scissor reset). So the frame-global viewport at record time is
// never the sub-region each RT draw was issued under. The fix snapshots the viewport per draw/batch
// at enqueue time (PushPending3DDraw / SpriteBatch End()) and replays it per draw via
// vkCmdSetViewport. This test proves that end-to-end through the standard "render to RT, unbind,
// sample RT onto the backbuffer via SpriteBatch, read back" path (GetBackBufferData on Vulkan always
// reads the swapchain, never a bound RT).
//
// Geometry (asymmetric on every axis to catch origin/width/height/Y-mirror errors):
//   RenderTarget2D = 64 x 48            (asymmetric, and != the 64x64 backbuffer)
//   Viewport       = (12, 7, 28, 19)    => filled region x in [12,40), y in [7,26)
//
//   Frame 0 — custom sub-region Viewport: a full-NDC red quad (which would cover the whole target
//     if the viewport were full-size) must fill ONLY the viewport rectangle.
//       probe inside  (26,16) -> RED     probe left  ( 4,16) -> BLACK
//       probe right   (52,16) -> BLACK   probe above (26, 3) -> BLACK
//       probe below   (26,40) -> BLACK
//     Distinguishes: viewport ignored/full-target, wrong X origin, wrong Y origin (vertical mirror),
//     wrong width, wrong height.
//
//   Frame 1 — full Viewport control: the Viewport left at SetRenderTarget's full-RT reset value,
//     full-NDC red quad must fill the WHOLE target — every probe RED. Guards against the fix
//     over-clipping when no custom sub-region viewport is active.
//
//   Frame 2 — two viewports in one frame (state-timing, the crux): bind RT, set Viewport A, draw
//     RED, set Viewport B, draw GREEN, unbind, blit, read. Each draw must land in the viewport
//     active WHEN IT WAS ENQUEUED. A fix that merely reads the frame-global viewport once at record
//     time (which by Present() is the post-unbind full-backbuffer reset) would draw both quads
//     full-size and leave the whole target GREEN — so this frame FAILS unless per-draw capture is
//     used.
//       Viewport A = (2, 2, 20, 20)   RED    => x[2,22)  y[2,22)
//       Viewport B = (40,26, 20, 20)  GREEN  => x[40,60) y[26,46)
//       probe A-center (12,12) -> RED    probe B-center (50,36) -> GREEN
//       probe (50,12) -> BLACK           probe (12,36)  -> BLACK
//
// Backbuffer Viewport is exercised separately by Vulkan_Viewport_Subregion (Task 880, the Phase 4
// control) and the backbuffer/RT reset transition by Vulkan_RenderTarget_ViewportScissorReset.
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
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64;   // backbuffer width  (!= RT height, to catch dimension leaks)
static constexpr int kBBH = 64;   // backbuffer height
static constexpr int kRTW = 64;   // render target width
static constexpr int kRTH = 48;   // render target height (asymmetric vs width & backbuffer)

// Sub-region Viewport inside the RT: non-zero X and Y, not full-target, W != H.
static constexpr int kVpX = 12, kVpY = 7, kVpW = 28, kVpH = 19; // x[12,40) y[7,26)

class VulkanRenderTargetViewportTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_;
    int frame_ = 0;
    int pass_  = 0;
    int fail_  = 0;

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
        return c.getRProperty() < 40 && c.getGProperty() < 40 && c.getBProperty() < 40;
    }

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    // A full-RT-covering quad in NDC (Y-symmetric, so the GFX-011 shader Y-flip keeps it
    // full-coverage — it fills exactly whatever viewport rectangle is active).
    static void drawFullNdcQuad(GraphicsDevice& dev, const Color& col)
    {
        const VertexPositionColor q[6] = {
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
            { Vector3( 1.f,  1.f, 0.f), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    static void configureEffect(BasicEffect& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
    }

    // Blit rt_ 1:1 onto the backbuffer (viewport left at the full-backbuffer reset) so the RT's
    // viewport-clipped content can be read via GetBackBufferData.
    void blitRTToBackbuffer(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setRasterizerStateProperty(RasterizerState());
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(*rt_, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, kRTW, kRTH), Color::White);
        sb_->End();
    }

    Color readAt(GraphicsDevice& dev, int px, int py)
    {
        Color c(0, 0, 0, 0);
        Rectangle reg(px, py, 1, 1);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    // Frame 0/1: render a single full-NDC quad into the RT under an optional custom viewport.
    void renderSingle(GraphicsDevice& dev, bool customViewport)
    {
        BasicEffect fx(dev);
        configureEffect(fx);
        dev.SetRenderTarget(rt_.get());            // resets Viewport to (0,0,kRTW,kRTH)
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        if (customViewport)
            dev.setViewportProperty(Viewport(kVpX, kVpY, kVpW, kVpH));
        fx.Apply();
        drawFullNdcQuad(dev, Color(255, 0, 0, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); // resets Viewport to backbuffer
        blitRTToBackbuffer(dev);
    }

    // Frame 2: two viewports, two draws, one frame — proves per-draw capture (state timing).
    void renderTwoViewports(GraphicsDevice& dev)
    {
        BasicEffect fx(dev);
        configureEffect(fx);
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        dev.setViewportProperty(Viewport(2, 2, 20, 20));
        fx.Apply();
        drawFullNdcQuad(dev, Color(255, 0, 0, 255));    // RED into viewport A

        dev.setViewportProperty(Viewport(40, 26, 20, 20));
        fx.Apply();
        drawFullNdcQuad(dev, Color(0, 255, 0, 255));    // GREEN into viewport B

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        blitRTToBackbuffer(dev);
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

        if (frame_ == 0)
        {
            // Core: custom sub-region viewport — red survives only inside the viewport rectangle.
            Color in(0,0,0,0), lf(0,0,0,0), rt(0,0,0,0), ab(0,0,0,0), be(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderSingle(dev, /*customViewport=*/true);
                in = readAt(dev, 26, 16);   // inside viewport
                lf = readAt(dev,  4, 16);   // left of viewport
                rt = readAt(dev, 52, 16);   // right of viewport
                ab = readAt(dev, 26,  3);   // above viewport
                be = readAt(dev, 26, 40);   // below viewport
                if (isRed(in)) break;       // produced a non-blank frame
            }
            check(isRed(in),   "RT viewport: inside (26,16)", in, "RED");
            check(isBlack(lf), "RT viewport: left   ( 4,16)", lf, "BLACK");
            check(isBlack(rt), "RT viewport: right  (52,16)", rt, "BLACK");
            check(isBlack(ab), "RT viewport: above  (26, 3)", ab, "BLACK");
            check(isBlack(be), "RT viewport: below  (26,40)", be, "BLACK");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Control: full viewport (RT reset value) — the whole target is red, no clip.
            Color in(0,0,0,0), lf(0,0,0,0), rt(0,0,0,0), ab(0,0,0,0), be(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderSingle(dev, /*customViewport=*/false);
                in = readAt(dev, 26, 16);
                lf = readAt(dev,  4, 16);
                rt = readAt(dev, 52, 16);
                ab = readAt(dev, 26,  3);
                be = readAt(dev, 26, 40);
                if (isRed(in)) break;
            }
            check(isRed(in), "RT full viewport: inside (26,16)", in, "RED");
            check(isRed(lf), "RT full viewport: left   ( 4,16)", lf, "RED");
            check(isRed(rt), "RT full viewport: right  (52,16)", rt, "RED");
            check(isRed(ab), "RT full viewport: above  (26, 3)", ab, "RED");
            check(isRed(be), "RT full viewport: below  (26,40)", be, "RED");
            ++frame_;
            return;
        }

        if (frame_ == 2)
        {
            // State-timing: two viewports, two draws — each must land where it was enqueued.
            Color a(0,0,0,0), b(0,0,0,0), x0(0,0,0,0), x1(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderTwoViewports(dev);
                a  = readAt(dev, 12, 12);   // viewport A center
                b  = readAt(dev, 50, 36);   // viewport B center
                x0 = readAt(dev, 50, 12);   // outside both
                x1 = readAt(dev, 12, 36);   // outside both
                if (isRed(a) && isGreen(b)) break;
            }
            check(isRed(a),   "RT two-viewport: A-center (12,12)", a,  "RED");
            check(isGreen(b), "RT two-viewport: B-center (50,36)", b,  "GREEN");
            check(isBlack(x0),"RT two-viewport: gap      (50,12)", x0, "BLACK");
            check(isBlack(x1),"RT two-viewport: gap      (12,36)", x1, "BLACK");

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    VulkanRenderTargetViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRenderTargetViewportTest game;
    game.Run();
    return game.getResult();
}
