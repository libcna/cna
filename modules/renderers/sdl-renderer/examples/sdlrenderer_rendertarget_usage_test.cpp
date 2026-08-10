// SPDX-License-Identifier: MS-PL
// Task 706: Verify RenderTargetUsage::DiscardContents vs PreserveContents on SDL_Renderer.
//
// GraphicsDevice::SetRenderTarget(RenderTarget2D*) auto-clears (Color(0,0,0,255), matching FNA's
// own "discard" semantics) on EVERY bind when the target's own RenderTargetUsage is
// DiscardContents (see Task 704's fix, which corrected this auto-clear to skip the depth-buffer
// component for depth-less targets) -- but does nothing extra when the usage is PreserveContents,
// relying on the fact that an SDL_TEXTUREACCESS_TARGET texture's own backing store is a normal,
// persistent GPU texture whose content survives being unbound and rebound unless something
// explicitly overwrites it.
//
// This test proves the two usages are genuinely observably different: create one RenderTarget2D
// of each usage, draw a distinctive colour into each once, unbind both, then rebind each WITHOUT
// drawing or clearing anything new this time, and immediately unbind again. Sampling each target
// afterward must show:
//   - DiscardContents: the ORIGINAL colour is GONE -- rebinding auto-cleared it to black.
//   - PreserveContents: the ORIGINAL colour is STILL there -- rebinding did not touch it.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = both checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlRenderTargetUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    bool done_   = false;
    int  result_ = 0;

    void checkColor(bool ok, const char* label, Color got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (!ok) result_ = 1;
    }

    // Samples the centre pixel of `rt` by drawing it onto the (freshly cleared) backbuffer.
    Color SampleRenderTarget(RenderTarget2D& rt, int W, int H)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        sb_->Begin();
        sb_->Draw(rt, Rectangle(0, 0, W, H), Rectangle(0, 0, rt.getWidthProperty(), rt.getHeightProperty()), Color::White);
        sb_->End();
        Color pixel(0, 0, 0, 0);
        const Rectangle region(W / 2, H / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        dev.setBlendStateProperty(BlendState::Opaque);
        const int rtSize = 8;

        RenderTarget2D rtDiscard(dev, rtSize, rtSize, false, SurfaceFormat::Color,
                                 DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        RenderTarget2D rtPreserve(dev, rtSize, rtSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        // Draw a distinctive Green into each target once.
        dev.SetRenderTarget(&rtDiscard);
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(&rtPreserve);
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetRenderTarget(nullptr);

        // Rebind each WITHOUT drawing or clearing anything new, then immediately unbind again --
        // isolates the auto-clear-on-bind behaviour itself.
        dev.SetRenderTarget(&rtDiscard);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(&rtPreserve);
        dev.SetRenderTarget(nullptr);

        const Color discardResult = SampleRenderTarget(rtDiscard, W, H);
        checkColor(discardResult.getRProperty() <= 20 && discardResult.getGProperty() <= 20 && discardResult.getBProperty() <= 20,
                   "DiscardContents: original Green is GONE after an untouched rebind (auto-cleared to black)",
                   discardResult);

        const Color preserveResult = SampleRenderTarget(rtPreserve, W, H);
        checkColor(preserveResult.getGProperty() >= 230 && preserveResult.getRProperty() <= 20 && preserveResult.getBProperty() <= 20,
                   "PreserveContents: original Green is STILL there after an untouched rebind",
                   preserveResult);

        Exit();
    }

public:
    SdlRenderTargetUsageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlRenderTargetUsageTest game;
    game.Run();
    return game.getResult();
}
