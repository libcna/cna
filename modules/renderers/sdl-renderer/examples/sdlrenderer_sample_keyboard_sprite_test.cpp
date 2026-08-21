// SPDX-License-Identifier: MS-PL
// Task 730 (sample 3 of 5): minimal 2D-only FNA/XNA sample -- a classic "keyboard-driven sprite"
// demo, specifically targeting SDL_Renderer. Exercises Keyboard::GetState()-driven Update() logic
// feeding into SpriteBatch rendering over multiple real frames, a different code path from
// Tasks 667-729's narrow API checks (none of which drive gameplay logic off input state).
//
// Since this runs headlessly under Xvfb via ctest (no real keyboard), the real platform is wrapped
// by the shared canned-keyboard test decorator. The platform remains real for windowing, timing,
// and graphics while its once-per-frame keyboard snapshot holds the Right arrow down. Each
// Update() moves the sprite right by a fixed logical-pixel increment as long as
// Keyboard::GetState().IsKeyDown(Keys::Right) is true, proving the full
// input-state -> Update() -> Draw() -> SDL_Renderer pipeline works end to end.
//
// Sprite: 4x4 solid white square, starts at (0,4), moves +3 logical px/Update() while Right is
// held. After 4 Update() calls the expected X position is 0 + 4*3 = 12 (backbuffer is 16 wide,
// so 12+4=16 stays exactly in bounds -- no clamping needed, keeping the expected-position math
// simple and unambiguous).
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
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "CNA/Platform/CannedKeyboard.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    constexpr int kBackbufferSize = 16;
    constexpr int kSpriteSize     = 4;
    constexpr int kFrameCount     = 4;
    constexpr int kStartX = 0, kStartY = 4;
    constexpr int kMovePerFrame = 3;
}

class SdlKeyboardSpriteSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             sprite_;

    int spriteX_ = kStartX;
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
        if (done_ || frame_ >= kFrameCount) return;
        if (Keyboard::GetState().IsKeyDown(Keys::Right))
            spriteX_ += kMovePerFrame;
        ++frame_;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin();
        sb_->Draw(*sprite_, Rectangle(spriteX_, kStartY, kSpriteSize, kSpriteSize),
                  Rectangle(0, 0, kSpriteSize, kSpriteSize), Color::White);
        sb_->End();

        if (frame_ < kFrameCount || done_) return;
        done_ = true;

        const int expectedX = kStartX + kMovePerFrame * kFrameCount;
        check(spriteX_ == expectedX,
              "Sprite moved the exact distance driven by simulated Keyboard::GetState() over 4 Update() cycles");

        Color px(0, 0, 0, 0);
        Rectangle region(expectedX + 1, kStartY + 1, 1, 1);
        dev.GetBackBufferData(&region, &px, 0, 1);
        check(px.getRProperty() >= 240 && px.getGProperty() >= 240 && px.getBProperty() >= 240,
              "Sprite is genuinely rendered at the keyboard-driven position, not just tracked internally");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    explicit SdlKeyboardSpriteSample(std::unique_ptr<CNA::Platform::IPlatform> platform)
        : Game(std::move(platform))
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
    auto platform = std::make_unique<CNA::Platform::Testing::CannedKeyboardPlatform>();
    platform->Canned().SetPending({CNA::Platform::KeyCode::Right});
    SdlKeyboardSpriteSample game(std::move(platform));
    game.Run();
    return game.getResult();
}
