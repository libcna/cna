#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

// Task 15.1: cna_demo_net_client_server_arena. Real two-process NetworkSession::Create/Find/Join,
// LocalNetworkGamer::SendData/ReceiveData via PacketWriter/PacketReader, and GamerJoined/
// SessionEnded events over real ENet - a small 2D arena where each connected gamer controls a
// colored square with arrow keys, and every other gamer's square visibly moves too.
class ArenaGame : public Microsoft::Xna::Framework::Game
{
public:
    explicit ArenaGame(bool isHost);
    ~ArenaGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    void OnGamerJoined(System::Object* sender, const Microsoft::Xna::Framework::Net::GamerJoinedEventArgs& e);
    void OnGamerLeft(System::Object* sender, const Microsoft::Xna::Framework::Net::GamerLeftEventArgs& e);
    void OnSessionEnded(System::Object* sender, const Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs& e);

    bool isHost_;
    // Non-owning: GamerServicesDispatcher::Initialize() owns every SignedInGamer it creates (see
    // GamerCollection<T>'s own canonical ownership contract) - NetworkSession::Join() has no
    // overload accepting an explicit gamer list (matches real XNA: only Create() does), so this
    // demo must go through the same GamerServicesComponent-populated global SignedInGamers list a
    // real XNA game would use, rather than a directly-constructed SignedInGamer of its own.
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* localGamer_ = nullptr;
    Microsoft::Xna::Framework::Net::NetworkSession* session_ = nullptr;
    Microsoft::Xna::Framework::Net::LocalNetworkGamer* localNetworkGamer_ = nullptr;

    Microsoft::Xna::Framework::Vector2 localPosition_{100.0f, 100.0f};
    std::unordered_map<Microsoft::Xna::Framework::Net::NetworkGamer*, Microsoft::Xna::Framework::Vector2> remotePositions_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
    float positionLogTimer_ = 0.0f;
};
