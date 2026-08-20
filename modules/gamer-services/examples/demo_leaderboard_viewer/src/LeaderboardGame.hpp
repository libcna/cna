#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.10 (re-scoped by plans/plan_net.md Task 4.3/4.4, 2026-07-16): cna_demo_leaderboard_viewer.
// LeaderboardWriter::GetLeaderboard/LeaderboardEntry::setRatingProperty and
// LeaderboardReader::Read/PageUp/PageDown are now real, local-disk-backed implementations (no
// longer the always-NotSupportedException stubs this demo originally had to work around) -
// publishes several synthetic SignedInGamer objects as "signed in" (LeaderboardReader::Read only
// attaches to currently signed-in gamers - see its own doc comment), gives each a real rating via
// LeaderboardWriter, then pages through them with the real PageUp()/PageDown().
class LeaderboardGame : public Microsoft::Xna::Framework::Game
{
public:
    LeaderboardGame();

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    Microsoft::Xna::Framework::GamerServices::LeaderboardIdentity identity_;
    // Task 4.3 (plans/plan_net.md Phase 4) post-fix: heap-allocated, not a std::vector<SignedInGamer> by
    // value - Gamer::leaderboardWriter_ captures `this` at construction time (see Gamer.hpp's own
    // doc comment on that member for why), so relocating an already-constructed Gamer via any
    // copy/move - which is exactly what push_back(prvalue) into a growing-by-value vector does,
    // reservation or not - leaves that captured pointer dangling. unique_ptr elements are
    // constructed once at a permanent heap address and never moved again.
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::GamerServices::SignedInGamer>> syntheticGamers_;
    std::optional<Microsoft::Xna::Framework::GamerServices::LeaderboardReader> reader_;

    static constexpr int kPageSize = 5;
    int currentPage_ = 0;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
};
