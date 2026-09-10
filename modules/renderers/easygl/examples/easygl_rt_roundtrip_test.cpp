// SPDX-License-Identifier: MS-PL
// Task 180: backbuffer → RT → backbuffer → RT → backbuffer round-trip.
//
// Sequence (all within one Initialize pass):
//   1. Clear backbuffer Red (no RT bound).
//   2. SetRenderTarget(rt1)  — DiscardContents → auto-Clear(black); then Clear(Green).
//   3. SetRenderTarget(nullptr), then read RT1 through RenderTarget2D::GetData → expect Green.
//   4. SetRenderTarget(rt2)  — DiscardContents → auto-Clear(black); then Clear(Blue).
//   5. SetRenderTarget(nullptr), then read RT2 through RenderTarget2D::GetData → expect Blue.
//   6. Read backbuffer pixel → expect Red (never overwritten after step 1).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 4;

class RtRoundtripTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_   = 0;
    int fail_   = 0;
    int result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else { ++fail_; result_ = 1; }
    }

    Color readTargetPixel(RenderTarget2D& target, int x = 0, int y = 0)
    {
        Color px(0, 0, 0, 0);
        Rectangle reg(x, y, 1, 1);
        target.GetData(0, &reg, &px, 0, 1);
        return px;
    }

    Color readBackBufferPixel(GraphicsDevice& dev, int x = 0, int y = 0)
    {
        Color px(0, 0, 0, 0);
        Rectangle reg(x, y, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    bool eq(Color a, Color b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty();
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        RenderTarget2D rt1(dev, kSize, kSize, false, SurfaceFormat::Color,
                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        RenderTarget2D rt2(dev, kSize, kSize, false, SurfaceFormat::Color,
                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        // Step 1: fill backbuffer with Red.
        dev.Clear(Color::Red);

        // Step 2: switch to RT1, fill with Green.
        dev.SetRenderTarget(&rt1);   // DiscardContents → auto-Clear(0,0,0,255)
        dev.Clear(Color::Green);

        // Step 3: finalize RT1 and verify it through the target's public readback path.
        dev.SetRenderTarget(nullptr);
        Color px1 = readTargetPixel(rt1);
        std::printf("RT1 readback: R=%d G=%d B=%d\n",
            px1.getRProperty(), px1.getGProperty(), px1.getBProperty());
        check(eq(px1, Color::Green), "RT1 contains Green after Clear");

        // Step 4: switch to RT2, fill with Blue.
        dev.SetRenderTarget(&rt2);   // DiscardContents → auto-Clear(0,0,0,255)
        dev.Clear(Color::Blue);

        // Step 5: finalize RT2 and verify it through the target's public readback path.
        dev.SetRenderTarget(nullptr);
        Color px2 = readTargetPixel(rt2);
        std::printf("RT2 readback: R=%d G=%d B=%d\n",
            px2.getRProperty(), px2.getGProperty(), px2.getBProperty());
        check(eq(px2, Color::Blue), "RT2 contains Blue after Clear");

        // Step 6: verify backbuffer still contains Red.
        Color pxBB = readBackBufferPixel(dev);
        std::printf("Backbuffer readback: R=%d G=%d B=%d\n",
            pxBB.getRProperty(), pxBB.getGProperty(), pxBB.getBProperty());
        check(eq(pxBB, Color::Red), "Backbuffer preserved Red across RT switches");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    RtRoundtripTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    RtRoundtripTest game;
    game.Run();
    return game.getResult();
}
