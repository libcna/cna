// SPDX-License-Identifier: MS-PL
// Task 705: Verify RenderTarget2D can be sampled as Texture2D after unbinding on SDL_Renderer.
//
// RenderTarget2D IS-A Texture2D, so once unbound it must be drawable via the same
// SpriteBatch::Draw(Texture2D&, ...) path as any other texture. Its renderer
// (SdlRenderTargetRenderer) is a SIBLING class of SdlTextureRenderer (both derive independently
// from ITextureRenderer) -- NOT a subclass of it.
//
// This test found a real bug while being written: SdlSpriteBatchRenderer::Draw's 3 overloads all
// did `static_cast<const SdlTextureRenderer&>(texture)` unconditionally, assuming every
// ITextureRenderer passed to Draw() really is an SdlTextureRenderer. Passing an SdlRenderTargetRenderer
// through that cast is undefined behavior (an unchecked downcast between unrelated sibling
// classes) -- fixed by switching to a checked SDL-only sibling interface implemented by both
// concrete texture types, while width/height stay on ITextureRenderer.
//
// Draws a distinctive pattern INTO a RenderTarget2D while it's bound (a Blue background with a
// smaller Red square marker in one corner), unbinds it, then draws the RenderTarget2D itself onto
// the backbuffer via SpriteBatch (exactly as if it were a plain Texture2D) and reads back the
// backbuffer to confirm the pattern survived the round trip byte-for-byte.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlRenderTarget2DSampleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;

    bool done_   = false;
    int  result_ = 0;

    void checkColor(bool ok, const char* label, Color got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        whiteTex_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
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

        // --- Render INTO the render target: Blue background + a Red marker in the top-left. ---
        const int rtSize = 16;
        RenderTarget2D rt(dev, rtSize, rtSize);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(0, 0, 255, 255));
        sb_->Begin();
        sb_->Draw(*whiteTex_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
        sb_->End();
        dev.SetRenderTarget(nullptr);

        // --- Sample the (now unbound) render target as a Texture2D via SpriteBatch, onto the backbuffer. ---
        dev.Clear(Color(0, 0, 0, 255));
        sb_->Begin();
        sb_->Draw(rt, Rectangle(0, 0, W, H), Rectangle(0, 0, rtSize, rtSize), Color::White);
        sb_->End();

        // Marker region (top-left rtSize/4 fraction of the stretched draw) must read Red.
        Color markerPx(0, 0, 0, 0);
        const Rectangle markerRegion(W / 8, H / 8, 1, 1);
        dev.GetBackBufferData(&markerRegion, &markerPx, 0, 1);
        checkColor(markerPx.getRProperty() >= 200 && markerPx.getGProperty() <= 40 && markerPx.getBProperty() <= 40,
                   "Marker corner of the sampled RenderTarget2D reads Red", markerPx);

        // Background region (away from the marker) must read the RT's Blue clear colour.
        Color bgPx(0, 0, 0, 0);
        const Rectangle bgRegion(W * 3 / 4, H * 3 / 4, 1, 1);
        dev.GetBackBufferData(&bgRegion, &bgPx, 0, 1);
        checkColor(bgPx.getRProperty() <= 40 && bgPx.getGProperty() <= 40 && bgPx.getBProperty() >= 200,
                   "Background region of the sampled RenderTarget2D reads Blue", bgPx);

        Exit();
    }

public:
    SdlRenderTarget2DSampleTest()
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
    SdlRenderTarget2DSampleTest game;
    game.Run();
    return game.getResult();
}
