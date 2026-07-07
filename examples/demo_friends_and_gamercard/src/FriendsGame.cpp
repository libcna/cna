#include "FriendsGame.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

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

    struct FriendDef
    {
        const char* gamertag;
        bool online;
        bool playing;
        bool away;
        bool busy;
        bool requestingFriend;
        bool friendRequesting;
    };

    constexpr FriendDef kFriendDefs[5] = {
        {"Alice", true, true, false, false, false, false},
        {"Bob", true, false, true, false, false, false},
        {"Carol", false, false, false, false, false, false},
        {"Dave", true, true, false, true, true, false},
        {"Eve", true, false, false, false, false, true},
    };
}

FriendsGame::FriendsGame() = default;

void FriendsGame::Log(const std::string& line)
{
    log_.push_back(line);
    while (log_.size() > kMaxLogLines)
    {
        log_.pop_front();
    }
    std::printf("[Friends] %s\n", line.c_str());
}

void FriendsGame::Initialize()
{
    Game::Initialize();

    for (const FriendDef& def : kFriendDefs)
    {
        friendStorage_.push_back(std::make_unique<FriendGamer>(FriendGamer::CreateInternal(
            def.gamertag, def.gamertag, def.online, def.playing, def.away, def.busy,
            def.requestingFriend, def.friendRequesting)));
        friends_.push_back(friendStorage_.back().get());
    }
    // FriendCollection is a documented non-owning view (see its own class comment) - friends_
    // (and friendStorage_, which actually owns the FriendGamer objects) is this demo's own
    // registry, matching the ownership pattern GamerServicesDispatcher::Initialize() already
    // establishes for SignedInGamer.
    FriendCollection collection = FriendCollection::CreateInternal(friends_);
    std::printf("[Friends] Built a local FriendCollection with %d entries via CreateInternal "
                "(SignedInGamer::GetFriends() itself always returns an empty stub).\n",
                collection.getCountProperty());
}

void FriendsGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void FriendsGame::TriggerAction(int actionIndex)
{
    Gamer* selected = friends_[static_cast<std::size_t>(selectedIndex_)];
    switch (actionIndex)
    {
        case 0:
            Guide::ShowGamerCard(PlayerIndex::One, selected);
            Log("ShowGamerCard(\"" + selected->getGamertagProperty() + "\") called - no-op, no real OS UI.");
            break;
        case 1:
            Guide::ShowFriendRequest(PlayerIndex::One, selected);
            Log("ShowFriendRequest(\"" + selected->getGamertagProperty() + "\") called - no-op, no real OS UI.");
            break;
        case 2:
            Guide::ShowFriends(PlayerIndex::One);
            Log("ShowFriends() called - no-op, no real OS UI.");
            break;
        case 3:
            Guide::ShowComposeMessage(PlayerIndex::One, "hello", {selected});
            Log("ShowComposeMessage(\"hello\", [\"" + selected->getGamertagProperty() + "\"]) called - no-op, no real OS UI.");
            break;
        default:
            break;
    }
}

void FriendsGame::Update(GameTime& /*gameTime*/)
{
    KeyboardState keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Down) && !previousKeys_.IsKeyDown(Keys::Down))
    {
        selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(friends_.size());
    }
    if (keys.IsKeyDown(Keys::Up) && !previousKeys_.IsKeyDown(Keys::Up))
    {
        selectedIndex_ = (selectedIndex_ - 1 + static_cast<int>(friends_.size())) % static_cast<int>(friends_.size());
    }
    if (keys.IsKeyDown(Keys::G) && !previousKeys_.IsKeyDown(Keys::G)) { TriggerAction(0); }
    if (keys.IsKeyDown(Keys::R) && !previousKeys_.IsKeyDown(Keys::R)) { TriggerAction(1); }
    if (keys.IsKeyDown(Keys::F) && !previousKeys_.IsKeyDown(Keys::F)) { TriggerAction(2); }
    if (keys.IsKeyDown(Keys::C) && !previousKeys_.IsKeyDown(Keys::C)) { TriggerAction(3); }
    previousKeys_ = keys;

    // Smoke-test mode has no real keyboard driving it - deterministically cycle through all 4
    // actions every 30 frames (matching the established Phase 15 deterministic-nudge convention).
    // Guarded by smokeFramesLeft_ > 0 (not >= 0): once it reaches 0 it stops decrementing below,
    // so an >= 0 check here would keep re-triggering forever on every subsequent frame - Exit()
    // does not halt Update() immediately, so this condition keeps being evaluated for a while.
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 30 == 0)
    {
        TriggerAction(smokeNextAction_ % 4);
        ++smokeNextAction_;
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Friends] Smoke test complete: actionsTriggered=%d logLines=%zu\n",
                        smokeNextAction_, log_.size());
            Exit();
        }
    }
}

void FriendsGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();
    spriteBatch_->DrawString(*font_, "Friends (Up/Down select, G/R/F/C to trigger Guide calls)",
                              Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));

    float y = 44.0f;
    for (std::size_t i = 0; i < friends_.size(); ++i)
    {
        FriendGamer* f = friends_[i];
        char line[192];
        std::snprintf(line, sizeof(line), "%s%-8s Online:%s Playing:%s Away:%s Busy:%s ReqRecv:%s ReqSent:%s",
                      static_cast<int>(i) == selectedIndex_ ? "> " : "  ",
                      f->getGamertagProperty().c_str(),
                      f->getIsOnlineProperty() ? "Y" : "N", f->getIsPlayingProperty() ? "Y" : "N",
                      f->getIsAwayProperty() ? "Y" : "N", f->getIsBusyProperty() ? "Y" : "N",
                      f->getFriendRequestReceivedFromProperty() ? "Y" : "N",
                      f->getFriendRequestSentToProperty() ? "Y" : "N");
        const Color color = static_cast<int>(i) == selectedIndex_ ? Color(240, 220, 80, 255) : Color(200, 200, 200, 255);
        spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), color);
        y += 18.0f;
    }

    y += 16.0f;
    spriteBatch_->DrawString(*font_, "Log:", Vector2(16.0f, y), Color(255, 255, 255, 255));
    y += 18.0f;
    for (const std::string& entry : log_)
    {
        spriteBatch_->DrawString(*font_, entry, Vector2(16.0f, y), Color(150, 200, 255, 255));
        y += 16.0f;
    }

    spriteBatch_->End();
}
