#include "LeaderboardGame.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"

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

void LeaderboardGame::Initialize()
{
    Game::Initialize();

    // Task 4.3/4.4 (plans/plan_net.md Phase 4): real persistence needs a real, distinct app-data
    // location - this demo uses its own dedicated app name so its 20 synthetic gamertags never
    // collide with any other CNA app's own local GamerServices store.
    Storage::StorageDevice::SetAppNameEXT("CnaDemoLeaderboardViewer");

    // 20 synthetic gamers, published as "signed in" (LeaderboardReader::Read only attaches to
    // currently signed-in gamers - see its own doc comment on this documented, honest local-only
    // limitation), each given a real rating through LeaderboardWriter - the real "write path"
    // (LeaderboardEntry::setRatingProperty() persists on every call - see its own doc comment for
    // why that, not a separate commit method, is the real XNA-faithful trigger).
    //
    // Each gamer is constructed with `new SignedInGamer(SignedInGamer::CreateInternal(tag))`, not
    // a plain push_back of the by-value CreateInternal() result - see syntheticGamers_'s own doc
    // comment in LeaderboardGame.hpp for why: this exact spelling is what makes C++17's mandatory
    // prvalue-elision rule construct the object directly at its final heap address, with no
    // intermediate move ever touching the Gamer subobject's self-captured LeaderboardWriter owner
    // pointer.
    std::vector<SignedInGamer*> signedInPointers;
    for (int i = 0; i < kTotalEntries; ++i)
    {
        char tag[32];
        std::snprintf(tag, sizeof(tag), "Player%02d", i + 1);
        syntheticGamers_.push_back(
            std::unique_ptr<SignedInGamer>(new SignedInGamer(SignedInGamer::CreateInternal(tag)))
        );
    }
    for (const std::unique_ptr<SignedInGamer>& gamer : syntheticGamers_)
    {
        signedInPointers.push_back(gamer.get());
    }
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal(signedInPointers)
    ));

    for (int i = 0; i < kTotalEntries; ++i)
    {
        const long long rating = 1000 - i * 10;
        syntheticGamers_[static_cast<std::size_t>(i)]->getLeaderboardWriterProperty()
            .GetLeaderboard(identity_)->setRatingProperty(rating);
    }

    reader_ = LeaderboardReader::Read(identity_, 0, kPageSize);
    std::printf("[Leaderboard] Wrote %d real ratings via LeaderboardWriter, read back "
                "totalOnFirstPage=%d via a real LeaderboardReader::Read().\n",
                kTotalEntries, reader_->getEntriesProperty().getCountProperty());
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
    // Task 4.4 (plans/plan_net.md Phase 4): PageDown()/PageUp() are now real - they mutate reader_ in
    // place (reslicing the already-cached full leaderboard, no new disk read), so no separate
    // "rebuild the reader" step is needed anymore.
    KeyboardState keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Down) && !previousKeys_.IsKeyDown(Keys::Down) && reader_->getCanPageDownProperty())
    {
        ++currentPage_;
        reader_->PageDown();
    }
    if (keys.IsKeyDown(Keys::Up) && !previousKeys_.IsKeyDown(Keys::Up) && reader_->getCanPageUpProperty())
    {
        --currentPage_;
        reader_->PageUp();
    }
    previousKeys_ = keys;

    // Smoke-test mode has no real keyboard driving it - deterministically page down every 30
    // frames while possible (matching the established Phase 15 deterministic-nudge convention).
    // Guarded by > 0, not >= 0: once smokeFramesLeft_ reaches 0 it stops decrementing (see the
    // block below), so an >= 0 check here would keep re-triggering every subsequent frame -
    // Exit() does not halt Update() immediately (Task 15.14's own discovery of this exact bug
    // class - naturally bounded here by CanPageDown becoming false, but inconsistent).
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 30 == 0 && reader_->getCanPageDownProperty())
    {
        ++currentPage_;
        reader_->PageDown();
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
