// SPDX-License-Identifier: MS-PL
// REMED-GFX-013: Vulkan ScissorRectangle must clip draws issued while a RenderTarget2D is bound.
//
// Before the fix, VulkanGraphicsBackend::RecordCommandBuffer()'s render-target pass (Phase 1)
// hardcoded a full-target scissor (VkRect2D{{0,0},{rtW,rtH}}) and never read the captured
// scissorEnabled_/scissorX_/Y_/W_/H_ state, so ScissorRectangle was a silent no-op whenever a
// render target was bound — even though the backbuffer pass (Phase 2) honored it correctly.
//
// Because the Vulkan backend defers every draw to a single Present()-time record, and because
// Task 338 (FNA parity) resets ScissorRectangle when a render target is unbound, honoring only the
// frame-global scissor at record time is NOT enough — by Present() the sub-rect the RT was drawn
// with has already been overwritten. The fix therefore snapshots the scissor per draw/batch at
// enqueue time and replays it per draw. This test proves that end-to-end through the standard
// "render to RT, unbind, sample RT onto the backbuffer via SpriteBatch, read back" methodology
// (the same path vulkan_rt2d_test.cpp uses — GetBackBufferData on Vulkan always reads the swapchain,
// never a bound RT).
//
// Geometry (chosen asymmetric on every axis to catch origin/width/height/Y-mirror errors):
//   RenderTarget2D  = 48 x 32           (asymmetric, and != the 64x64 backbuffer)
//   ScissorRectangle = (12, 8, 20, 12)  => clipped region x in [12,32), y in [8,20)
//
//   Frame 0 — ScissorTestEnable=true: a full-RT red quad must survive ONLY inside the scissor.
//     probe inside  (20,13) -> RED     probe left  ( 4,13) -> BLACK
//     probe right   (40,13) -> BLACK   probe above (20, 3) -> BLACK
//     probe below   (20,26) -> BLACK
//   This 5-way probe distinguishes: scissor ignored entirely, wrong origin, vertical mirror,
//   wrong width/height, and full-target hardcoded scissor.
//
//   Frame 1 — ScissorTestEnable=false (control, Phase 5): the SAME rect is set but the scissor
//     test is off, so the full-RT red quad must NOT be clipped — every probe RED. Guards against
//     the fix wrongly applying the stored rectangle when the scissor test is disabled.
//
// Backbuffer scissor is exercised separately by Vulkan_ScissorTest (control, Phase 4) and the
// backbuffer/RT reset transition by Vulkan_RenderTarget_ViewportScissorReset (Phase 8).
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64;   // backbuffer width  (!= RT, to catch dimension leaks)
static constexpr int kBBH = 64;   // backbuffer height
static constexpr int kRTW = 48;   // render target width  (asymmetric)
static constexpr int kRTH = 32;   // render target height

// Scissor sub-rectangle inside the RT: non-zero X and Y, not full-target, W != H.
static constexpr int kScX = 12, kScY = 8, kScW = 20, kScH = 12; // x[12,32) y[8,20)

class VulkanRenderTargetScissorTest : public Game
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

    // A full-RT-covering red quad in NDC (the Vulkan Y-flip keeps this full-coverage).
    static void drawFullRTRedQuad(GraphicsDevice& dev)
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

    // Render the red quad into rt_ under the given scissor-test flag, unbind, then blit rt_ 1:1
    // onto the backbuffer so the RT's clipped content can be read via GetBackBufferData.
    void renderRTAndBlit(GraphicsDevice& dev, bool scissorTestEnable)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        // --- Phase 1: draw the clipped red quad into the RT ---
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));               // RT background = black
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Set scissor state AFTER binding: SetRenderTarget resets ScissorRectangle (Task 338).
        RasterizerState rs = RasterizerState::CullNone; // CCW full quad needs CullNone
        rs.setScissorTestEnableProperty(scissorTestEnable);
        dev.setRasterizerStateProperty(rs);
        dev.setScissorRectangleProperty(Rectangle(kScX, kScY, kScW, kScH));
        fx.Apply();
        drawFullRTRedQuad(dev);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); // Task 338 resets scissor

        // --- Phase 2: blit the rendered RT onto the backbuffer, unclipped, 1:1 at origin ---
        dev.Clear(Color(0, 0, 0, 255));
        dev.setRasterizerStateProperty(RasterizerState()); // scissor disabled for the blit
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
            // Core: scissor test ON — red survives only inside the scissor rectangle.
            Color in(0,0,0,0), lf(0,0,0,0), rt(0,0,0,0), ab(0,0,0,0), be(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderRTAndBlit(dev, /*scissorTestEnable=*/true);
                in = readAt(dev, 20, 13);   // inside  scissor
                lf = readAt(dev,  4, 13);   // left of scissor
                rt = readAt(dev, 40, 13);   // right of scissor
                ab = readAt(dev, 20,  3);   // above  scissor
                be = readAt(dev, 20, 26);   // below  scissor
                if (isRed(in)) break;       // produced a non-blank frame
            }
            check(isRed(in),   "RT scissor ON: inside  (20,13)", in, "RED");
            check(isBlack(lf), "RT scissor ON: left    ( 4,13)", lf, "BLACK");
            check(isBlack(rt), "RT scissor ON: right   (40,13)", rt, "BLACK");
            check(isBlack(ab), "RT scissor ON: above   (20, 3)", ab, "BLACK");
            check(isBlack(be), "RT scissor ON: below   (20,26)", be, "BLACK");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Control: scissor test OFF — the stored rect must NOT clip; full RT is red.
            Color in(0,0,0,0), lf(0,0,0,0), ab(0,0,0,0), be(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderRTAndBlit(dev, /*scissorTestEnable=*/false);
                in = readAt(dev, 20, 13);
                lf = readAt(dev,  4, 13);
                ab = readAt(dev, 20,  3);
                be = readAt(dev, 20, 26);
                if (isRed(in)) break;
            }
            check(isRed(in), "RT scissor OFF: inside  (20,13)", in, "RED");
            check(isRed(lf), "RT scissor OFF: left    ( 4,13)", lf, "RED");
            check(isRed(ab), "RT scissor OFF: above   (20, 3)", ab, "RED");
            check(isRed(be), "RT scissor OFF: below   (20,26)", be, "RED");

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    VulkanRenderTargetScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRenderTargetScissorTest game;
    game.Run();
    return game.getResult();
}
