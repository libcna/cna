#include "AchievementGame.hpp"

#include <algorithm>
#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "System/DateTime.hpp"

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

    struct TileDef
    {
        const char* key;
        const char* name;
        const char* description;
        bool displayBeforeEarned;
    };

    // Fixed demo content, not derived from GetAchievements() (which is always empty on this
    // platform - see the header's own honest-scope note).
    constexpr TileDef kTileDefs[6] = {
        {"first_steps", "First Steps", "Complete the tutorial.", true},
        {"speed_demon", "Speed Demon", "Finish a level in under 60 seconds.", true},
        {"collector", "Collector", "Find every hidden item.", true},
        {"no_hit_run", "Flawless", "Complete a level without taking damage.", true},
        {"secret_ending", "???", "A secret achievement.", false},
        {"completionist", "Completionist", "Earn every other achievement.", true},
    };
}

AchievementGame::AchievementGame()
{
    gamerServicesComponent_ = new GamerServicesComponent(*this);
    getComponentsProperty().Add(gamerServicesComponent_);
}

AchievementGame::~AchievementGame()
{
    delete gamerServicesComponent_;
}

void AchievementGame::Initialize()
{
    Game::Initialize();

    localGamer_ = (*Gamer::getSignedInGamersProperty())[0];

    for (const TileDef& def : kTileDefs)
    {
        tiles_.push_back(Achievement::CreateInternal(def.key, def.name, def.description,
                                                       def.displayBeforeEarned, false, System::DateTime{}));
    }
    flashTimers_.assign(tiles_.size(), 0.0f);
}

void AchievementGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void AchievementGame::AwardTile(std::size_t index)
{
    if (index >= tiles_.size() || tiles_[index].getIsEarnedProperty())
    {
        return;
    }

    // Task 4.5 (plans/plan_net.md Phase 4): real API call - persists to the local GamerServices store,
    // keyed by gamertag. Real XNA's AwardAchievement only ever takes a key, no name/description/
    // score, so GetAchievements() below reflects only key+earned+earnedDateTime for real; this
    // demo's own tiles_ grid (with the real name/description/displayBeforeEarned this local store
    // has no source of truth for) stays the actual source of truth for what's displayed.
    localGamer_->AwardAchievement(tiles_[index].getKeyProperty());

    // Achievement has no isEarned setter (it's immutable once constructed) - "earning" a tile
    // means constructing a brand-new value with earned=true and swapping it into our own local
    // grid, which is the demo's actual source of truth for what's displayed.
    const TileDef& def = kTileDefs[index];
    tiles_[index] = Achievement::CreateInternal(def.key, def.name, def.description,
                                                 def.displayBeforeEarned, true, System::DateTime::getNowProperty());
    flashTimers_[index] = 1.0f;

    AchievementCollection real = localGamer_->GetAchievements();
    std::printf("[Achievements] Awarded \"%s\" locally. Real GetAchievements() count=%d "
                "(now real disk-backed persistence - survives across process runs for this "
                "gamertag; Name/Description/GamerScore stay empty since AwardAchievement's real "
                "API surface never carries them)\n",
                def.name, real.getCountProperty());
}

void AchievementGame::Update(GameTime& gameTime)
{
    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

    KeyboardState keys = Keyboard::GetState();
    const Keys numberKeys[6] = {Keys::D1, Keys::D2, Keys::D3, Keys::D4, Keys::D5, Keys::D6};
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (keys.IsKeyDown(numberKeys[i]) && !previousKeys_.IsKeyDown(numberKeys[i]))
        {
            AwardTile(i);
        }
    }
    previousKeys_ = keys;

    for (float& timer : flashTimers_)
    {
        if (timer > 0.0f)
        {
            timer = std::max(0.0f, timer - dt);
        }
    }

    // Smoke-test mode has no real keyboard driving it - deterministically award one tile every
    // 30 frames (matching the established Phase 15 deterministic-nudge convention). Guarded by
    // > 0, not >= 0: once smokeFramesLeft_ reaches 0 it stops decrementing (see the block below),
    // so an >= 0 check here would keep re-triggering every subsequent frame - Exit() does not
    // halt Update() immediately (Task 15.14's own discovery of this exact bug class - naturally
    // bounded here by smokeNextAwardIndex_, but inconsistent).
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 30 == 0 &&
        smokeNextAwardIndex_ < static_cast<int>(tiles_.size()))
    {
        AwardTile(static_cast<std::size_t>(smokeNextAwardIndex_));
        ++smokeNextAwardIndex_;
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            int earnedCount = 0;
            for (const auto& tile : tiles_)
            {
                if (tile.getIsEarnedProperty()) { ++earnedCount; }
            }
            AchievementCollection real = localGamer_->GetAchievements();
            std::printf("[Achievements] Smoke test complete: locallyEarned=%d/%zu "
                        "realGetAchievementsCount=%d (now real disk-backed persistence, expected "
                        "== locallyEarned)\n",
                        earnedCount, tiles_.size(), real.getCountProperty());
            Exit();
        }
    }
}

void AchievementGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();
    spriteBatch_->DrawString(*font_, "Achievement Showcase (1-6 to award)", Vector2(16.0f, 16.0f),
                              Color(255, 255, 255, 255));

    const float tileWidth = 120.0f;
    const float tileHeight = 64.0f;
    const float margin = 12.0f;

    for (std::size_t i = 0; i < tiles_.size(); ++i)
    {
        const Achievement& tile = tiles_[i];
        const float x = margin + static_cast<float>(i % 3) * (tileWidth + margin);
        const float y = 56.0f + static_cast<float>(i / 3) * (tileHeight + margin);

        Color tileColor(0, 0, 0, 255);
        if (tile.getIsEarnedProperty())
        {
            tileColor = flashTimers_[i] > 0.0f ? Color(255, 255, 180, 255) : Color(220, 190, 60, 255);
        }
        else if (!tile.getDisplayBeforeEarnedProperty())
        {
            tileColor = Color(30, 30, 30, 255);
        }
        else
        {
            tileColor = Color(90, 90, 100, 255);
        }

        const Rectangle rect(static_cast<int>(x), static_cast<int>(y),
                              static_cast<int>(tileWidth), static_cast<int>(tileHeight));
        spriteBatch_->Draw(*whitePixel_, rect, tileColor);

        const bool hidden = !tile.getIsEarnedProperty() && !tile.getDisplayBeforeEarnedProperty();
        char line1[64];
        char line2[64];
        std::snprintf(line1, sizeof(line1), "%zu.%s", i + 1, hidden ? "???" : tile.getNameProperty().c_str());
        if (tile.getIsEarnedProperty())
        {
            std::snprintf(line2, sizeof(line2), "Score:%d %s", tile.getGamerScoreProperty(),
                          tile.getEarnedDateTimeProperty().ToString().c_str());
        }
        else
        {
            std::snprintf(line2, sizeof(line2), "%s", hidden ? "(hidden)" : "Locked");
        }
        spriteBatch_->DrawString(*font_, line1, Vector2(x + 4.0f, y + 4.0f), Color(255, 255, 255, 255));
        spriteBatch_->DrawString(*font_, line2, Vector2(x + 4.0f, y + 24.0f), Color(200, 200, 200, 255));
    }

    spriteBatch_->End();
}
