#include "RosterGame.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/IServiceProvider.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Net;

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

    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };
}

RosterGame::RosterGame(bool isHost)
    : isHost_(isHost)
{
}

RosterGame::~RosterGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}

void RosterGame::Initialize()
{
    Game::Initialize();

    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    localGamer_ = (*Microsoft::Xna::Framework::GamerServices::Gamer::getSignedInGamersProperty())[0];

    if (isHost_)
    {
        session_ = NetworkSession::Create(NetworkSessionType::SystemLink, 1, 8);
        std::printf("[Roster] Hosting as \"%s\", waiting for players to join...\n",
                    localGamer_->getGamertagProperty().c_str());
    }
    else
    {
        std::printf("[Roster] Searching for a session to join...\n");
        AvailableNetworkSessionCollection available =
            NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        for (int attempt = 0; attempt < 100 && available.getCountProperty() == 0; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            available = NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        }
        if (available.getCountProperty() == 0)
        {
            std::printf("[Roster] No session found after searching - is a host running?\n");
            Exit();
            return;
        }
        const auto& constAvailable = available;
        session_ = NetworkSession::Join(&constAvailable[0]);
        std::printf("[Roster] Joined the host's session as \"%s\".\n", localGamer_->getGamertagProperty().c_str());

        // Task 5.1-5.4 (plans/plan_net.md Phase 5): real host migration now exists - opting in here (as
        // the joining/client role, the only side that ever actually loses a host connection) means
        // OnHostChanged below is no longer permanently dead: if the host process is killed while
        // this one keeps running, this client either really becomes the new host itself (the only
        // possible outcome with just one host + one client, per Task 5.1's "lowest remaining wire
        // id" rule - trivially itself when it's the sole survivor) or, with more client processes
        // running, genuinely reconnects to whichever one was promoted.
        session_->setAllowHostMigrationProperty(true);
    }

    session_->GamerJoined += [this](System::Object* sender, const GamerJoinedEventArgs& e) { OnGamerJoined(sender, e); };
    session_->GamerLeft += [this](System::Object* sender, const GamerLeftEventArgs& e) { OnGamerLeft(sender, e); };
    session_->HostChanged += [this](System::Object* sender, const HostChangedEventArgs& e) { OnHostChanged(sender, e); };
    session_->SessionEnded += [this](System::Object* sender, const NetworkSessionEndedEventArgs& e) { OnSessionEnded(sender, e); };

    localNetworkGamer_ = session_->getLocalGamersProperty()[0];
}

void RosterGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void RosterGame::OnGamerJoined(System::Object* /*sender*/, const GamerJoinedEventArgs& e)
{
    NetworkGamer* gamer = e.getGamerProperty();
    std::printf("[Roster] GamerJoined: %s (local=%d host=%d)\n", gamer->getGamertagProperty().c_str(),
                gamer->getIsLocalProperty() ? 1 : 0, gamer->getIsHostProperty() ? 1 : 0);
}

void RosterGame::OnGamerLeft(System::Object* /*sender*/, const GamerLeftEventArgs& e)
{
    NetworkGamer* gamer = e.getGamerProperty();
    std::printf("[Roster] GamerLeft: %s\n", gamer->getGamertagProperty().c_str());
}

void RosterGame::OnHostChanged(System::Object* /*sender*/, const HostChangedEventArgs& e)
{
    // Task 5.1-5.4 (plans/plan_net.md Phase 5): real host migration - this client session opted in via
    // setAllowHostMigrationProperty(true) in Initialize() above, so this really can fire now: kill
    // the host process while this one keeps running and watch it happen live (this client
    // deterministically becomes the new host itself, or reconnects to whichever other client did -
    // see Task 5.1's own "lowest remaining wire id" rule).
    ++hostChangedFireCount_;
    std::printf("[Roster] HostChanged: new host is %s\n", e.getNewHostProperty()->getGamertagProperty().c_str());
}

void RosterGame::OnSessionEnded(System::Object* /*sender*/, const NetworkSessionEndedEventArgs& /*e*/)
{
    std::printf("[Roster] SessionEnded.\n");
}

void RosterGame::PrintRosterToConsole()
{
    const auto& all = session_->getAllGamersProperty();
    std::printf("[Roster] --- roster (%d gamers) ---\n", all.getCountProperty());
    for (int i = 0; i < all.getCountProperty(); ++i)
    {
        NetworkGamer* gamer = all[i];
        std::printf("[Roster]   %-12s Host:%s Local:%s Ready:%s Talking:%s\n",
                    gamer->getGamertagProperty().c_str(),
                    gamer->getIsHostProperty() ? "Y" : "N",
                    gamer->getIsLocalProperty() ? "Y" : "N",
                    gamer->getIsReadyProperty() ? "Y" : "N",
                    gamer->getIsTalkingProperty() ? "Y" : "N");
    }
}

void RosterGame::Update(GameTime& gameTime)
{
    if (session_ == nullptr)
    {
        return;
    }
    session_->Update();

    KeyboardState keys = Keyboard::GetState();
    // 'R' toggles the local gamer's IsReady - edge-triggered so holding the key doesn't flicker
    // it every frame. IsReady is plain local storage (see NetworkGamer.cpp) never sent over the
    // wire, so this process's own roster panel updates immediately but the *other* process's
    // roster panel for this same gamer never changes - a real, honest limitation, not a bug.
    if (keys.IsKeyDown(Keys::R) && !previousKeys_.IsKeyDown(Keys::R) && localNetworkGamer_ != nullptr)
    {
        localNetworkGamer_->setIsReadyProperty(!localNetworkGamer_->getIsReadyProperty());
    }
    previousKeys_ = keys;

    // Smoke-test mode has no real keyboard driving it - deterministically flip Ready every ~60
    // frames so a headless run still shows an observable, verifiable change over time (matching
    // the established Phase 15 deterministic-nudge convention). Guarded by > 0, not >= 0: once
    // smokeFramesLeft_ reaches 0 it stops decrementing (see the block below), so an >= 0 check
    // here would keep re-triggering every subsequent frame - Exit() does not halt Update()
    // immediately (Task 15.14's own discovery of this exact bug class).
    if (smokeFramesLeft_ > 0 && localNetworkGamer_ != nullptr && smokeFramesLeft_ % 60 == 0)
    {
        localNetworkGamer_->setIsReadyProperty(!localNetworkGamer_->getIsReadyProperty());
    }

    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    rosterLogTimer_ += dt;
    if (rosterLogTimer_ >= 1.0f)
    {
        rosterLogTimer_ = 0.0f;
        PrintRosterToConsole();
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            PrintRosterToConsole();
            std::printf("[Roster] Smoke test complete: gamerCount=%d hostChangedFireCount=%d "
                        "(usually 0 - only nonzero if the host process happened to already exit "
                        "before this one's own smoke run finished, in which case this client just "
                        "really migrated - see OnHostChanged's own comment)\n",
                        session_->getAllGamersProperty().getCountProperty(), hostChangedFireCount_);
            Exit();
        }
    }
}

void RosterGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();

    spriteBatch_->DrawString(*font_, "Gamer Roster (R to toggle Ready)", Vector2(16.0f, 16.0f),
                              Color(255, 255, 255, 255));

    if (session_ != nullptr)
    {
        const auto& all = session_->getAllGamersProperty();
        float y = 48.0f;
        for (int i = 0; i < all.getCountProperty(); ++i)
        {
            NetworkGamer* gamer = all[i];
            char line[256];
            std::snprintf(line, sizeof(line), "%-12s Host:%s Local:%s Ready:%s Talking:%s",
                          gamer->getGamertagProperty().c_str(),
                          gamer->getIsHostProperty() ? "Y" : "N",
                          gamer->getIsLocalProperty() ? "Y" : "N",
                          gamer->getIsReadyProperty() ? "Y" : "N",
                          gamer->getIsTalkingProperty() ? "Y" : "N");
            const Color color = gamer->getIsLocalProperty() ? Color(240, 220, 80, 255) : Color(200, 200, 200, 255);
            spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), color);
            y += 20.0f;
        }
    }

    spriteBatch_->End();
}
