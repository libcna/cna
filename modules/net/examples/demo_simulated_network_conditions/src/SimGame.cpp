#include "SimGame.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "System/IServiceProvider.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace
{
    constexpr float kScreenWidth = 800.0f;
    constexpr float kScreenHeight = 480.0f;
    constexpr float kPaddleWidth = 20.0f;
    constexpr float kPaddleHeight = 80.0f;
    constexpr float kBallSize = 16.0f;
    constexpr float kLeftPaddleX = 20.0f;
    constexpr float kRightPaddleX = kScreenWidth - kLeftPaddleX - kPaddleWidth;

    // A minimal SpriteFont covering all printable ASCII, reusing one 1x1 white glyph (same
    // approach already established in demo_net_client_server_arena's ArenaGame.cpp).
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

    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };
}

SimGame::SimGame(bool isHost)
    : isHost_(isHost)
{
    localPaddlePos_.X = isHost ? kLeftPaddleX : kRightPaddleX;
    remotePaddlePos_.X = isHost ? kRightPaddleX : kLeftPaddleX;
}

SimGame::~SimGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}

void SimGame::Initialize()
{
    Game::Initialize();

    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    localGamer_ = (*Microsoft::Xna::Framework::GamerServices::Gamer::getSignedInGamersProperty())[0];

    if (isHost_)
    {
        session_ = NetworkSession::Create(NetworkSessionType::SystemLink, 1, 8);
        std::printf("[SimNet] Hosting as \"%s\", waiting for a client to join...\n",
                    localGamer_->getGamertagProperty().c_str());
    }
    else
    {
        std::printf("[SimNet] Searching for a session to join...\n");
        AvailableNetworkSessionCollection available =
            NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        for (int attempt = 0; attempt < 100 && available.getCountProperty() == 0; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            available = NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        }
        if (available.getCountProperty() == 0)
        {
            std::printf("[SimNet] No session found after searching - is a host running?\n");
            Exit();
            return;
        }
        const auto& constAvailable = available;
        session_ = NetworkSession::Join(&constAvailable[0]);
        std::printf("[SimNet] Joined the host's session as \"%s\".\n", localGamer_->getGamertagProperty().c_str());
    }

    session_->SessionEnded += [this](System::Object* sender, const NetworkSessionEndedEventArgs& e) { OnSessionEnded(sender, e); };
    localNetworkGamer_ = session_->getLocalGamersProperty()[0];
}

void SimGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void SimGame::OnSessionEnded(System::Object* /*sender*/, const NetworkSessionEndedEventArgs& /*e*/)
{
    std::printf("[SimNet] SessionEnded.\n");
}

void SimGame::UpdateBallPhysics(float dt)
{
    ballPos_ = ballPos_ + ballVelocity_ * dt;

    if (ballPos_.Y <= 0.0f || ballPos_.Y + kBallSize >= kScreenHeight)
    {
        ballVelocity_.Y = -ballVelocity_.Y;
        ballPos_.Y = std::clamp(ballPos_.Y, 0.0f, kScreenHeight - kBallSize);
    }

    const Rectangle ballRect(static_cast<int>(ballPos_.X), static_cast<int>(ballPos_.Y),
                              static_cast<int>(kBallSize), static_cast<int>(kBallSize));
    const Rectangle leftPaddleRect(static_cast<int>(kLeftPaddleX), static_cast<int>(localPaddlePos_.Y),
                                    static_cast<int>(kPaddleWidth), static_cast<int>(kPaddleHeight));
    const Rectangle rightPaddleRect(static_cast<int>(kRightPaddleX), static_cast<int>(remotePaddlePos_.Y),
                                     static_cast<int>(kPaddleWidth), static_cast<int>(kPaddleHeight));

    if (ballVelocity_.X < 0.0f && ballRect.Intersects(leftPaddleRect))
    {
        ballVelocity_.X = -ballVelocity_.X;
    }
    else if (ballVelocity_.X > 0.0f && ballRect.Intersects(rightPaddleRect))
    {
        ballVelocity_.X = -ballVelocity_.X;
    }
    else if (ballPos_.X < -kBallSize || ballPos_.X > kScreenWidth)
    {
        // Missed - reset to center rather than modeling score-keeping (out of scope for this demo).
        ballPos_ = Vector2(kScreenWidth / 2.0f, kScreenHeight / 2.0f);
    }
}

void SimGame::Update(GameTime& gameTime)
{
    if (session_ == nullptr)
    {
        return;
    }
    session_->Update();

    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    const float speed = 220.0f;
    KeyboardState keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Up))   { localPaddlePos_.Y -= speed * dt; }
    if (keys.IsKeyDown(Keys::Down)) { localPaddlePos_.Y += speed * dt; }
    localPaddlePos_.Y = std::clamp(localPaddlePos_.Y, 0.0f, kScreenHeight - kPaddleHeight);

    // 1/2 lower/raise simulated latency by 100ms per press; 3/4 lower/raise simulated packet loss
    // by 10 percentage points per press. Task 6.1-6.5 (plans/plan_net.md Phase 6): both now drive a
    // real receive-side delayed-delivery queue and probabilistic drop in ENetBackend - raising
    // either one now visibly stutters/drops the remote paddle and (on the client) the
    // host-authoritative ball position. The HUD's "Real measured RTT" column still won't move in
    // response to these (ENet's own per-peer RTT measurement happens below this simulated-delivery
    // layer, at the raw transport level, and is unaffected by it - an honest, separate limitation,
    // not a regression of this task).
    if (keys.IsKeyDown(Keys::D1))
    {
        simulatedLatency_ = System::TimeSpan::FromMilliseconds(
            std::max(0.0, simulatedLatency_.getTotalMillisecondsProperty() - 100.0));
        session_->setSimulatedLatencyProperty(simulatedLatency_);
    }
    if (keys.IsKeyDown(Keys::D2))
    {
        simulatedLatency_ = System::TimeSpan::FromMilliseconds(
            std::min(2000.0, simulatedLatency_.getTotalMillisecondsProperty() + 100.0));
        session_->setSimulatedLatencyProperty(simulatedLatency_);
    }
    if (keys.IsKeyDown(Keys::D3))
    {
        simulatedPacketLoss_ = std::max(0.0f, simulatedPacketLoss_ - 0.1f);
        session_->setSimulatedPacketLossProperty(simulatedPacketLoss_);
    }
    if (keys.IsKeyDown(Keys::D4))
    {
        simulatedPacketLoss_ = std::min(1.0f, simulatedPacketLoss_ + 0.1f);
        session_->setSimulatedPacketLossProperty(simulatedPacketLoss_);
    }

    // Smoke-test mode has no real keyboard driving it - deterministically move the dials so a
    // headless run still produces an observable, verifiable value change over time (matching
    // ArenaGame's own deterministic-nudge convention for its position field). Guarded by > 0,
    // not >= 0: once smokeFramesLeft_ reaches 0 it stops decrementing (see the block below), so
    // an >= 0 check here would keep re-running every subsequent frame after Exit() is requested,
    // which does not halt Update() immediately (Task 15.14's own discovery of this exact bug
    // class - harmless here since both dials are already clamped, but wasteful and inconsistent).
    if (smokeFramesLeft_ > 0)
    {
        simulatedLatency_ = System::TimeSpan::FromMilliseconds(
            std::min(2000.0, simulatedLatency_.getTotalMillisecondsProperty() + 20.0));
        simulatedPacketLoss_ = std::min(1.0f, simulatedPacketLoss_ + 0.02f);
        session_->setSimulatedLatencyProperty(simulatedLatency_);
        session_->setSimulatedPacketLossProperty(simulatedPacketLoss_);
    }

    if (isHost_)
    {
        UpdateBallPhysics(dt);
    }

    if (localNetworkGamer_ != nullptr)
    {
        PacketWriter writer;
        writer.Write(localPaddlePos_);
        if (isHost_)
        {
            writer.Write(ballPos_);
        }
        localNetworkGamer_->SendData(writer, SendDataOptions::InOrder);

        PacketReader reader;
        NetworkGamer* sender = nullptr;
        while (localNetworkGamer_->getIsDataAvailableProperty())
        {
            localNetworkGamer_->ReceiveData(reader, sender);
            if (sender != nullptr && !sender->getIsLocalProperty())
            {
                remotePaddlePos_ = reader.ReadVector2();
                haveRemotePaddle_ = true;
                if (sender->getIsHostProperty())
                {
                    ballPos_ = reader.ReadVector2();
                }
            }
        }
    }

    hudLogTimer_ += dt;
    if (hudLogTimer_ >= 1.0f)
    {
        hudLogTimer_ = 0.0f;
        double realRttMs = 0.0;
        const auto& remotes = session_->getRemoteGamersProperty();
        if (remotes.getCountProperty() > 0)
        {
            realRttMs = remotes[0]->getRoundtripTimeProperty().getTotalMillisecondsProperty();
        }
        std::printf("[SimNet] simulatedLatency=%.0fms simulatedPacketLoss=%.0f%% realMeasuredRTT=%.1fms "
                    "(simulated values now have a real effect on remote-paddle/ball AppData "
                    "delivery - realMeasuredRTT stays unaffected, ENet's own RTT tracking is a "
                    "lower transport layer)\n",
                    simulatedLatency_.getTotalMillisecondsProperty(), simulatedPacketLoss_ * 100.0f, realRttMs);
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[SimNet] Smoke test complete: simulatedLatency=%.0fms simulatedPacketLoss=%.0f%% "
                        "haveRemotePaddle=%s\n",
                        simulatedLatency_.getTotalMillisecondsProperty(), simulatedPacketLoss_ * 100.0f,
                        haveRemotePaddle_ ? "true" : "false");
            Exit();
        }
    }
}

void SimGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(15, 15, 25, 255));

    spriteBatch_->Begin();

    const Vector2& leftPaddlePos = isHost_ ? localPaddlePos_ : remotePaddlePos_;
    const Vector2& rightPaddlePos = isHost_ ? remotePaddlePos_ : localPaddlePos_;

    const Rectangle leftRect(static_cast<int>(kLeftPaddleX), static_cast<int>(leftPaddlePos.Y),
                              static_cast<int>(kPaddleWidth), static_cast<int>(kPaddleHeight));
    const Rectangle rightRect(static_cast<int>(kRightPaddleX), static_cast<int>(rightPaddlePos.Y),
                               static_cast<int>(kPaddleWidth), static_cast<int>(kPaddleHeight));
    const Rectangle ballRect(static_cast<int>(ballPos_.X), static_cast<int>(ballPos_.Y),
                              static_cast<int>(kBallSize), static_cast<int>(kBallSize));

    spriteBatch_->Draw(*whitePixel_, leftRect, Color(220, 60, 60, 255));
    spriteBatch_->Draw(*whitePixel_, rightRect, Color(60, 140, 220, 255));
    spriteBatch_->Draw(*whitePixel_, ballRect, Color(240, 220, 80, 255));

    char hud[256];
    std::snprintf(hud, sizeof(hud), "SimLatency:%.0fms SimPacketLoss:%.0f%% (1/2/3/4 to adjust - now a real effect)",
                  simulatedLatency_.getTotalMillisecondsProperty(), simulatedPacketLoss_ * 100.0f);
    spriteBatch_->DrawString(*font_, hud, Vector2(16.0f, 8.0f), Color(255, 255, 255, 255));

    spriteBatch_->End();
}
