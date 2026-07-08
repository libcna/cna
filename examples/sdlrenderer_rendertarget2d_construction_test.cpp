// SPDX-License-Identifier: MS-PL
// Task 704: Audit RenderTarget2D construction on SDL_Renderer.
//
// SdlRenderTargetBackend's constructor creates its backing texture via
// SDL_CreateTexture(..., SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h) --
// SDL_TEXTUREACCESS_TARGET is the one access mode SDL requires for a texture to be a valid
// SDL_SetRenderTarget() destination. This test audits two things:
//   1. Property wiring: RenderTarget2D's own constructor (Microsoft::Xna::Framework::Graphics::
//      RenderTarget2D.cpp) stores DepthStencilFormat/RenderTargetUsage verbatim (matching FNA's
//      real semantics -- FNA's own constructor just assigns the preferred value directly, no
//      device-capability clamping for either), while MultiSampleCount reflects the backend's real,
//      device-clamped value (FNA: FNA3D_GetMaxMultiSampleCount) -- SDL_Renderer's 2D blit pipeline
//      has no MSAA support at all (IRenderTargetBackend::GetMultiSampleCount defaults to 0 and
//      SdlRenderTargetBackend never overrides it), so MultiSampleCount must always report 0
//      regardless of what was requested.
//   2. Bind/draw/unbind isolation: SDL_SetRenderTarget genuinely redirects drawing to the target's
//      own off-screen texture -- content drawn while the RT is bound must NOT leak onto the real
//      backbuffer, and drawing must correctly resume hitting the backbuffer once unbound
//      (SetRenderTarget(nullptr)).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
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

class SdlRenderTarget2DConstructionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;
    std::unique_ptr<Texture2D>             redTex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

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
        redTex_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // --- Part 1: property wiring, 2-arg (default) constructor. ---
        RenderTarget2D rtDefault(dev, 32, 16);
        check(rtDefault.getWidthProperty() == 32 && rtDefault.getHeightProperty() == 16,
              "2-arg ctor: Width/Height match requested");
        check(rtDefault.getDepthStencilFormatProperty() == DepthFormat::None,
              "2-arg ctor: DepthStencilFormat defaults to None");
        check(rtDefault.getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents,
              "2-arg ctor: RenderTargetUsage defaults to DiscardContents");
        check(rtDefault.getMultiSampleCountProperty() == 0,
              "2-arg ctor: MultiSampleCount defaults to 0");

        // --- Part 2: property wiring, full 8-arg constructor with non-default values. ---
        RenderTarget2D rtFull(dev, 32, 16, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
        check(rtFull.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              "8-arg ctor: DepthStencilFormat echoes the requested value verbatim (FNA: no device clamping)");
        check(rtFull.getRenderTargetUsageProperty() == RenderTargetUsage::PreserveContents,
              "8-arg ctor: RenderTargetUsage echoes the requested value verbatim");
        check(rtFull.getMultiSampleCountProperty() == 0,
              "8-arg ctor: MultiSampleCount is device-clamped to 0 (SDL_Renderer has no MSAA support), not the requested 4");

        // --- Part 3: bind/draw/unbind isolation. ---
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        const Rectangle markerDrawRegion(2, 2, 4, 4);
        const Rectangle markerSampleRegion(3, 3, 1, 1);
        sb_->Begin();
        sb_->Draw(*whiteTex_, markerDrawRegion, Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        Color markerBeforeBind(0, 0, 0, 0);
        dev.GetBackBufferData(&markerSampleRegion, &markerBeforeBind, 0, 1);
        checkColor(markerBeforeBind.getRProperty() >= 240 && markerBeforeBind.getGProperty() >= 240
                       && markerBeforeBind.getBProperty() >= 240,
                   "Backbuffer marker is White before binding the render target", markerBeforeBind);

        RenderTarget2D rt(dev, W, H);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(0, 0, 255, 255));
        sb_->Begin();
        sb_->Draw(*redTex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
        dev.SetRenderTarget(nullptr);

        Color markerAfterUnbind(0, 0, 0, 0);
        dev.GetBackBufferData(&markerSampleRegion, &markerAfterUnbind, 0, 1);
        checkColor(markerAfterUnbind.getRProperty() >= 240 && markerAfterUnbind.getGProperty() >= 240
                       && markerAfterUnbind.getBProperty() >= 240,
                   "Backbuffer marker is UNCHANGED (still White) after the render target was bound, drawn into, and unbound",
                   markerAfterUnbind);

        // Confirm drawing resumes hitting the backbuffer once unbound.
        sb_->Begin();
        sb_->Draw(*redTex_, Rectangle(W - 6, H - 6, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
        Color postUnbindDraw(0, 0, 0, 0);
        const Rectangle postUnbindRegion(W - 4, H - 4, 1, 1);
        dev.GetBackBufferData(&postUnbindRegion, &postUnbindDraw, 0, 1);
        checkColor(postUnbindDraw.getRProperty() >= 240 && postUnbindDraw.getBProperty() <= 15,
                   "A draw issued AFTER unbinding lands on the backbuffer (not silently lost/still routed to the RT)",
                   postUnbindDraw);

        Exit();
    }

public:
    SdlRenderTarget2DConstructionTest()
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
    SdlRenderTarget2DConstructionTest game;
    game.Run();
    return game.getResult();
}
