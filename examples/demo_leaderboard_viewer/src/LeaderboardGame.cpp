#include "LeaderboardGame.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "System/NotSupportedException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::GamerServices;

namespace
{
    std::unique_ptr<SpriteFont> MakeSimpleFont(GraphicsDevice& device)
    {
        const std::vector<uint8_t> px = {255, 255, 255, 255};
        Texture2D atlas = Texture2D::CreateFromPixels(device, 1, 1, px);

        std::vector<SharpRuntime::charcs> chars;
        std::vector<Rectangle> bounds;
        std::vector<Rectangle> cropping;
        std::vector<Vector3> kerning;
        for (char c = 32; c < 127; ++c)
        {
            chars.push_back(static_cast<SharpRuntime::charcs>(c));
            bounds.push_back(Rectangle(0, 0, 1, 1));
            cropping.push_back(Rectangle(0, 0, 8, 14));
            kerning.push_back(Vector3(0.0f, 8.0f, 0.0f));
        }

        return std::make_unique<SpriteFont>(atlas, bounds, cropping, chars, 16, 1.0f, kerning,
                                             static_cast<SharpRuntime::charcs>(' '));
    }

    constexpr int kTotalEntries = 20;
}

LeaderboardGame::LeaderboardGame()
    : identity_(LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime))
{
}

void LeaderboardGame::RebuildReaderForCurrentPage()
{
    // LeaderboardReader's real constructor loop is `for (i = pageStart; i < size && i <
    // entryCache.size(); i++)` (Task 10.6's confirmed, FNA-faithful loop-bound quirk) - "size" is
    // an absolute upper index bound, not a page length, for any page beyond the first. Passing
    // the FULL fabricated entry list plus [pageStart, pageStart+kPageSize) as start/size is the
    // correct way to get exactly kPageSize real entries back for page N.
    const int start = currentPage_ * kPageSize;
    reader_ = LeaderboardReader::CreateInternal(identity_, start, start + kPageSize, fullEntries_, false);
}

void LeaderboardGame::Initialize()
{
    Game::Initialize();

    for (int i = 0; i < kTotalEntries; ++i)
    {
        const int ranking = i + 1;
        const long long rating = 1000 - i * 10;
        fullEntries_.push_back(LeaderboardEntry::CreateInternal(nullptr, rating, ranking));
    }
    RebuildReaderForCurrentPage();

    // Real API calls, once each, proving the always-throws boundary is real rather than assumed.
    try
    {
        reader_->PageDown();
        std::printf("[Leaderboard] PageDown() unexpectedly did not throw.\n");
    }
    catch (const System::NotSupportedException&)
    {
        std::printf("[Leaderboard] PageDown() threw NotSupportedException as expected.\n");
    }
    try
    {
        reader_->PageUp();
        std::printf("[Leaderboard] PageUp() unexpectedly did not throw.\n");
    }
    catch (const System::NotSupportedException&)
    {
        std::printf("[Leaderboard] PageUp() threw NotSupportedException as expected.\n");
    }
    try
    {
        LeaderboardWriter writer;
        [[maybe_unused]] LeaderboardEntry* unused = writer.GetLeaderboard(identity_);
        std::printf("[Leaderboard] LeaderboardWriter::GetLeaderboard() unexpectedly did not throw.\n");
    }
    catch (const System::NotSupportedException&)
    {
        std::printf("[Leaderboard] LeaderboardWriter::GetLeaderboard() threw NotSupportedException "
                    "as expected.\n");
    }

    // The 3 real throwing calls above may have left the reader in an unspecified state (they're
    // stubs, not tested for post-throw side effects) - rebuild fresh before actually displaying it.
    RebuildReaderForCurrentPage();
}

void LeaderboardGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void LeaderboardGame::Update(GameTime& /*gameTime*/)
{
    KeyboardState keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Down) && !previousKeys_.IsKeyDown(Keys::Down) && reader_->getCanPageDownProperty())
    {
        ++currentPage_;
        RebuildReaderForCurrentPage();
    }
    if (keys.IsKeyDown(Keys::Up) && !previousKeys_.IsKeyDown(Keys::Up) && reader_->getCanPageUpProperty())
    {
        --currentPage_;
        RebuildReaderForCurrentPage();
    }
    previousKeys_ = keys;

    // Smoke-test mode has no real keyboard driving it - deterministically page down every 30
    // frames while possible (matching the established Phase 15 deterministic-nudge convention).
    if (smokeFramesLeft_ >= 0 && smokeFramesLeft_ % 30 == 0 && reader_->getCanPageDownProperty())
    {
        ++currentPage_;
        RebuildReaderForCurrentPage();
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Leaderboard] Smoke test complete: currentPage=%d pageStart=%d "
                        "entriesOnPage=%d canPageDown=%s canPageUp=%s\n",
                        currentPage_, reader_->getPageStartProperty(),
                        reader_->getEntriesProperty().getCountProperty(),
                        reader_->getCanPageDownProperty() ? "true" : "false",
                        reader_->getCanPageUpProperty() ? "true" : "false");
            Exit();
        }
    }
}

void LeaderboardGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();
    char header[128];
    std::snprintf(header, sizeof(header), "Leaderboard Viewer (Up/Down to page) - page %d, pageStart=%d",
                  currentPage_, reader_->getPageStartProperty());
    spriteBatch_->DrawString(*font_, header, Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));

    float y = 48.0f;
    const auto entries = reader_->getEntriesProperty();
    for (int i = 0; i < entries.getCountProperty(); ++i)
    {
        const LeaderboardEntry& entry = entries[i];
        char line[128];
        std::snprintf(line, sizeof(line), "#%-3d  rating=%lld", entry.getRankingEXTProperty(),
                      entry.getRatingProperty());
        spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255));
        y += 18.0f;
    }
    if (entries.getCountProperty() == 0)
    {
        spriteBatch_->DrawString(*font_, "(no entries on this page)", Vector2(16.0f, y),
                                  Color(150, 150, 150, 255));
    }

    char footer[128];
    std::snprintf(footer, sizeof(footer), "CanPageUp=%s CanPageDown=%s",
                  reader_->getCanPageUpProperty() ? "true" : "false",
                  reader_->getCanPageDownProperty() ? "true" : "false");
    spriteBatch_->DrawString(*font_, footer, Vector2(16.0f, y + 20.0f), Color(180, 180, 180, 255));

    spriteBatch_->End();
}
