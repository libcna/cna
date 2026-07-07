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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.10: cna_demo_leaderboard_viewer. LeaderboardReader (CanPageUp/Down, Entries, PageStart,
// TotalLeaderboardSize) plus an explicit demonstration of the always-throws-NotSupportedException
// platform boundary shared by LeaderboardWriter::GetLeaderboard *and*, as this demo's own
// investigation confirmed before writing a line of demo code, LeaderboardReader::PageUp/PageDown
// themselves (already locked in by LeaderboardReaderTest.PageUpThrows/PageDownThrows).
//
// Re-scoped from the plan's literal premise ("Up/Down paging" implying PageUp()/PageDown() do the
// real work): since those always throw, real page-to-page navigation in this demo is done by
// discarding the current LeaderboardReader and constructing a fresh one via CreateInternal at a
// different pageStart over the same fabricated local entry list - the same technique any
// CNA/FNA caller would have to use today, since the real paging methods have no working
// implementation to call. The demo still calls the real PageUp()/PageDown()/GetLeaderboard() once
// each, catching and reporting the expected NotSupportedException, exactly as the plan asked.
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
    void RebuildReaderForCurrentPage();

    Microsoft::Xna::Framework::GamerServices::LeaderboardIdentity identity_;
    std::vector<Microsoft::Xna::Framework::GamerServices::LeaderboardEntry> fullEntries_;
    std::optional<Microsoft::Xna::Framework::GamerServices::LeaderboardReader> reader_;

    static constexpr int kPageSize = 5;
    int currentPage_ = 0;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
};
