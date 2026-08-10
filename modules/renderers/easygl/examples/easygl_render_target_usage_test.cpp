// SPDX-License-Identifier: MS-PL
// Task 177: RenderTargetUsage::DiscardContents vs PreserveContents
//
// DiscardContents — SetRenderTarget calls Clear(0,0,0,255) on every bind.
//   A red fill written before unbinding must NOT survive the re-bind.
//
// PreserveContents — SetRenderTarget does NOT clear on re-bind.
//   A green fill written before unbinding must survive the re-bind unchanged.
//
// Verification: GetBackBufferData is called while the RT's FBO is still
// bound (currentRtHeight_ != 0), which reads directly from the RT attachment.
// No SpriteBatch round-trip needed.

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

class RenderTargetUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;
    int pass_   = 0;
    int fail_   = 0;

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

    // Read one pixel from the currently bound framebuffer (RT or backbuffer).
    Color readPixel(GraphicsDevice& dev, int x, int y)
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

        // ── Case A: DiscardContents ──────────────────────────────────────────
        //
        // Fill RT with red, unbind, re-bind.
        // SetRenderTarget with DiscardContents must Clear → RT is no longer red.
        {
            RenderTarget2D rt(dev, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Red);
            dev.SetRenderTarget(nullptr);

            // Re-bind → DiscardContents issues Clear(0,0,0,255).
            dev.SetRenderTarget(&rt);

            // Read directly from the RT's FBO (it is still bound).
            Color px = readPixel(dev, 0, 0);
            std::printf("DiscardContents readback: R=%d G=%d B=%d\n",
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            check(!colourMatch(px, Color::Red),
                  "DiscardContents: contents are discarded (not red)");
            check(px.getRProperty() == 0 && px.getGProperty() == 0 && px.getBProperty() == 0,
                  "DiscardContents: cleared to black (0,0,0)");

            dev.SetRenderTarget(nullptr);
        }

        // ── Case B: PreserveContents ─────────────────────────────────────────
        //
        // Fill RT with green, unbind, re-bind.
        // SetRenderTarget with PreserveContents must NOT clear → RT keeps green.
        {
            RenderTarget2D rt(dev, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Green);
            dev.SetRenderTarget(nullptr);

            // Re-bind → PreserveContents: no clear.
            dev.SetRenderTarget(&rt);

            // Read directly from the RT's FBO.
            Color px = readPixel(dev, 0, 0);
            std::printf("PreserveContents readback: R=%d G=%d B=%d\n",
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            check(colourMatch(px, Color::Green),
                  "PreserveContents: contents are preserved (green)");

            dev.SetRenderTarget(nullptr);
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    RenderTargetUsageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    RenderTargetUsageTest game;
    game.Run();
    return game.getResult();
}
