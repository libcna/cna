// SPDX-License-Identifier: MS-PL
// Task 178: RenderTargetUsage::DiscardContents vs PreserveContents in Vulkan.
//
// Vulkan uses a deferred two-phase model: RT render passes run inside
// RecordCommandBuffer (triggered by Present / GetBackBufferData), not at
// SetRenderTarget() time. The loadOp on the RT render pass determines whether
// previous-frame content is preserved or discarded.
//
// Test structure (2 Draw() frames):
//
//   Frame 0 — fill all three RTs:
//     rt_preserve2 → full-screen BLUE quad, then a LEFT-HALF WHITE quad   (recorded FIRST)
//     rt_discard   → full-screen RED   quad
//     rt_preserve  → full-screen GREEN quad
//     (EndDraw calls Present() automatically)
//     rt_preserve2 goes first deliberately: a per-frame vertex arena whose cursor restarts at 0
//     for every pass is only detectable on a pass that is NOT the last writer.
//
//   Frame 1 — verify behaviour:
//     REMED-GFX-140 strengthening: rt_preserve2 is checked FIRST, before anything is rebound.
//     Frame 0 records three targets' passes into ONE command buffer, and this file never looked
//     at what those passes actually produced -- it only ever looked at rt_discard AFTER a discard
//     had wiped it, and at rt_preserve's uniform fill, which is indistinguishable from any other
//     target's uniform fill. Both of the defects REMED-GFX-140 fixed were therefore invisible
//     here: passes sharing one per-frame vertex arena whose cursor restarted at 0 (so an earlier
//     pass drew a later pass's geometry), and bind cycles of one target collapsing into one pass.
//     rt_preserve2 differs from the other two in COLOUR and, between its own two draws, in
//     GEOMETRY, and both halves are asserted byte-exact (BLUE and WHITE are 0/255-only, so no
//     sRGB tolerance is needed, unlike Color::Green's 128).
//     DiscardContents: re-bind rt_discard, draw left-half BLUE quad to force
//       an RT render pass.  LOAD_OP_CLEAR clears the right half to black (not red).
//       Blit to BB → GetBackBufferData at right half → expect black (not red).
//     PreserveContents: re-bind rt_preserve, draw left-half BLUE quad.
//       LOAD_OP_LOAD keeps right half GREEN.
//       Blit to BB → GetBackBufferData at right half → expect green.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// Full-screen NDC quad (two triangles, 6 vertices).
static void drawFullScreen(GraphicsDevice& dev, Color col)
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

// Left-half NDC quad (NDC x ∈ [-1, 0]).  Right half is untouched by this draw.
static void drawLeftHalf(GraphicsDevice& dev, Color col)
{
    const VertexPositionColor q[6] = {
        { Vector3(-1.f,  1.f, 0.f), col },
        { Vector3(-1.f, -1.f, 0.f), col },
        { Vector3( 0.f, -1.f, 0.f), col },
        { Vector3(-1.f,  1.f, 0.f), col },
        { Vector3( 0.f, -1.f, 0.f), col },
        { Vector3( 0.f,  1.f, 0.f), col },
    };
    dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
}

class VulkanRTUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_discard_;
    std::unique_ptr<RenderTarget2D>        rt_preserve_;
    std::unique_ptr<RenderTarget2D>        rt_preserve2_;
    int  frame_  = 0;
    int  pass_   = 0;
    int  fail_   = 0;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else { ++fail_; result_ = 1; }
    }

    bool colourMatch(Color a, Color b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty();
    }

    // Read one pixel from the back buffer at pixel (x, y).
    // GetBackBufferData triggers Present() if there are pending draws.
    Color readBB(GraphicsDevice& dev, int x, int y)
    {
        Color px(0, 0, 0, 0);
        Rectangle reg(x, y, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_         = std::make_unique<SpriteBatch>(dev);
        rt_discard_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::DiscardContents);
        rt_preserve_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::PreserveContents);
        rt_preserve2_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding: drawFullScreen()/drawLeftHalf()'s winding is CCW/back-facing
        // under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Keep the BasicEffect alive for the entire frame so currentEffect_ stays valid.
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        // VertexColorEnabled defaults to false (Task 361); this test's drawFullScreen()
        // quads carry the desired colour as a per-vertex attribute, so it must be
        // explicitly enabled here (previously masked by a since-fixed Vulkan bug — Task
        // 364 — where the colored3D pipeline ignored this flag and always used vertex color).
        fx.VertexColorEnabled = true;
        fx.Apply();

        if (frame_ == 0)
        {
            // REMED-GFX-140: a target whose content differs from both others in colour AND whose
            // own two draws differ in geometry, recorded FIRST so that a shared arena cursor
            // restarting per pass would overwrite its vertices before the queue is submitted. All
            // three targets' passes are recorded into one command buffer by the Present below.
            dev.SetRenderTarget(rt_preserve2_.get());
            drawFullScreen(dev, Color::Blue);
            drawLeftHalf(dev, Color::White);
            dev.SetRenderTarget(nullptr);

            // Fill rt_discard with red.
            dev.SetRenderTarget(rt_discard_.get());
            drawFullScreen(dev, Color::Red);
            dev.SetRenderTarget(nullptr);

            // Fill rt_preserve with green.
            dev.SetRenderTarget(rt_preserve_.get());
            drawFullScreen(dev, Color::Green);
            dev.SetRenderTarget(nullptr);

            // EndDraw() → Present() submits both RT passes; images are now in
            // SHADER_READ_ONLY_OPTIMAL with their respective fill colours.
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            const int rxEarly = kSize * 3 / 4;
            const int ryEarly = kSize / 2;
            const int lxEarly = kSize / 4;

            // ── REMED-GFX-140: what frame 0's shared record actually produced ──
            // Nothing is rebound before this, so the only thing being read is the result of three
            // render-target passes recorded side by side into one command buffer.
            sb_->Begin();
            sb_->Draw(*rt_preserve2_,
                      Rectangle(0, 0, kSize, kSize),
                      Rectangle(0, 0, kSize, kSize),
                      Color::White);
            sb_->End();
            Color px2Right = readBB(dev, rxEarly, ryEarly);
            Color px2Left  = readBB(dev, lxEarly, ryEarly);
            std::printf("shared record right-half: R=%d G=%d B=%d, left-half: R=%d G=%d B=%d\n",
                        px2Right.getRProperty(), px2Right.getGProperty(), px2Right.getBProperty(),
                        px2Left.getRProperty(), px2Left.getGProperty(), px2Left.getBProperty());
            check(colourMatch(px2Right, Color::Blue),
                  "shared record: the third target kept its OWN colour, not another target's");
            check(colourMatch(px2Left, Color::White),
                  "shared record: the third target's second draw kept its OWN geometry");

            // ── DiscardContents ──────────────────────────────────────────────
            // Re-bind rt_discard: SetRenderTarget calls Clear(0,0,0,255) via XNA
            // layer which sets the Vulkan clear colour to black.
            // Draw left-half blue to force a render pass for this RT.
            // LOAD_OP_CLEAR: entire RT starts black; left half becomes blue,
            // right half stays black (no longer red).
            dev.SetRenderTarget(rt_discard_.get());
            drawLeftHalf(dev, Color::Blue);
            dev.SetRenderTarget(nullptr);

            // Blit rt_discard to full BB.
            sb_->Begin();
            sb_->Draw(*rt_discard_,
                      Rectangle(0, 0, kSize, kSize),
                      Rectangle(0, 0, kSize, kSize),
                      Color::White);
            sb_->End();

            // Read right-half pixel (should be black, not red).
            const int rx = kSize * 3 / 4;
            const int ry = kSize / 2;
            Color pxD = readBB(dev, rx, ry);
            std::printf("DiscardContents right-half: R=%d G=%d B=%d\n",
                        pxD.getRProperty(), pxD.getGProperty(), pxD.getBProperty());
            check(!colourMatch(pxD, Color::Red),
                  "DiscardContents: right-half is not red");
            check(pxD.getRProperty() < 30 && pxD.getGProperty() < 30 && pxD.getBProperty() < 30,
                  "DiscardContents: right-half is black (cleared)");

            // ── PreserveContents ─────────────────────────────────────────────
            // Re-bind rt_preserve: NO XNA clear (PreserveContents).
            // Draw left-half blue to force a render pass for this RT.
            // LOAD_OP_LOAD: entire RT loaded from frame 0 (green); left half
            // becomes blue, right half stays green.
            dev.SetRenderTarget(rt_preserve_.get());
            drawLeftHalf(dev, Color::Blue);
            dev.SetRenderTarget(nullptr);

            // Blit rt_preserve to full BB.
            sb_->Begin();
            sb_->Draw(*rt_preserve_,
                      Rectangle(0, 0, kSize, kSize),
                      Rectangle(0, 0, kSize, kSize),
                      Color::White);
            sb_->End();

            // Read right-half pixel (should be green — sRGB encoding of
            // Color::Green maps G=128 linear to ~G=188 on screen).
            Color pxP = readBB(dev, rx, ry);
            std::printf("PreserveContents right-half: R=%d G=%d B=%d\n",
                        pxP.getRProperty(), pxP.getGProperty(), pxP.getBProperty());
            check(pxP.getRProperty() < 30 && pxP.getGProperty() > 100 && pxP.getBProperty() < 30,
                  "PreserveContents: right-half is green (preserved)");

            std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
            ++frame_;
            Exit();
        }
    }

public:
    VulkanRTUsageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanRTUsageTest game;
    game.Run();
    return game.getResult();
}
