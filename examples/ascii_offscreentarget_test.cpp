// SPDX-License-Identifier: MS-PL
// plan_ascii.md Phase G3 (ASCII-20/21): the game must never draw straight onto the real
// backbuffer -- everything goes to a private offscreen target (gameTarget_) instead, and
// SetRenderTarget(nullptr) ("target the back buffer" in XNA) must transparently redirect there.
//
// Check A -- Clear() + GetBackBufferData() round-trips the exact clear color (gameTarget_ is
//   genuinely readable back, not a fiction).
// Check B -- binding the game's own explicit RenderTarget2D, clearing it, then calling
//   SetRenderTarget(nullptr) and clearing again: GetBackBufferData() reflects the SECOND clear,
//   not the first -- proves nullptr really redirects to gameTarget_, not to the explicit target.
// Check C -- re-binding the explicit RenderTarget2D afterwards still shows its OWN first clear
//   color, unaffected by anything done to gameTarget_ in between -- proves the two framebuffers
//   are genuinely independent, not aliased.
// Check D -- Present() does not throw (the Phase G3 plain stretch-blit path).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool ColorEquals(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }
}

class AsciiOffscreenTargetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Color colorA(10, 200, 30, 255);
        const Color colorB(220, 40, 90, 255);
        const Color colorC(30, 60, 220, 255);

        const Rectangle onePixel(0, 0, 1, 1);

        // Check A: gameTarget_ round-trips a plain Clear() correctly.
        dev.Clear(colorA);
        {
            std::vector<Color> pixel(1, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&onePixel, pixel.data(), 0, 1);
            check(ColorEquals(pixel[0], colorA), "Clear()+GetBackBufferData() round-trips the exact color (gameTarget_ readback)");
        }

        // Check B/C: explicit RenderTarget2D vs. SetRenderTarget(nullptr) redirect.
        // RenderTargetUsage::PreserveContents (not the 2-arg ctor's DiscardContents default) --
        // DiscardContents auto-clears to black on every bind (real, documented XNA/FNA behavior,
        // see docs/sdl-renderer-2d-completeness.md's RenderTarget2D section), which would make
        // Check C below fail for a reason unrelated to what it's actually testing.
        RenderTarget2D explicitRt(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(&explicitRt);
        dev.Clear(colorB);

        dev.SetRenderTarget(nullptr);
        dev.Clear(colorC);
        {
            std::vector<Color> pixel(1, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&onePixel, pixel.data(), 0, 1);
            check(ColorEquals(pixel[0], colorC),
                  "SetRenderTarget(nullptr) redirects to gameTarget_, not the explicit RenderTarget2D");
        }

        dev.SetRenderTarget(&explicitRt);
        {
            // GraphicsDevice::GetBackBufferData reads whatever is currently bound (here,
            // explicitRt), which is what this check is testing. (The former claim that
            // Texture2D::GetData "would need a CPU-side shadow copy this backend doesn't populate"
            // no longer holds: REMED-GFX-127 gave the wrapped SdlRenderer target a real
            // SDL_RenderReadPixels readback, covered by Ascii_Texture2D_GetDataContract.)
            std::vector<Color> pixel(1, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&onePixel, pixel.data(), 0, 1);
            check(ColorEquals(pixel[0], colorB),
                  "The explicit RenderTarget2D still holds its own first clear, unaffected by gameTarget_ activity");
        }
        dev.SetRenderTarget(nullptr);

        // Check D: Present() (Phase G3's plain stretch-blit path) does not throw.
        bool threw = false;
        try { dev.Present(); }
        catch (const std::exception&) { threw = true; }
        check(!threw, "Present() does not throw");

        std::printf("=== %d/%d PASS ===\n", passCount_, 4);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    AsciiOffscreenTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    AsciiOffscreenTargetTest game;
    game.Run();
    return game.getResult();
}
