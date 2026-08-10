// SPDX-License-Identifier: MS-PL
// REMED-GFX-064: SdlGpu GraphicsDevice.Viewport must be honored for GPU rasterization — the
// SdlGpu renderer never overrode the no-op base IGraphicsRenderer::SetViewport and never called
// SDL_SetGPUViewport, so a custom sub-region Viewport was a total no-op (backbuffer AND render
// target).
//
// SdlGpu is a DEFERRED renderer: every draw is queued (drawOrder_) and replayed at Present-time
// inside a per-target render pass. GraphicsDevice::SetRenderTarget resets the Viewport to the
// target's full size on both bind and unbind (ResetViewportAndScissorForRenderTarget), so by the
// time the queued draws are replayed the frame-global Viewport is the post-unbind full-backbuffer
// value — never the sub-region an RT draw was issued under. This is the exact deferred-model shape
// GFX-062 solved for Vulkan: the fix captures the Viewport PER DRAW at enqueue time (into each
// QueuedDrawRef) and applies SDL_SetGPUViewport per draw at replay. A per-pass read of the live
// Viewport (mirroring the existing per-pass scissor) would be wrong here — see Frame 2.
//
// Readback: SdlGpu cannot download the swapchain (SDLGPU-39), so — like every other SdlGpu pixel
// test — this renders into a RenderTarget2D and reads it back with RenderTarget2D::GetData(), which
// reads the RT's own colorTexture_. The RT is unbound BEFORE GetData() (matching the real usage the
// existing sdlgpu_rendertarget2d_test exercises), which is precisely what makes the per-draw capture
// necessary rather than a per-pass live-state read.
//
// Geometry (asymmetric on every axis to catch origin/width/height/Y-mirror errors):
//   RenderTarget2D = 80 x 60            (asymmetric)
//   Viewport       = (13, 9, 31, 23)    => filled region x in [13,44), y in [9,32)
//
//   Frame 0 — custom sub-region Viewport: a full-NDC red quad (which would cover the whole target
//     if the viewport were full-size) must fill ONLY the viewport rectangle.
//       inside (28,20) -> RED   left (5,20) -> BLACK   right (60,20) -> BLACK
//       above  (28,3)  -> BLACK below (28,50) -> BLACK
//     Distinguishes: viewport ignored/full-target, wrong X origin, wrong Y origin (vertical
//     mirror), wrong width, wrong height.
//
//   Frame 1 — full Viewport control: Viewport left at SetRenderTarget's full-RT reset value; the
//     full-NDC red quad must fill the WHOLE target — every probe RED. Guards against over-clipping
//     when no custom sub-region viewport is active.
//
//   Frame 2 — two viewports in one frame (the per-draw-capture crux): bind RT, set Viewport A, draw
//     RED, set Viewport B, draw GREEN, unbind, read. Each draw must land in the viewport active WHEN
//     IT WAS ENQUEUED. A per-pass read of the live viewport (which, after unbind, is the full
//     backbuffer reset) would draw both quads full-size and leave the whole target GREEN.
//       Viewport A = (4, 4, 22, 22)   RED    => x[4,26)  y[4,26)
//       Viewport B = (48,32, 22, 22)  GREEN  => x[48,70) y[32,54)
//       A-center (15,15) -> RED   B-center (59,43) -> GREEN
//       (59,15) -> BLACK          (15,43) -> BLACK
//
// The Viewport∩ScissorRectangle INTERSECTION is not asserted in THIS test (it exercises only the
// per-draw Viewport wiring). When this test was written SdlGpu applied the scissor once per pass from
// the live state at Present time, so — because SdlGpu can only read a RenderTarget2D back via GetData()
// after the target is UNBOUND, and SetRenderTarget(nullptr) resets ScissorRectangle to the full
// backbuffer — the RT pass replayed with the scissor no longer clipping. That per-pass-scissor
// deferred-reset limitation is now FIXED (REMED-GFX-068 made the scissor per-draw, like the viewport),
// and the Viewport∩Scissor intersection is asserted end-to-end in sdlgpu_rendertarget_scissor_test
// Frame 3. (The independence was already proven on Vulkan, where both states are per-draw —
// vulkan_rendertarget_viewport_test, GFX-062 Frame 3.)
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 96;   // backbuffer width  (!= RT, to catch dimension leaks)
static constexpr int kBBH = 72;   // backbuffer height
static constexpr int kRTW = 80;   // render target width  (asymmetric vs height & backbuffer)
static constexpr int kRTH = 60;   // render target height

// Sub-region Viewport inside the RT: non-zero X and Y, not full-target, W != H.
static constexpr int kVpX = 13, kVpY = 9, kVpW = 31, kVpH = 23; // x[13,44) y[9,32)

class SdlGpuRenderTargetViewportTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D>        rt_;
    std::unique_ptr<VertexBuffer>          redQuadVb_;
    std::unique_ptr<VertexBuffer>          greenQuadVb_;
    std::unique_ptr<BasicEffect>           fx_;
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

    static std::unique_ptr<VertexBuffer> makeFullNdcQuad(GraphicsDevice& dev, const Color& col)
    {
        // A full-NDC-covering quad (fills exactly whatever viewport rectangle is active).
        const VertexPositionColor q[6] = {
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
            { Vector3( 1.f,  1.f, 0.f), col },
        };
        auto vb = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(),
                                                 6, BufferUsage::None);
        vb->SetData(q, 0, 6);
        return vb;
    }

    void drawQuad(GraphicsDevice& dev, VertexBuffer& vb)
    {
        fx_->Apply();   // fx_ (identity MVP, VertexColorEnabled) must outlive the draw below
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    Color readAt(int px, int py)
    {
        Color c(0, 0, 0, 0);
        const Rectangle reg(px, py, 1, 1);
        rt_->GetData(0, &reg, &c, 0, 1);
        return c;
    }

    // Frame 0/1: render a single full-NDC quad into the RT under an optional custom viewport.
    void renderSingle(GraphicsDevice& dev, bool customViewport)
    {
        dev.SetRenderTarget(rt_.get());            // resets Viewport to (0,0,kRTW,kRTH)
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        if (customViewport)
            dev.setViewportProperty(Viewport(kVpX, kVpY, kVpW, kVpH));
        drawQuad(dev, *redQuadVb_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); // resets Viewport to backbuffer
    }

    // Frame 2: two viewports, two draws, one frame — proves per-draw capture (state timing).
    void renderTwoViewports(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        dev.setViewportProperty(Viewport(4, 4, 22, 22));
        drawQuad(dev, *redQuadVb_);                 // RED into viewport A

        dev.setViewportProperty(Viewport(48, 32, 22, 22));
        drawQuad(dev, *greenQuadVb_);               // GREEN into viewport B

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTW, kRTH);
        redQuadVb_ = makeFullNdcQuad(dev, Color(255, 0, 0, 255));
        greenQuadVb_ = makeFullNdcQuad(dev, Color(0, 255, 0, 255));
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->VertexColorEnabled = true;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 0)
        {
            renderSingle(dev, /*customViewport=*/true);
            const Color in = readAt(28, 20), lf = readAt(5, 20), rt = readAt(60, 20);
            const Color ab = readAt(28, 3), be = readAt(28, 50);
            check(isRed(in),   "RT viewport: inside (28,20)", in, "RED");
            check(isBlack(lf), "RT viewport: left   ( 5,20)", lf, "BLACK");
            check(isBlack(rt), "RT viewport: right  (60,20)", rt, "BLACK");
            check(isBlack(ab), "RT viewport: above  (28, 3)", ab, "BLACK");
            check(isBlack(be), "RT viewport: below  (28,50)", be, "BLACK");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            renderSingle(dev, /*customViewport=*/false);
            const Color in = readAt(28, 20), lf = readAt(5, 20), rt = readAt(60, 20);
            const Color ab = readAt(28, 3), be = readAt(28, 50);
            check(isRed(in), "RT full viewport: inside (28,20)", in, "RED");
            check(isRed(lf), "RT full viewport: left   ( 5,20)", lf, "RED");
            check(isRed(rt), "RT full viewport: right  (60,20)", rt, "RED");
            check(isRed(ab), "RT full viewport: above  (28, 3)", ab, "RED");
            check(isRed(be), "RT full viewport: below  (28,50)", be, "RED");
            ++frame_;
            return;
        }

        if (frame_ == 2)
        {
            renderTwoViewports(dev);
            const Color a = readAt(15, 15), b = readAt(59, 43);
            const Color x0 = readAt(59, 15), x1 = readAt(15, 43);
            check(isRed(a),    "RT two-viewport: A-center (15,15)", a,  "RED");
            check(isGreen(b),  "RT two-viewport: B-center (59,43)", b,  "GREEN");
            check(isBlack(x0), "RT two-viewport: gap      (59,15)", x0, "BLACK");
            check(isBlack(x1), "RT two-viewport: gap      (15,43)", x1, "BLACK");

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    SdlGpuRenderTargetViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SdlGpuRenderTargetViewportTest game;
    game.Run();
    return game.getResult();
}
