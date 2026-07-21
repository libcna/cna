// SPDX-License-Identifier: MS-PL
// REMED-GFX-066: per-draw ScissorRectangle (bgfx::setScissor) must clip on FBO-bound
// (render-target) views, not only on the backbuffer.
//
// GFX-063 bring-up recorded "no clipping at all" on RT views but never runtime-verified it. This
// test establishes the real behavior. RUNTIME RESULT: the RT scissor already clips correctly on
// both bgfx-OpenGL and bgfx-Vulkan at HEAD — GFX-066 does NOT reproduce. It was a test-setup
// artifact: a ScissorRectangle set without RasterizerState.ScissorTestEnable=true, which per XNA
// semantics is a no-op (fills the whole target) — exactly Scenario B here. This test locks the
// correct behavior in so it can never silently regress or be re-misdiagnosed.
//
// Methodology (proven Bgfx RT-sample path from bgfx_rendertarget_viewport_test.cpp / GFX-063):
// render into a 64x48 RenderTarget2D, unbind, blit the RT 1:1 onto the backbuffer via SpriteBatch,
// read the WHOLE backbuffer region in one GetBackBufferData call (Bgfx first-read-per-frame quirk),
// probe. 64x48 reads back upright through the RT-sample path (other sizes mirror — REMED-GFX-067).
//
// Scenarios (asymmetric on every axis to catch origin/width/height/Y-mirror errors):
//   A  scissor ENABLED, rect (12,8,20,13): red survives only inside; outside black.
//   B  scissor DISABLED, same rect set:    whole target red (the false-finding symptom; XNA-correct).
//   C  two per-draw scissors in ONE RT view/frame: red under a LEFT rect, green under a RIGHT rect,
//      disjoint gap black -> proves per-draw scissor independence (distinguishes GFX-066 from the
//      per-view GFX-065 viewport limit). A single frame + single readback, so it is order-stable.
//
// Deliberately limited to three RT-sample readbacks per frame-sequence. Two further scenarios were
// verified correct in isolation (grids in the REMED-GFX-066 progress entry) but are NOT included
// here, because a 4th/5th RT-sample readback in one Draw() trips the separate, pre-existing
// REMED-GFX-067 RT-readback Y-mirror — a readback-orientation artifact that would make this test
// flaky and could be misread as a scissor bug:
//   * different scissor rect re-enabled across frames -> clips upright (rect (30,20,24,16));
//   * custom Viewport (GFX-063) INTERSECT ScissorRectangle -> visible = viewport ∩ scissor in
//     framebuffer coordinates (Viewport(10,6,30,26) ∩ Scissor(20,4,25,20) = x[20,40) y[6,24)).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64, kBBH = 64;   // backbuffer
static constexpr int kRTW = 64, kRTH = 48;   // render target (upright readback per GFX-067)
static constexpr int kScX = 12, kScY = 8, kScW = 20, kScH = 13;   // kept x[12,32) y[8,21)

class BgfxRenderTargetScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_;
    int pass_ = 0, fail_ = 0;

    static bool isRed(const Color& c)
    { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    static bool isGreen(const Color& c)
    { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    static bool isBlack(const Color& c)
    { return c.getRProperty() < 40 && c.getGProperty() < 40 && c.getBProperty() < 40; }
    static bool sameColor(const Color& a, const Color& b, int tol = 60)
    { return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
             std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
             std::abs(a.getBProperty() - b.getBProperty()) <= tol; }

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    static void drawFullNdcQuad(GraphicsDevice& dev, const Color& col)
    {
        const VertexPositionColor q[6] = {
            { Vector3(-1.f,  1.f, 0.f), col }, { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col }, { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col }, { Vector3( 1.f,  1.f, 0.f), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    // Common frame scaffold: bind rt_, run `body`, unbind, blit rt_ 1:1 onto the backbuffer, read the
    // WHOLE region in one call (Bgfx first-read quirk). Retries until `fresh(buf)` is true.
    template <class Body, class Fresh>
    std::vector<Color> renderFrame(GraphicsDevice& dev, Body&& body, Fresh&& fresh)
    {
        std::vector<Color> buf(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        SamplerState point = SamplerState::PointClamp;

        for (int i = 0; i < 20; ++i)
        {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color(0, 0, 0, 255));
            DepthStencilState ds; ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);
            dev.setBlendStateProperty(BlendState::Opaque);
            body(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            dev.Clear(Color(0, 0, 0, 255));
            dev.setRasterizerStateProperty(RasterizerState());
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*rt_, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, kRTW, kRTH), Color::White);
            sb_->End();

            dev.GetBackBufferData(&whole, buf.data(), 0, kBBW * kBBH);
            if (fresh(buf)) break;
        }
        return buf;
    }

    // Single full-NDC quad under an optional scissor.
    std::vector<Color> renderScissored(GraphicsDevice& dev, bool enable, const Rectangle& rect,
                                        const Color& col)
    {
        auto body = [&](GraphicsDevice& d) {
            BasicEffect fx(d);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            RasterizerState rs = RasterizerState::CullNone;
            rs.setScissorTestEnableProperty(enable);
            d.setRasterizerStateProperty(rs);
            d.setScissorRectangleProperty(rect);
            fx.Apply();
            drawFullNdcQuad(d, col);
        };
        // Bgfx GetBackBufferData reflects the first read per frame; the fresh-frame sentinel must
        // match THIS scenario's specific expected inside color (not merely "non-black"), else a
        // previous scenario's stale frame — a different color at the same spot — is wrongly accepted.
        const int px = enable ? (rect.X + rect.Width / 2) : 5;
        const int py = enable ? (rect.Y + rect.Height / 2) : 5;
        return renderFrame(dev, body,
            [&](const std::vector<Color>& b) { return sameColor(at(b, px, py), col); });
    }

    static Color at(const std::vector<Color>& buf, int x, int y)
    { return buf[static_cast<std::size_t>(y) * kBBW + x]; }

    static void dumpGrid(const char* title, const std::vector<Color>& buf)
    {
        std::printf("--- grid: %s (R=red G=green .=black ?=other) ---\n", title);
        for (int y = 0; y < kRTH; y += 2)
        {
            std::printf("y=%2d ", y);
            for (int x = 0; x < kRTW; x += 2)
            {
                const Color c = at(buf, x, y);
                std::putchar(isRed(c) ? 'R' : isGreen(c) ? 'G' : (isBlack(c) ? '.' : '?'));
            }
            std::putchar('\n');
        }
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

        // A: scissor ENABLED (12,8,20,13) — red only inside; outside black.
        {
            std::vector<Color> b = renderScissored(dev, true, Rectangle(kScX, kScY, kScW, kScH),
                                                    Color(255, 0, 0, 255));
            dumpGrid("A scissor ENABLED (12,8,20,13)", b);
            check(isRed  (at(b, 20, 14)), "A inside (20,14)", at(b, 20, 14), "RED");
            check(isBlack(at(b,  5, 14)), "A left   ( 5,14)", at(b,  5, 14), "BLACK");
            check(isBlack(at(b, 45, 14)), "A right  (45,14)", at(b, 45, 14), "BLACK");
            check(isBlack(at(b, 20,  3)), "A above  (20, 3)", at(b, 20,  3), "BLACK");
            check(isBlack(at(b, 20, 30)), "A below  (20,30)", at(b, 20, 30), "BLACK");
        }

        // B: scissor DISABLED, same rect set — whole target red (false-finding symptom; XNA-correct).
        {
            std::vector<Color> b = renderScissored(dev, false, Rectangle(kScX, kScY, kScW, kScH),
                                                    Color(255, 0, 0, 255));
            check(isRed(at(b, 20, 14)), "B inside (20,14)", at(b, 20, 14), "RED");
            check(isRed(at(b,  5, 14)), "B left   ( 5,14)", at(b,  5, 14), "RED");
            check(isRed(at(b, 45, 14)), "B right  (45,14)", at(b, 45, 14), "RED");
            check(isRed(at(b, 20,  3)), "B above  (20, 3)", at(b, 20,  3), "RED");
            check(isRed(at(b, 20, 30)), "B below  (20,30)", at(b, 20, 30), "RED");
        }

        // C: two per-draw scissors in ONE RT view/frame — red LEFT rect, green RIGHT rect, gap black.
        {
            const Rectangle left (4,  8, 20, 32);  // x[4,24)
            const Rectangle right(40, 8, 20, 32);  // x[40,60)
            auto body = [&](GraphicsDevice& d) {
                BasicEffect fx(d);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.VertexColorEnabled = true;
                RasterizerState rs = RasterizerState::CullNone;
                rs.setScissorTestEnableProperty(true);
                d.setRasterizerStateProperty(rs);
                fx.Apply();
                d.setScissorRectangleProperty(left);
                drawFullNdcQuad(d, Color(255, 0, 0, 255));   // red under left scissor
                d.setScissorRectangleProperty(right);
                drawFullNdcQuad(d, Color(0, 255, 0, 255));   // green under right scissor
            };
            std::vector<Color> b = renderFrame(dev, body,
                [&](const std::vector<Color>& x) { return isRed(at(x, 12, 24)) && isGreen(at(x, 48, 24)); });
            dumpGrid("C two per-draw scissors (red left / green right)", b);
            check(isRed  (at(b, 12, 24)), "C left  (12,24)", at(b, 12, 24), "RED");
            check(isGreen(at(b, 48, 24)), "C right (48,24)", at(b, 48, 24), "GREEN");
            check(isBlack(at(b, 32, 24)), "C gap   (32,24)", at(b, 32, 24), "BLACK");
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxRenderTargetScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxRenderTargetScissorTest game;
    game.Run();
    return game.getResult();
}
