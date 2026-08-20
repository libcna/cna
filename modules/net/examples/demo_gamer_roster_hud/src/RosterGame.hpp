#pragma once

#include <memory>

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
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

// Task 15.6: cna_demo_gamer_roster_hud. Real two-process NetworkSession over real ENet. Exercises
// the full gamer-roster event surface - GamerJoined, GamerLeft, HostChanged, SessionEnded - and
// renders a live-updating panel listing every NetworkGamer in AllGamers with its IsHost/IsLocal/
// IsReady/IsTalking flags. Documents one remaining real, current gap rather than hiding it:
// IsReady is local-only storage never synced to remote gamers over the wire (toggling it locally
// changes only this process's own view of that one gamer). HostChanged used to never fire (Task
// 2.6 confirmed real host migration was unimplemented, matching FNA's own reference, so the host
// disconnecting always ended the session outright instead of electing a new host) - Task 5.1-5.4
// (plans/plan_net.md Phase 5) implemented it for real; the client role now opts in via
// setAllowHostMigrationProperty(true) (see RosterGame::Initialize()), so killing the host process
// mid-session now really does elect a new host and fire this event, instead of ending the session.
class RosterGame : public Microsoft::Xna::Framework::Game
{
public:
    explicit RosterGame(bool isHost);
    ~RosterGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    void OnGamerJoined(System::Object* sender, const Microsoft::Xna::Framework::Net::GamerJoinedEventArgs& e);
    void OnGamerLeft(System::Object* sender, const Microsoft::Xna::Framework::Net::GamerLeftEventArgs& e);
    void OnHostChanged(System::Object* sender, const Microsoft::Xna::Framework::Net::HostChangedEventArgs& e);
    void OnSessionEnded(System::Object* sender, const Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs& e);
    void PrintRosterToConsole();

    bool isHost_;
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* localGamer_ = nullptr;
    Microsoft::Xna::Framework::Net::NetworkSession* session_ = nullptr;
    Microsoft::Xna::Framework::Net::LocalNetworkGamer* localNetworkGamer_ = nullptr;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    int hostChangedFireCount_ = 0;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
    float rosterLogTimer_ = 0.0f;
};
