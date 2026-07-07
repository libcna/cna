#include "ProfileGame.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerZone.hpp"
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
    selectedIndex_ = ((index % static_cast<int>(gamers_.size())) + static_cast<int>(gamers_.size())) %
                      static_cast<int>(gamers_.size());
    if (currentProfile_ != nullptr)
    {
        currentProfile_->Dispose();
        delete currentProfile_;
    }
    currentProfile_ = gamers_[static_cast<std::size_t>(selectedIndex_)]->GetProfile();
}

void ProfileGame::Initialize()
{
    Game::Initialize();

    const auto* signedIn = Gamer::getSignedInGamersProperty();
    for (int i = 0; i < signedIn->getCountProperty(); ++i)
    {
        gamers_.push_back((*signedIn)[i]);
    }
    SelectGamer(0);

    std::printf("[Profile] %zu signed-in gamers. GamerProfile/GamerPrivileges are both built via "
                "hardcoded CreateInternal() with no per-gamer configuration - every gamer's card "
                "and privilege flags below are expected to be identical.\n", gamers_.size());
}

void ProfileGame::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = MakeSimpleFont(device);
}

void ProfileGame::Update(GameTime& /*gameTime*/)
{
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
    // (matching the established Phase 15 deterministic-nudge convention).
    if (smokeFramesLeft_ >= 0 && smokeFramesLeft_ % 30 == 0)
    {
        SelectGamer(selectedIndex_ + 1);
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            const GamerPrivileges& priv = gamers_[static_cast<std::size_t>(selectedIndex_)]->getPrivilegesProperty();
            std::printf("[Profile] Smoke test complete: selectedIndex=%d gamerScore=%d gamerZone=%s "
                        "allowCommunication=%s\n",
                        selectedIndex_, currentProfile_->getGamerScoreProperty(),
                        GamerZoneName(currentProfile_->getGamerZoneProperty()),
                        PrivilegeSettingName(priv.getAllowCommunicationProperty()));
            Exit();
        }
    }
}

void ProfileGame::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    spriteBatch_->Begin();

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
