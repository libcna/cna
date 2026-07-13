// SPDX-License-Identifier: MS-PL
// Task 498 (sample 4 of 4 new samples; EasyGL backend): a "layered UI/fade" demo -- a classic
// XNA 2D compositing pattern where an opaque background sprite is drawn first, then a
// semi-transparent white overlay sprite is drawn on top with BlendState::NonPremultiplied, and
// the overlay's alpha is driven by Update()-based game state (a fade-in effect), proving the
// full Update()-state -> SpriteBatch layering -> real alpha compositing -> EasyGL draw pipeline
// works end to end -- a genuinely different scenario from Task 730's SDL_Renderer samples (none
// of which exercise multi-layer alpha blending).
//
// Straight (non-premultiplied) alpha compositing formula: result = fg*a + bg*(1-a), where a is
// the overlay's alpha in [0,1]. Background is solid Blue (0,0,255); overlay is solid White
// (255,255,255) tinted via Color(255,255,255,alphaByte).
//
// Update() logic: frame counter starts at 0; while frame_ < 2, overlay alpha=64 (a=64/255,
// ~25% opacity); once frame_ >= 2, overlay alpha=192 (a=192/255, ~75% opacity) -- a simple
// two-stage fade driven entirely by game state, not a per-frame continuous animation, to keep
// the expected-value math exact.
//
// Expected R/G channel (both fg and bg have R=G contributions that differ: fg R=G=255, bg
// R=G=0, so result R=G=255*a): a=64/255 -> R=G=64; a=192/255 -> R=G=192. The B channel is 255 in
// both textures, so it stays 255 regardless of alpha (used only as a "did compositing touch this
// channel at all" cross-check, not to distinguish the two phases).
//
// Checks: (1) after Update() 1 (frame_==1, low-alpha phase) center pixel R/G is close to 64;
// (2) after Update() 4 (frame_==4, high-alpha phase) center pixel R/G is close to 192; (3) B
// stays close to 255 in both phases (fg/bg blue channels are identical); (4) the two phases'
// R/G values are genuinely different, proving live Update()-driven alpha reached the render.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    constexpr int kFrameCount = 4;
    constexpr int kFadeUpAtFrame = 2;
    constexpr std::uint8_t kLowAlpha = 64;
    constexpr std::uint8_t kHighAlpha = 192;

    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
}

class LayeredBlendSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> bg_, overlay_;

    int frame_ = 0;
    bool done_ = false;
    int pass_ = 0, fail_ = 0;
    Color capturedFrame1_{0, 0, 0, 0};
    bool capturedFrame1Set_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle r(kSize / 2, kSize / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&r, &c, 0, 1);
        return c;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        Color blue(0, 0, 255, 255);
        bg_ = std::make_unique<Texture2D>(dev, 1, 1);
        bg_->SetData(&blue, 1);

        Color white(255, 255, 255, 255);
        overlay_ = std::make_unique<Texture2D>(dev, 1, 1);
        overlay_->SetData(&white, 1);
    }

    void Update(GameTime&) override
    {
        if (done_) return;
        ++frame_;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));

        const std::uint8_t alpha = (frame_ < kFadeUpAtFrame) ? kLowAlpha : kHighAlpha;

        // Task 956 fix: pass the intended BlendState directly to SpriteBatch::Begin() instead of
        // setting it on GraphicsDevice right before calling the no-arg Begin() overload -- the
        // no-arg overload's own implicit BlendState::AlphaBlend argument immediately re-applies
        // and clobbers whatever was just set (SpriteBatch::Begin() always calls
        // GraphicsDevice.setBlendStateProperty(blendState) itself, see SpriteBatch.cpp). This test
        // previously only passed because EasyGLSpriteBatchBackend::Begin() separately hardcoded
        // SrcAlpha/OneMinusSrcAlpha blend factors regardless of any of this -- coincidentally the
        // same factors BlendState::NonPremultiplied needs -- masking the fact that the explicit
        // NonPremultiplied request below was never actually reaching the GPU.
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(*bg_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        sb_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        sb_->Draw(*overlay_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1),
                  Color(255, 255, 255, static_cast<int>(alpha)));
        sb_->End();

        if (frame_ == 1 && !capturedFrame1Set_)
        {
            capturedFrame1_ = readCenter(dev);
            capturedFrame1Set_ = true;
            check(closeTo(capturedFrame1_.getRProperty(), kLowAlpha, 6) &&
                  closeTo(capturedFrame1_.getGProperty(), kLowAlpha, 6),
                  "Frame 1 (low-alpha fade phase): composited R/G close to the expected ~64");
        }

        if (frame_ < kFrameCount || done_) return;
        done_ = true;

        const Color finalPx = readCenter(dev);
        check(closeTo(finalPx.getRProperty(), kHighAlpha, 6) &&
              closeTo(finalPx.getGProperty(), kHighAlpha, 6),
              "Frame 4 (high-alpha fade phase): composited R/G close to the expected ~192, after Update()-driven fade");
        check(finalPx.getBProperty() >= 240,
              "Blue channel stays ~255 in both phases (fg/bg blue channels are identical)");
        check(capturedFrame1Set_ && !closeTo(finalPx.getRProperty(), capturedFrame1_.getRProperty(), 20),
              "Composited color genuinely changed between the two fade phases (live state, not baked-in)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    LayeredBlendSample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    LayeredBlendSample game;
    game.Run();
    return game.getResult();
}
