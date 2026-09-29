#include "ProfileGame.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerZone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "System/Globalization/RegionInfo.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::GamerServices;

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

    const char* GamerZoneName(GamerZone zone)
    {
        switch (zone)
        {
            case GamerZone::Unknown: return "Unknown";
            case GamerZone::Recreation: return "Recreation";
            case GamerZone::Pro: return "Pro";
            case GamerZone::Family: return "Family";
            case GamerZone::Underground: return "Underground";
        }
        return "?";
    }

    const char* PrivilegeSettingName(GamerPrivilegeSetting setting)
    {
        switch (setting)
        {
            case GamerPrivilegeSetting::Blocked: return "Blocked";
            case GamerPrivilegeSetting::FriendsOnly: return "FriendsOnly";
            case GamerPrivilegeSetting::Everyone: return "Everyone";
        }
        return "?";
    }
}

ProfileGame::ProfileGame()
{
    gamerServicesComponent_ = new GamerServicesComponent(*this);
    getComponentsProperty().Add(gamerServicesComponent_);
}

ProfileGame::~ProfileGame()
{
    if (currentProfile_ != nullptr)
    {
        currentProfile_->Dispose();
        delete currentProfile_;
    }
    delete gamerServicesComponent_;
}

void ProfileGame::SelectGamer(int index)
{
    if (currentProfile_ != nullptr)
    {
        currentProfile_->Dispose();
        delete currentProfile_;
        currentProfile_ = nullptr;
    }
    if (gamers_.empty())
    {
        selectedIndex_ = 0;
        return;
    }
    selectedIndex_ = ((index % static_cast<int>(gamers_.size())) + static_cast<int>(gamers_.size())) %
                      static_cast<int>(gamers_.size());
    currentProfile_ = gamers_[static_cast<std::size_t>(selectedIndex_)]->GetProfile();
}

void ProfileGame::Initialize()
{
    Game::Initialize();
}

void ProfileGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void ProfileGame::Update(GameTime& gameTime)
{
    // Runs the GamerServicesComponent, whose GamerServicesDispatcher.Update signs gamers in.
    Game::Update(gameTime);

    const auto* signedIn = Gamer::getSignedInGamersProperty();
    std::vector<SignedInGamer*> current;
    for (int i = 0; i < signedIn->getCountProperty(); ++i)
    {
        current.push_back((*signedIn)[i]);
    }
    if (current != gamers_)
    {
        gamers_ = std::move(current);
        SelectGamer(selectedIndex_);
        std::printf("[Profile] %zu signed-in gamer(s).\n", gamers_.size());
    }
    if (gamers_.empty() && !Guide::getIsVisibleProperty())
    {
        Guide::ShowSignIn(1, false);
    }

    KeyboardState keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Right) && !previousKeys_.IsKeyDown(Keys::Right))
    {
        SelectGamer(selectedIndex_ + 1);
    }
    if (keys.IsKeyDown(Keys::Left) && !previousKeys_.IsKeyDown(Keys::Left))
    {
        SelectGamer(selectedIndex_ - 1);
    }
    previousKeys_ = keys;

    // Smoke-test mode has no real keyboard driving it - deterministically cycle every 30 frames
    // (matching the established Phase 15 deterministic-nudge convention). Guarded by > 0, not
    // >= 0: once smokeFramesLeft_ reaches 0 it stops decrementing (see the block below), so an
    // >= 0 check here would keep re-triggering every subsequent frame - Exit() does not halt
    // Update() immediately (Task 15.14's own discovery of this exact bug class).
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 30 == 0 && !gamers_.empty())
    {
        SelectGamer(selectedIndex_ + 1);
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            if (gamers_.empty())
            {
                std::printf("[Profile] Smoke test complete: nobody signed in.\n");
            }
            else
            {
                SignedInGamer* gamer = gamers_[static_cast<std::size_t>(selectedIndex_)];
                const GamerPrivileges& priv = gamer->getPrivilegesProperty();
                std::printf("[Profile] Smoke test complete: selectedIndex=%d gamertag=%s gamerScore=%d "
                            "totalAchievements=%d gamerZone=%s allowCommunication=%s "
                            "allowOnlineSessions=%s\n",
                            selectedIndex_, gamer->getGamertagProperty().c_str(),
                            currentProfile_->getGamerScoreProperty(),
                            currentProfile_->getTotalAchievementsProperty(),
                            GamerZoneName(currentProfile_->getGamerZoneProperty()),
                            PrivilegeSettingName(priv.getAllowCommunicationProperty()),
                            priv.getAllowOnlineSessionsProperty() ? "true" : "false");
            }
            Exit();
        }
    }
}

void ProfileGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();

    if (gamers_.empty())
    {
        spriteBatch_->DrawString(*font_, "Nobody is signed in.", Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));
        spriteBatch_->End();
        return;
    }

    SignedInGamer* gamer = gamers_[static_cast<std::size_t>(selectedIndex_)];
    char header[128];
    std::snprintf(header, sizeof(header), "Gamer %d/%zu: %s (Left/Right to cycle)",
                  selectedIndex_ + 1, gamers_.size(), gamer->getGamertagProperty().c_str());
    spriteBatch_->DrawString(*font_, header, Vector2(16.0f, 16.0f), Color(255, 255, 255, 255));

    float y = 48.0f;
    char line[192];
    const std::string motto = currentProfile_->getMottoProperty().empty()
                                   ? "(empty)" : currentProfile_->getMottoProperty();
    std::snprintf(line, sizeof(line), "GamerScore: %d", currentProfile_->getGamerScoreProperty());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "GamerZone: %s", GamerZoneName(currentProfile_->getGamerZoneProperty()));
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "Motto: %s", motto.c_str());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "Region: %s", currentProfile_->getRegionProperty().getNameProperty().c_str());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "Reputation: %.1f", currentProfile_->getReputationProperty());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "TitlesPlayed: %d", currentProfile_->getTitlesPlayedProperty());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "TotalAchievements: %d", currentProfile_->getTotalAchievementsProperty());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(200, 220, 255, 255)); y += 26.0f;

    const GamerPrivileges& priv = gamer->getPrivilegesProperty();
    spriteBatch_->DrawString(*font_, "Privileges:", Vector2(16.0f, y), Color(255, 255, 255, 255)); y += 18.0f;
    std::snprintf(line, sizeof(line), "AllowCommunication: %s", PrivilegeSettingName(priv.getAllowCommunicationProperty()));
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255)); y += 16.0f;
    std::snprintf(line, sizeof(line), "AllowOnlineSessions: %s", priv.getAllowOnlineSessionsProperty() ? "true" : "false");
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255)); y += 16.0f;
    std::snprintf(line, sizeof(line), "AllowPremiumContent: %s", priv.getAllowPremiumContentProperty() ? "true" : "false");
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255)); y += 16.0f;
    std::snprintf(line, sizeof(line), "AllowProfileViewing: %s", PrivilegeSettingName(priv.getAllowProfileViewingProperty()));
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255)); y += 16.0f;
    std::snprintf(line, sizeof(line), "AllowPurchaseContent: %s", priv.getAllowPurchaseContentProperty() ? "true" : "false");
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255)); y += 16.0f;
    std::snprintf(line, sizeof(line), "AllowTradeContent: %s", priv.getAllowTradeContentProperty() ? "true" : "false");
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255)); y += 16.0f;
    std::snprintf(line, sizeof(line), "AllowUserCreatedContent: %s", PrivilegeSettingName(priv.getAllowUserCreatedContentProperty()));
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color(180, 220, 180, 255));

    spriteBatch_->End();
}
