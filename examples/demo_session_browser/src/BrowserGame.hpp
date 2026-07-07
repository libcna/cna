#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

// Task 15.5: cna_demo_session_browser. One process hosts/advertises ("Hosting..."); another
// repeatedly calls NetworkSession::Find(...), renders the resulting AvailableNetworkSessionCollection
// as a scrollable list (host gamertag, port used to disambiguate identical "Stub Gamer" entries,
// current/max gamer counts), Up/Down to move the selection, Enter to Join - the classic "session
// lobby" screen. Real two-process ENet; supports launching several --host processes at once (the
// discovery port allows this, see ENetDiscoveryService.cpp's Task 6.5 comment) to prove the list
// genuinely scrolls across multiple distinct entries, not just a single-item stub.
class BrowserGame : public Microsoft::Xna::Framework::Game
{
public:
    BrowserGame(bool isHost, int maxGamersForHost, int smokeSelectIndex);
    ~BrowserGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    void RefreshDiscoveredSessions();
    void JoinSelected();

    bool isHost_;
    int maxGamersForHost_;
    int smokeSelectIndex_;

    Microsoft::Xna::Framework::GamerServices::SignedInGamer* localGamer_ = nullptr;
    Microsoft::Xna::Framework::Net::NetworkSession* session_ = nullptr;
    bool joined_ = false;

    std::vector<Microsoft::Xna::Framework::Net::AvailableNetworkSession> discovered_;
    int selectedIndex_ = 0;
    float refreshTimer_ = 0.0f;
    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
};
