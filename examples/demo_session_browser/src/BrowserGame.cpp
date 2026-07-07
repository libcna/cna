#include "BrowserGame.hpp"

#include <algorithm>
#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/IServiceProvider.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

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

BrowserGame::BrowserGame(bool isHost, int maxGamersForHost, int smokeSelectIndex)
    : isHost_(isHost), maxGamersForHost_(maxGamersForHost), smokeSelectIndex_(smokeSelectIndex)
{
}

BrowserGame::~BrowserGame()
{
    if (session_ != nullptr)
    {
        session_->Dispose();
        session_ = nullptr;
    }
}

void BrowserGame::Initialize()
{
    Game::Initialize();

    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    localGamer_ = (*Microsoft::Xna::Framework::GamerServices::Gamer::getSignedInGamersProperty())[0];

    if (isHost_)
    {
        session_ = NetworkSession::Create(NetworkSessionType::SystemLink, 1, maxGamersForHost_);
        std::printf("[Browser] Hosting as \"%s\" (maxGamers=%d)...\n",
                    localGamer_->getGamertagProperty().c_str(), maxGamersForHost_);
    }
    else
    {
        std::printf("[Browser] Browsing for sessions to join...\n");
    }
}

void BrowserGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void BrowserGame::RefreshDiscoveredSessions()
{
    AvailableNetworkSessionCollection available =
        NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
    const auto& constAvailable = available;

    discovered_.clear();
    for (int i = 0; i < constAvailable.getCountProperty(); ++i)
    {
        discovered_.push_back(constAvailable[i]);
    }
    if (discovered_.empty())
    {
        selectedIndex_ = 0;
    }
    else
    {
        selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(discovered_.size()) - 1);
    }
}

void BrowserGame::JoinSelected()
{
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(discovered_.size()))
    {
        return;
    }
    const AvailableNetworkSession& target = discovered_[static_cast<std::size_t>(selectedIndex_)];
    session_ = NetworkSession::Join(&target);
    joined_ = true;
    std::printf("[Browser] Joined \"%s\" (port %u) as \"%s\".\n",
                target.getHostGamertagProperty().c_str(), target.GetConnectPort(),
                localGamer_->getGamertagProperty().c_str());
}

void BrowserGame::Update(GameTime& gameTime)
{
    if (isHost_)
    {
        if (session_ != nullptr)
        {
            session_->Update();
        }
    }
    else if (joined_)
    {
        session_->Update();
    }
    else
    {
        const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
        refreshTimer_ += dt;
        if (refreshTimer_ >= 0.3f)
        {
            refreshTimer_ = 0.0f;
            RefreshDiscoveredSessions();
        }

        KeyboardState keys = Keyboard::GetState();
        // Edge-triggered (this frame down, last frame up) so holding a key scrolls one entry per
        // press instead of racing through the whole list in a single frame.
        if (keys.IsKeyDown(Keys::Down) && !previousKeys_.IsKeyDown(Keys::Down) && !discovered_.empty())
        {
            selectedIndex_ = std::min(selectedIndex_ + 1, static_cast<int>(discovered_.size()) - 1);
        }
        if (keys.IsKeyDown(Keys::Up) && !previousKeys_.IsKeyDown(Keys::Up) && !discovered_.empty())
        {
            selectedIndex_ = std::max(selectedIndex_ - 1, 0);
        }
        if (keys.IsKeyDown(Keys::Enter) && !previousKeys_.IsKeyDown(Keys::Enter))
        {
            JoinSelected();
        }
        previousKeys_ = keys;

        // Smoke-test mode has no real keyboard driving it - once at least (smokeSelectIndex_ + 1)
        // entries have been discovered, deterministically select and join that index (matching
        // the deterministic-nudge convention already established for prior Phase 15 demos).
        if (smokeFramesLeft_ >= 0 && !joined_ &&
            static_cast<int>(discovered_.size()) > smokeSelectIndex_)
        {
            selectedIndex_ = smokeSelectIndex_;
            JoinSelected();
        }
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Browser] Smoke test complete: discovered=%zu joined=%s\n",
                        discovered_.size(), joined_ ? "true" : "false");
            Exit();
        }
    }
}

void BrowserGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();

    if (isHost_)
    {
        spriteBatch_->DrawString(*font_, "Hosting...", Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));
    }
    else if (joined_)
    {
        spriteBatch_->DrawString(*font_, "Joined.", Vector2(16.0f, 16.0f), Color(120, 220, 120, 255));
    }
    else
    {
        spriteBatch_->DrawString(*font_, "Session Browser (Up/Down, Enter to join)",
                                  Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));
        float y = 48.0f;
        for (std::size_t i = 0; i < discovered_.size(); ++i)
        {
            const AvailableNetworkSession& entry = discovered_[i];
            const int maxGamers = entry.getCurrentGamerCountProperty() +
                                   entry.getOpenPublicGamerSlotsProperty() +
                                   entry.getOpenPrivateGamerSlotsProperty();
            char line[256];
            std::snprintf(line, sizeof(line), "%s%s @ port %u  gamers:%d/%d",
                          static_cast<int>(i) == selectedIndex_ ? "> " : "  ",
                          entry.getHostGamertagProperty().c_str(), entry.GetConnectPort(),
                          entry.getCurrentGamerCountProperty(), maxGamers);
            const Color color = static_cast<int>(i) == selectedIndex_ ? Color(240, 220, 80, 255)
                                                                       : Color(200, 200, 200, 255);
            spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), color);
            y += 20.0f;
        }
        if (discovered_.empty())
        {
            spriteBatch_->DrawString(*font_, "(searching...)", Vector2(16.0f, y), Color(150, 150, 150, 255));
        }
    }

    spriteBatch_->End();
}
