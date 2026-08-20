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
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

// Task 15.4: cna_demo_simulated_network_conditions. Real two-process NetworkSession over real
// ENet, rendered as a small Pong-style match (two paddles, one host-authoritative ball). Number
// keys 1/2/3/4 raise/lower NetworkSession::SimulatedLatencyProperty/SimulatedPacketLossProperty
// live, and the HUD shows both the requested simulated values and the real measured RTT (Task
// 4.1) side by side. Task 4.3 originally confirmed these two properties were stored but never
// applied to real traffic anywhere (deliberately matching FNA's own non-functional reference
// stub); Task 6.1-6.5 (plans/plan_net.md Phase 6) implemented a real receive-side delayed-delivery
// queue and probabilistic drop for them, scoped to AppData delivered to local gamers (see
// ENetBackend.cpp's own HandleAppData comment for why session-management traffic and host-relay
// traffic stay unaffected) - raising SimLatency/SimPacketLoss here now produces a genuinely
// visible remote-paddle/ball stutter, not just a HUD number that moves in isolation.
class SimGame : public Microsoft::Xna::Framework::Game
{
public:
    explicit SimGame(bool isHost);
    ~SimGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    void OnSessionEnded(System::Object* sender, const Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs& e);
    void UpdateBallPhysics(float dt);

    bool isHost_;
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* localGamer_ = nullptr;
    Microsoft::Xna::Framework::Net::NetworkSession* session_ = nullptr;
    Microsoft::Xna::Framework::Net::LocalNetworkGamer* localNetworkGamer_ = nullptr;

    Microsoft::Xna::Framework::Vector2 localPaddlePos_{0.0f, 200.0f};
    Microsoft::Xna::Framework::Vector2 remotePaddlePos_{0.0f, 200.0f};
    bool haveRemotePaddle_ = false;

    // Host-authoritative; the client only ever renders the value it receives from the host.
    Microsoft::Xna::Framework::Vector2 ballPos_{390.0f, 240.0f};
    Microsoft::Xna::Framework::Vector2 ballVelocity_{180.0f, 130.0f};

    System::TimeSpan simulatedLatency_;
    float simulatedPacketLoss_ = 0.0f;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
    float hudLogTimer_ = 0.0f;
};
