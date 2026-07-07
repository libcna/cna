#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.14: cna_demo_friends_and_gamercard. FriendCollection (via CreateInternal) and the
// no-op Guide::ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage calls. A
// friends-list panel (Up/Down to select) plus an on-screen scrolling log printing
// "ShowGamerCard(...) called" etc. every time a key triggers one of those Guide calls, since
// none produce real OS UI otherwise (all 4 are confirmed no-ops in this platform's
// implementation, matching FNA's own reference).
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
