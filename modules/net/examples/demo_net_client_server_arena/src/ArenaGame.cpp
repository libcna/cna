#include "ArenaGame.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
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
    // A minimal SpriteFont covering all printable ASCII, reusing the same 1x1 white-pixel glyph
    // for every character (rendering fidelity is not the point here - the point is that a real
    // gamertag label draws above each square without crashing).
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

    Color ColorForPlayerIndex(int index)
    {
        static const Color kPalette[] = {
            Color(220, 60, 60, 255), Color(60, 140, 220, 255), Color(60, 200, 90, 255),
            Color(220, 200, 60, 255),
        };
        return kPalette[static_cast<std::size_t>(index) % (sizeof(kPalette) / sizeof(kPalette[0]))];
    }

    // GamerServicesDispatcher::Initialize() never dereferences its serviceProvider argument
    // (mirrors tools/net/gamerservices_dispatcher_harness.cpp's own NullServiceProvider).
    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };
}

ArenaGame::ArenaGame(bool isHost)
    : isHost_(isHost)
{
}

ArenaGame::~ArenaGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}

void ArenaGame::Initialize()
{
    Game::Initialize();

    // NetworkSession::Join() has no overload accepting an explicit gamer list (matches real XNA:
    // only Create() does) - it always draws from the global Gamer::SignedInGamers, exactly like
    // a real game with a GamerServicesComponent registered. GamerServicesDispatcher::Initialize()
    // is that population step.
    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    localGamer_ = (*Microsoft::Xna::Framework::GamerServices::Gamer::getSignedInGamersProperty())[0];

    if (isHost_)
    {
        session_ = NetworkSession::Create(NetworkSessionType::SystemLink, 1, 8);
        std::printf("[Arena] Hosting a new session as \"%s\", waiting for players to join...\n",
                    localGamer_->getGamertagProperty().c_str());
    }
    else
    {
        std::printf("[Arena] Searching for a session to join...\n");
        AvailableNetworkSessionCollection available =
            NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        for (int attempt = 0; attempt < 100 && available.getCountProperty() == 0; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            available = NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        }
        if (available.getCountProperty() == 0)
        {
            std::printf("[Arena] No session found after searching - is a host running?\n");
            Exit();
            return;
        }
        // const_cast-free: AvailableNetworkSessionCollection's non-const operator[] is the
        // mutating accessor (throws NotSupportedException, since ReadOnlyCollection<T> is
        // genuinely read-only) - only the const overload returns a plain reference.
        const auto& constAvailable = available;
        session_ = NetworkSession::Join(&constAvailable[0]);
        std::printf("[Arena] Joined the host's session as \"%s\".\n", localGamer_->getGamertagProperty().c_str());
    }

    session_->GamerJoined += [this](System::Object* sender, const GamerJoinedEventArgs& e) { OnGamerJoined(sender, e); };
    session_->GamerLeft += [this](System::Object* sender, const GamerLeftEventArgs& e) { OnGamerLeft(sender, e); };
    session_->SessionEnded += [this](System::Object* sender, const NetworkSessionEndedEventArgs& e) { OnSessionEnded(sender, e); };

    localNetworkGamer_ = session_->getLocalGamersProperty()[0];
    localPosition_ = Vector2(isHost_ ? 100.0f : 500.0f, 300.0f);
}

void ArenaGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void ArenaGame::OnGamerJoined(System::Object* /*sender*/, const GamerJoinedEventArgs& e)
{
    NetworkGamer* gamer = e.getGamerProperty();
    std::printf("[Arena] GamerJoined: %s (local=%d)\n",
                gamer->getGamertagProperty().c_str(), gamer->getIsLocalProperty() ? 1 : 0);
    if (!gamer->getIsLocalProperty())
    {
        remotePositions_[gamer] = Vector2(300.0f, 300.0f);
    }
}

void ArenaGame::OnGamerLeft(System::Object* /*sender*/, const GamerLeftEventArgs& e)
{
    NetworkGamer* gamer = e.getGamerProperty();
    std::printf("[Arena] GamerLeft: %s\n", gamer->getGamertagProperty().c_str());
    remotePositions_.erase(gamer);
}

void ArenaGame::OnSessionEnded(System::Object* /*sender*/, const NetworkSessionEndedEventArgs& /*e*/)
{
    std::printf("[Arena] SessionEnded.\n");
}

void ArenaGame::Update(GameTime& gameTime)
{
    if (session_ == nullptr)
    {
        return;
    }
    session_->Update();

    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    const float speed = 150.0f;
    KeyboardState keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Left))  { localPosition_.X -= speed * dt; }
    if (keys.IsKeyDown(Keys::Right)) { localPosition_.X += speed * dt; }
    if (keys.IsKeyDown(Keys::Up))    { localPosition_.Y -= speed * dt; }
    if (keys.IsKeyDown(Keys::Down))  { localPosition_.Y += speed * dt; }

    // Smoke-test mode has no real keyboard driving it - nudge the square deterministically so a
    // headless run still produces an observable, verifiable position change over time. Guarded
    // by > 0, not >= 0: once smokeFramesLeft_ reaches 0 it stops decrementing (see the block
    // below), so an >= 0 check here would keep nudging every subsequent frame after Exit() is
    // requested, which does not halt Update() immediately (Task 15.14's own discovery of this
    // exact bug class - harmless here (just a bit more drift before exit) but inconsistent).
    if (smokeFramesLeft_ > 0)
    {
        localPosition_.X += speed * dt;
    }

    if (localNetworkGamer_ != nullptr)
    {
        PacketWriter writer;
        writer.Write(localPosition_);
        localNetworkGamer_->SendData(writer, SendDataOptions::InOrder);

        PacketReader reader;
        NetworkGamer* sender = nullptr;
        while (localNetworkGamer_->getIsDataAvailableProperty())
        {
            localNetworkGamer_->ReceiveData(reader, sender);
            if (sender != nullptr && !sender->getIsLocalProperty())
            {
                remotePositions_[sender] = reader.ReadVector2();
            }
        }
    }

    positionLogTimer_ += dt;
    if (positionLogTimer_ >= 1.0f)
    {
        positionLogTimer_ = 0.0f;
        std::printf("[Arena] local=(%.0f,%.0f) knownRemotes=%zu\n",
                    localPosition_.X, localPosition_.Y, remotePositions_.size());
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Arena] Smoke test complete: knownRemotes=%zu\n", remotePositions_.size());
            Exit();
        }
    }
}

void ArenaGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(20, 20, 30, 255));

    spriteBatch_->Begin();

    const Rectangle localRect(static_cast<int>(localPosition_.X), static_cast<int>(localPosition_.Y), 32, 32);
    spriteBatch_->Draw(*whitePixel_, localRect, ColorForPlayerIndex(0));
    spriteBatch_->DrawString(*font_, localGamer_->getGamertagProperty(),
                              Vector2(localPosition_.X, localPosition_.Y - 16.0f), Color(255, 255, 255, 255));

    int index = 1;
    for (const auto& [gamer, pos] : remotePositions_)
    {
        const Rectangle rect(static_cast<int>(pos.X), static_cast<int>(pos.Y), 32, 32);
        spriteBatch_->Draw(*whitePixel_, rect, ColorForPlayerIndex(index));
        spriteBatch_->DrawString(*font_, gamer->getGamertagProperty(),
                                  Vector2(pos.X, pos.Y - 16.0f), Color(255, 255, 255, 255));
        ++index;
    }

    spriteBatch_->End();
}
