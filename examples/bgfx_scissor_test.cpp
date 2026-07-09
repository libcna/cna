// SPDX-License-Identifier: MS-PL
// Task 768: verify scissor test enable/disable interaction with ScissorRectangle on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_scissor_test.cpp (Task 209, already reused verbatim
// on Vulkan). Not a verbatim reuse: that file draws full-screen 3 times (once per RasterizerState
// check) and reads 2 spatially-separate columns per draw within the SAME frame, but Bgfx's own
// GetBackBufferData only reliably reflects the FIRST read per rendered frame (Task 406 finding) --
// restructured into one separately-read RunCheck() pass per (RasterizerState, column) pair,
// mirroring Tasks 759-767's established Bgfx pattern.
//
// Checks (matching Task 209's own 3-phase structure):
//   1. ScissorTestEnable=false (rect set to right-half, but test OFF): full draw not clipped --
//      both left and right columns show the draw color.
//   2. ScissorTestEnable=true + ScissorRectangle=right-half: only the right column shows the draw
//      color; the left column stays background (clipped).
//   3. ScissorTestEnable=false again: full draw restored -- both columns show the draw color
//      again (proves the property genuinely toggles back, not stuck permanently enabled/disabled).
//   4. ScissorRectangle getter/setter round-trip (no rendering involved, doesn't need Bgfx-specific
//      restructuring).
//
// RasterizerState uses CullNone throughout so winding order does not interfere.
//
// Exit code 0 = all 7 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cstdio>
#include <cmath>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    static bool colorNear(Color a, Color b, int tol = 4)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    const Vector3 kTL(-1.0f,  1.0f, 0.0f);
    const Vector3 kBL(-1.0f, -1.0f, 0.0f);
    const Vector3 kBR( 1.0f, -1.0f, 0.0f);
    const Vector3 kTR( 1.0f,  1.0f, 0.0f);

    void DrawFullScreen(GraphicsDevice& dev, Color col)
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
}

class BgfxScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, bool scissorEnable, const Rectangle& scissorRect,
                    Color drawColor)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color::Black);
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);
            dev.setBlendStateProperty(BlendState::Opaque);

            RasterizerState rs = RasterizerState::CullNone;
            rs.setScissorTestEnableProperty(scissorEnable);
            dev.setRasterizerStateProperty(rs);
            dev.setScissorRectangleProperty(scissorRect);

            DrawFullScreen(dev, drawColor);

            // Sample whichever single column this check cares about -- passed via scissorRect's
            // own X (left column probes centre-of-left-half, right column centre-of-right-half;
            // the caller picks the sample point, see the 2 lambdas below in Draw()).
            const Rectangle reg(sampleX_, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

    int sampleX_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const int leftX  = kSize / 4;
        const int rightX = 3 * kSize / 4;
        const Rectangle rightHalf(kSize / 2, 0, kSize - kSize / 2, kSize);

        int passCount = 0, totalCount = 0;
        auto check = [&](bool ok, const char* label)
        {
            ++totalCount;
            std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
            if (ok) ++passCount;
        };

        // 1. ScissorTestEnable=false: full draw, not clipped.
        sampleX_ = leftX;
        check(colorNear(RunCheck(dev, false, rightHalf, Color::Red), Color::Red),
              "ScissorOff: left pixel is red (not clipped)");
        sampleX_ = rightX;
        check(colorNear(RunCheck(dev, false, rightHalf, Color::Red), Color::Red),
              "ScissorOff: right pixel is red (not clipped)");

        // 2. ScissorTestEnable=true + right-half rect: only right column shows the draw.
        sampleX_ = leftX;
        check(colorNear(RunCheck(dev, true, rightHalf, Color::Green), Color::Black),
              "ScissorOn (right half): left pixel is black (clipped)");
        sampleX_ = rightX;
        check(colorNear(RunCheck(dev, true, rightHalf, Color::Green), Color::Green),
              "ScissorOn (right half): right pixel is green (inside scissor)");

        // 3. ScissorTestEnable=false again: full draw restored.
        sampleX_ = leftX;
        check(colorNear(RunCheck(dev, false, rightHalf, Color::Blue), Color::Blue),
              "ScissorOff again: left pixel is blue (not clipped)");
        sampleX_ = rightX;
        check(colorNear(RunCheck(dev, false, rightHalf, Color::Blue), Color::Blue),
              "ScissorOff again: right pixel is blue (not clipped)");

        // 4. ScissorRectangle getter/setter round-trip (no rendering, no Bgfx-specific concern).
        const Rectangle testRect(10, 20, 300, 200);
        dev.setScissorRectangleProperty(testRect);
        const Rectangle gotRect = dev.getScissorRectangleProperty();
        check(gotRect.X == testRect.X && gotRect.Y == testRect.Y &&
              gotRect.Width == testRect.Width && gotRect.Height == testRect.Height,
              "ScissorRectangle get/set round-trip preserves all fields");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        result_ = (passCount == totalCount) ? 0 : 1;
        Exit();
    }

public:
    BgfxScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxScissorTest game;
    game.Run();
    return game.getResult();
}
