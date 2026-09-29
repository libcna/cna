#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.14: cna_demo_friends_and_gamercard. FriendCollection/FriendGamer and the Guide's
// ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage. A friends-list panel (Up/Down to
// select) plus an on-screen scrolling log recording what each G/R/F/C key's Guide call did.
//
// The list is sample data built with FriendCollection::CreateInternal: SignedInGamer::GetFriends()
// returns an account's friends from a CNA account service and throws GamerPrivilegeException for a
// local offline profile. The four Guide calls open service panes (gamer card, friend request,
// friends list, message composer) for a gamer signed in to a CNA account as player One (Home opens
// the Guide to sign in); without a CNA account service (the default), or without such a gamer, each
// throws GamerServicesNotAvailableException, and the log shows that refusal.
class FriendsGame : public Microsoft::Xna::Framework::Game
{
public:
    FriendsGame();

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    // The Guide draws its panes and notifications through the game's graphics device service.
    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};

    void Log(const std::string& line);
    void TriggerAction(int actionIndex);

    std::vector<std::unique_ptr<Microsoft::Xna::Framework::GamerServices::FriendGamer>> friendStorage_;
    std::vector<Microsoft::Xna::Framework::GamerServices::FriendGamer*> friends_;
    int selectedIndex_ = 0;

    std::deque<std::string> log_;
    static constexpr std::size_t kMaxLogLines = 6;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
    int smokeNextAction_ = 0;
};
