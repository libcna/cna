// SPDX-License-Identifier: MS-PL
// Task 730 (sample 5 of 5): minimal 2D-only FNA/XNA sample -- a classic "animated spritesheet"
// demo, specifically targeting SDL_Renderer. Exercises Update()-driven frame-index selection
// feeding SpriteBatch::Draw's sourceRectangle parameter over multiple real frames, proving
// spritesheet-style animation (a very common 2D XNA idiom) works correctly end to end on this
// renderer -- a different code path from Tasks 667-729's single-frame API checks.
//
// Uses a FIXED per-Update() frame-index increment (not real elapsed-time-based animation timing)
// so the outcome is fully deterministic regardless of wall-clock frame speed -- appropriate for a
// "minimal" automated compatibility smoke test.
//
// Spritesheet: 8x4 texture, two adjacent 4x4 frames -- frame 0 (left half, x=0..3) solid red,
// frame 1 (right half, x=4..7) solid green. Each Update() increments a frame counter;
// frameIndex = counter % 2 selects which half of the sheet SpriteBatch::Draw samples via its
// sourceRectangle. After 3 Update() calls, frameIndex = 3 % 2 = 1 -> frame 1 (green) should be
// showing, confirmed by pixel readback of the drawn sprite (NOT the spritesheet source texture
// itself -- proving the sourceRectangle selection genuinely reached the actual draw call).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding), matching every other
// SDL_Renderer pixel-verification test in this project.

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
    constexpr int kFrameSize      = 4;
    constexpr int kUpdateCount    = 3;
}

class SdlAnimatedSpritesheetSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             sheet_;

    int frameCounter_ = 0;
    int updatesRun_   = 0;
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

        // 8x4 spritesheet: left 4x4 = solid red (frame 0), right 4x4 = solid green (frame 1).
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(8 * kFrameSize * 4));
        for (int y = 0; y < kFrameSize; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 8 + static_cast<std::size_t>(x)) * 4;
                const bool isFrame1 = x >= kFrameSize;
                pixels[i + 0] = isFrame1 ? 0   : 255; // R
                pixels[i + 1] = isFrame1 ? 255 : 0;   // G
                pixels[i + 2] = 0;                    // B
                pixels[i + 3] = 255;                  // A
            }
        }
        sheet_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 8, kFrameSize, pixels));
    }

    void Update(GameTime&) override
    {
        if (done_) return;
        ++frameCounter_;
        ++updatesRun_;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        const int frameIndex = frameCounter_ % 2;
        const Rectangle srcRect(frameIndex * kFrameSize, 0, kFrameSize, kFrameSize);
        const Rectangle destRect(2, 2, kFrameSize, kFrameSize);

        sb_->Begin();
        sb_->Draw(*sheet_, destRect, srcRect, Color::White);
        sb_->End();

        if (updatesRun_ < kUpdateCount || done_) return;
        done_ = true;

        check(frameIndex == 1, "After 3 Update() calls, the animation frame index is exactly 1 (green frame)");

        Color px(0, 0, 0, 0);
        Rectangle region(3, 3, 1, 1);
        dev.GetBackBufferData(&region, &px, 0, 1);
        check(px.getRProperty() <= 15 && px.getGProperty() >= 240 && px.getBProperty() <= 15,
              "The drawn sprite genuinely shows frame 1 (green), not frame 0 (red) -- sourceRectangle selection reached the real Draw call");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlAnimatedSpritesheetSample()
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
    SdlAnimatedSpritesheetSample game;
    game.Run();
    return game.getResult();
}
