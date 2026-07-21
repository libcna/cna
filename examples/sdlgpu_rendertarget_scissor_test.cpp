// SPDX-License-Identifier: MS-PL
// REMED-GFX-068: SdlGpu GraphicsDevice.ScissorRectangle must clip draws issued while a
// RenderTarget2D is bound — even though the draws are replayed at Present-time.
//
// SdlGpu is a DEFERRED backend: every draw is queued (drawOrder_) and replayed at Present-time
// inside a per-target render pass. Before this fix the scissor was applied ONCE PER PASS
// (ApplyScissorForPass) from the live member state at Present() time, unlike the per-draw viewport
// GFX-064 wired. GraphicsDevice::SetRenderTarget resets ScissorRectangle to the target's full size
// on both bind and unbind (ResetViewportAndScissorForRenderTarget), so by the time the queued RT
// draws are replayed the frame-global ScissorRectangle is the post-unbind full-BACKBUFFER value
// (larger than the RT) — never the sub-rect an RT draw was issued under. The scissor therefore no
// longer clipped. This is the exact deferred-model shape GFX-013 solved for Vulkan scissor: the fix
// captures the ScissorRectangle + RasterizerState.ScissorTestEnable PER DRAW at enqueue time (into
// each QueuedDrawRef) and applies SDL_SetGPUScissor per draw at replay (ApplyScissorForRef).
//
// Readback: SdlGpu cannot download the swapchain (SDLGPU-39), so — like every other SdlGpu pixel
// test — this renders into a RenderTarget2D and reads it back with RenderTarget2D::GetData(), which
// reads the RT's own colorTexture_. The RT is unbound BEFORE GetData() (matching the real usage the
// existing sdlgpu_rendertarget2d_test exercises), which is precisely what makes the per-draw capture
// necessary rather than a per-pass live-state read.
//
// Geometry (asymmetric on every axis to catch origin/width/height/Y-mirror errors):
//   RenderTarget2D   = 64 x 48           (asymmetric, and != the 96x72 backbuffer)
//   ScissorRectangle = (12, 8, 20, 13)   => clipped region x in [12,32), y in [8,21)
//
//   Frame 0 — ScissorTestEnable=true: a full-NDC red quad must survive ONLY inside the scissor.
//       inside (20,14) -> RED    left ( 5,14) -> BLACK   right (40,14) -> BLACK
//       above  (20, 3) -> BLACK   below (20,30) -> BLACK
//     Distinguishes: scissor ignored/full-target, wrong X origin, wrong Y origin (vertical mirror),
//     wrong width, wrong height.
//
//   Frame 1 — ScissorTestEnable=false (control, Phase 4): the SAME small rect is still stored but the
//     scissor test is OFF, so the full-NDC red quad must NOT be clipped — every probe RED. Guards
//     against the fix wrongly applying the stored rectangle when the scissor test is disabled.
//
//   Frame 2 — three scissor states in one frame (the per-draw-capture crux, Phase 5): within one RT
//     pass, queue three full-NDC quads each under a DIFFERENT scissor state, then unbind and read.
//     Each draw must use the scissor active WHEN IT WAS ENQUEUED. A solution that reads the final
//     scissor once at replay (or the pre-fix per-pass read, which after unbind is the full backbuffer)
//     would clip every quad identically and leave the whole target one color.
//       Draw 1: scissor DISABLED, BLUE full-NDC  => fills the whole target (background)
//       Draw 2: scissor A = (4,4,20,16) ON, RED  => clipped to x[4,24)  y[4,20)
//       Draw 3: scissor B = (40,26,20,16) ON, GREEN => clipped to x[40,60) y[26,42)
//     (The disabled/full-coverage draw is sequenced FIRST so the opaque background does not overwrite
//     the two clipped quads — A and B are disjoint and punch through it.)
//       A-center (14,12) -> RED     B-center (50,34) -> GREEN
//       background (32,10) -> BLUE   background (2,45) -> BLUE
//     Pre-fix / read-final-scissor-once signature: all three quads share one scissor, so A-center and
//     both backgrounds turn GREEN (last full draw wins), not RED/BLUE.
//
//   Frame 3 — Viewport ∩ ScissorRectangle (Phase 11): both dynamic states are now per-draw and must
//     be honored simultaneously and independently. Visible region = Viewport ∩ Scissor.
//       Viewport V = (8, 6, 40, 30)  => full-NDC quad fills x[8,48)  y[6,36)
//       Scissor  S = (20, 4, 30, 20) => clips to           x[20,50) y[4,24)
//       intersection interior (30,14) -> RED     (in V and in S)
//       scissor-clipped       (12,14) -> BLACK   (in V, x<20 outside S)  -> proves scissor still clips
//       viewport-clipped      (49,14) -> BLACK   (in S, x>=48 outside V) -> proves viewport still clips
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
static constexpr int kRTW = 64;   // render target width  (asymmetric vs height & backbuffer)
static constexpr int kRTH = 48;   // render target height

// Scissor sub-rectangle inside the RT: non-zero X and Y, not full-target, W != H.
static constexpr int kScX = 12, kScY = 8, kScW = 20, kScH = 13; // x[12,32) y[8,21)

class SdlGpuRenderTargetScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D>        rt_;
    std::unique_ptr<VertexBuffer>          redQuadVb_;
    std::unique_ptr<VertexBuffer>          greenQuadVb_;
    std::unique_ptr<VertexBuffer>          blueQuadVb_;
    std::unique_ptr<BasicEffect>           fx_;   // must outlive every draw (GPU resources)
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
    static bool isBlue(const Color& c)
    {
        return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60;
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
        // A full-NDC-covering quad (fills exactly whatever viewport rectangle is active, then is
        // clipped by whatever scissor rectangle is active).
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

    // Set RasterizerState.ScissorTestEnable + the ScissorRectangle for the next draw. Must be called
    // AFTER SetRenderTarget (which resets ScissorRectangle) and mirrors real game usage.
    void setScissor(GraphicsDevice& dev, bool enable, int x, int y, int w, int h)
    {
        RasterizerState rs = RasterizerState::CullNone;   // CCW full quad needs CullNone
        rs.setScissorTestEnableProperty(enable);
        dev.setRasterizerStateProperty(rs);
        dev.setScissorRectangleProperty(Rectangle(x, y, w, h));
    }

    Color readAt(int px, int py)
    {
        Color c(0, 0, 0, 0);
        const Rectangle reg(px, py, 1, 1);
        rt_->GetData(0, &reg, &c, 0, 1);
        return c;
    }

    void beginRTFrame(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rt_.get());            // resets Viewport + ScissorRectangle to full RT
        dev.Clear(Color(0, 0, 0, 255));            // RT background = black
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTW, kRTH);
        redQuadVb_   = makeFullNdcQuad(dev, Color(255, 0, 0, 255));
        greenQuadVb_ = makeFullNdcQuad(dev, Color(0, 255, 0, 255));
        blueQuadVb_  = makeFullNdcQuad(dev, Color(0, 0, 255, 255));
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
            // Core: scissor test ON — red survives only inside the scissor rectangle.
            beginRTFrame(dev);
            setScissor(dev, /*enable=*/true, kScX, kScY, kScW, kScH);
            drawQuad(dev, *redQuadVb_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); // resets scissor to backbuffer

            const Color in = readAt(20, 14), lf = readAt(5, 14), rt = readAt(40, 14);
            const Color ab = readAt(20, 3),  be = readAt(20, 30);
            check(isRed(in),   "RT scissor ON: inside (20,14)", in, "RED");
            check(isBlack(lf), "RT scissor ON: left   ( 5,14)", lf, "BLACK");
            check(isBlack(rt), "RT scissor ON: right  (40,14)", rt, "BLACK");
            check(isBlack(ab), "RT scissor ON: above  (20, 3)", ab, "BLACK");
            check(isBlack(be), "RT scissor ON: below  (20,30)", be, "BLACK");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Control: scissor test OFF — the stored rect must NOT clip; full RT is red.
            beginRTFrame(dev);
            setScissor(dev, /*enable=*/false, kScX, kScY, kScW, kScH);
            drawQuad(dev, *redQuadVb_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color in = readAt(20, 14), lf = readAt(5, 14), rt = readAt(40, 14);
            const Color ab = readAt(20, 3),  be = readAt(20, 30);
            check(isRed(in), "RT scissor OFF: inside (20,14)", in, "RED");
            check(isRed(lf), "RT scissor OFF: left   ( 5,14)", lf, "RED");
            check(isRed(rt), "RT scissor OFF: right  (40,14)", rt, "RED");
            check(isRed(ab), "RT scissor OFF: above  (20, 3)", ab, "RED");
            check(isRed(be), "RT scissor OFF: below  (20,30)", be, "RED");
            ++frame_;
            return;
        }

        if (frame_ == 2)
        {
            // State-timing: three scissor snapshots in one RT pass.
            beginRTFrame(dev);
            setScissor(dev, /*enable=*/false, 0, 0, kRTW, kRTH);
            drawQuad(dev, *blueQuadVb_);                 // BLUE background (scissor disabled)
            setScissor(dev, /*enable=*/true, 4, 4, 20, 16);
            drawQuad(dev, *redQuadVb_);                  // RED clipped to A x[4,24) y[4,20)
            setScissor(dev, /*enable=*/true, 40, 26, 20, 16);
            drawQuad(dev, *greenQuadVb_);                // GREEN clipped to B x[40,60) y[26,42)
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color a = readAt(14, 12), b = readAt(50, 34);
            const Color g0 = readAt(32, 10), g1 = readAt(2, 45);
            check(isRed(a),    "RT 3-scissor: A-center   (14,12)", a,  "RED");
            check(isGreen(b),  "RT 3-scissor: B-center   (50,34)", b,  "GREEN");
            check(isBlue(g0),  "RT 3-scissor: background (32,10)", g0, "BLUE");
            check(isBlue(g1),  "RT 3-scissor: background ( 2,45)", g1, "BLUE");
            ++frame_;
            return;
        }

        if (frame_ == 3)
        {
            // Viewport ∩ Scissor: both dynamic states honored per-draw, simultaneously.
            beginRTFrame(dev);
            dev.setViewportProperty(Viewport(8, 6, 40, 30)); // fills x[8,48) y[6,36)
            setScissor(dev, /*enable=*/true, 20, 4, 30, 20); // clips to x[20,50) y[4,24)
            drawQuad(dev, *redQuadVb_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color in = readAt(30, 14), sc = readAt(12, 14), vp = readAt(49, 14);
            check(isRed(in),   "RT vp∩scissor: intersection (30,14)", in, "RED");
            check(isBlack(sc), "RT vp∩scissor: scissor-clip  (12,14)", sc, "BLACK");
            check(isBlack(vp), "RT vp∩scissor: viewport-clip (49,14)", vp, "BLACK");

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    SdlGpuRenderTargetScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SdlGpuRenderTargetScissorTest game;
    game.Run();
    return game.getResult();
}
