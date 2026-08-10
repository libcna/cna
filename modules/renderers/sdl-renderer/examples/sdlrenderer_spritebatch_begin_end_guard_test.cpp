// SPDX-License-Identifier: MS-PL
// Task 677: SDL_Renderer SpriteBatch Begin/End sequencing guard integration test.
//
// Tasks 161-166's mock-renderer unit tests (SpriteBatchTests.cpp) already thoroughly cover these
// guards at the shared, renderer-agnostic SpriteBatch.cpp layer (`begun` flag, checked before any
// renderer_ call in every case) using a no-renderer SpriteBatch instance. This task instead
// confirms the guards still hold end-to-end with a REAL SDL_Renderer-backed GraphicsDevice/
// SpriteBatch/Texture2D -- i.e. that no real renderer call happens (or throws for some unrelated
// reason) before the sequencing check fires.
//
// Verifies:
//   1. End() before Begin() throws.
//   2. Draw() before Begin() throws.
//   3. A second Begin() without an intervening End() throws.
//   4. Begin()/Draw()/End() succeeds without throwing (sanity: the real renderer engages fine).
//   5. Begin()/End()/Begin()/End() (re-entrant sequencing) succeeds without throwing.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlSpriteBatchBeginEndGuardTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_  = std::make_unique<SpriteBatch>(dev);
        tex_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color px = Color::White;
        tex_->SetData(&px, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));

        // 1. End() before Begin() throws.
        bool threw = false;
        try { sb_->End(); } catch (const std::exception&) { threw = true; }
        check(threw, "End() before Begin() throws");

        // 2. Draw() before Begin() throws.
        threw = false;
        try { sb_->Draw(*tex_, Vector2(0.0f, 0.0f), Color::White); }
        catch (const std::exception&) { threw = true; }
        check(threw, "Draw() before Begin() throws");

        // 3. A second Begin() without an intervening End() throws.
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        threw = false;
        try { sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr); }
        catch (const std::exception&) { threw = true; }
        check(threw, "Second Begin() without End() throws");
        sb_->End(); // clean up the still-open batch from step 3's first Begin().

        // 4. Begin()/Draw()/End() succeeds -- real renderer engages without throwing.
        bool okNoThrow = true;
        try
        {
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
            sb_->Draw(*tex_, Vector2(0.0f, 0.0f), Color::White);
            sb_->End();
        }
        catch (const std::exception&) { okNoThrow = false; }
        check(okNoThrow, "Begin()/Draw()/End() does not throw");

        // 5. Begin()/End()/Begin()/End() (re-entrant sequencing) succeeds.
        okNoThrow = true;
        try
        {
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
            sb_->End();
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
            sb_->End();
        }
        catch (const std::exception&) { okNoThrow = false; }
        check(okNoThrow, "Begin()/End()/Begin()/End() does not throw");

        Exit();
    }

public:
    SdlSpriteBatchBeginEndGuardTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteBatchBeginEndGuardTest game;
    game.Run();
    return game.getResult();
}
