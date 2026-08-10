// SPDX-License-Identifier: MS-PL
// Task 730 (sample 2 of 5): minimal 2D-only FNA/XNA sample -- a classic "bouncing sprite" demo,
// specifically targeting SDL_Renderer. Unlike Tasks 667-729's narrow single-Draw()-call API
// checks, this exercises a REAL multi-frame Update()+Draw() game loop: a small sprite moves each
// frame and bounces off the backbuffer edges, proving genuine gameplay-shaped code (not just
// isolated feature calls) works correctly end-to-end on this renderer.
//
// Uses a FIXED per-Update() logical increment (not real GameTime-elapsed physics) so the outcome
// is fully deterministic regardless of wall-clock frame timing -- a deliberate simplification
// appropriate for a "minimal" sample used as an automated compatibility smoke test.
//
// Sprite: 4x4 solid white square. Backbuffer: 16x16. Start position (2,2), velocity (+3,+2)
// logical px/Update(). After 5 Update() calls the naive (unbounced) trajectory would be
// (17,12) -- off the right edge (max valid left-edge X is 16-4=12) -- so a bounce on X is
// guaranteed within these 5 frames, proving the bounce-reversal logic actually runs, not just
// straight-line movement. The exact expected final position is independently computed in
// ComputeExpectedPosition() below (mirroring the same bounce formula) and compared against the
// sprite's actual rendered position via pixel readback.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode does not map
// logical pixels 1:1 to physical ones.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackbufferSize = 16;
    constexpr int kSpriteSize     = 4;
    constexpr int kFrameCount     = 5;
    constexpr int kStartX = 2, kStartY = 2;
    constexpr int kVelX = 3, kVelY = 2;

    // Mirrors the exact bounce formula used in Update() below -- computed independently here so
    // the test doesn't just check "whatever Update() produced" against itself.
    void ComputeExpectedPosition(int& x, int& y)
    {
        x = kStartX; y = kStartY;
        int vx = kVelX, vy = kVelY;
        const int maxPos = kBackbufferSize - kSpriteSize;
        for (int i = 0; i < kFrameCount; ++i)
        {
            x += vx; y += vy;
            if (x < 0) { x = 0; vx = -vx; }
            if (x > maxPos) { x = maxPos; vx = -vx; }
            if (y < 0) { y = 0; vy = -vy; }
            if (y > maxPos) { y = maxPos; vy = -vy; }
        }
    }
}

class SdlBouncingSpriteSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             sprite_;

    int spriteX_ = kStartX, spriteY_ = kStartY;
    int velX_ = kVelX, velY_ = kVelY;
    int frame_ = 0;
    int pass_ = 0, fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        sprite_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, kSpriteSize, kSpriteSize,
                std::vector<std::uint8_t>(static_cast<std::size_t>(kSpriteSize * kSpriteSize * 4), 255)));
    }

    void Update(GameTime&) override
    {
        if (done_) return;

        const int maxPos = kBackbufferSize - kSpriteSize;
        spriteX_ += velX_;
        spriteY_ += velY_;
        if (spriteX_ < 0)      { spriteX_ = 0;      velX_ = -velX_; }
        if (spriteX_ > maxPos) { spriteX_ = maxPos; velX_ = -velX_; }
        if (spriteY_ < 0)      { spriteY_ = 0;      velY_ = -velY_; }
        if (spriteY_ > maxPos) { spriteY_ = maxPos; velY_ = -velY_; }
        ++frame_;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin();
        sb_->Draw(*sprite_, Rectangle(spriteX_, spriteY_, kSpriteSize, kSpriteSize),
                  Rectangle(0, 0, kSpriteSize, kSpriteSize), Color::White);
        sb_->End();

        if (frame_ < kFrameCount || done_) return;
        done_ = true;

        int expectedX, expectedY;
        ComputeExpectedPosition(expectedX, expectedY);
        check(expectedX != kStartX + kVelX * kFrameCount,
              "A bounce genuinely occurred within the 5 frames (unbounced trajectory would differ)");

        Color px(0, 0, 0, 0);
        Rectangle region(expectedX + 1, expectedY + 1, 1, 1);
        dev.GetBackBufferData(&region, &px, 0, 1);
        check(px.getRProperty() >= 240 && px.getGProperty() >= 240 && px.getBProperty() >= 240,
              "Sprite is rendered at the exact independently-computed post-bounce position after 5 Update()+Draw() cycles");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlBouncingSpriteSample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferSize);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlBouncingSpriteSample game;
    game.Run();
    return game.getResult();
}
