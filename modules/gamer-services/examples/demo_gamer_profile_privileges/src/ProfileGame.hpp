#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.13: cna_demo_gamer_profile_privileges. GamerProfile (GamerScore, GamerZone, Motto,
// Region, Reputation, TitlesPlayed, TotalAchievements, via Gamer::GetProfile()) and
// GamerPrivileges. Left/Right cycles through the signed-in gamers, showing each one's profile card
// and privilege flags.
//
// Without a CNA account service the gamers are local offline profiles: GetProfile() reports the
// GamerScore, TotalAchievements and TitlesPlayed of the achievements that profile earned in this
// title's local store, GamerZone Unknown, Reputation 0 and an empty Motto, and the privileges deny
// online sessions and content purchases. A gamer signed in to a CNA account gets its profile and
// its online-session privilege from the service.
//
// Nobody is signed in at startup unless CNA_GAMER_SERVICES_AUTO_SIGN_IN names profiles (up to
// four; --smoke signs in two when it is unset); otherwise the demo opens the Guide's sign-in pane.
class ProfileGame : public Microsoft::Xna::Framework::Game
{
public:
    ProfileGame();
    ~ProfileGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    // The Guide draws its panes and notifications through the game's graphics device service.
    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};

    void SelectGamer(int index);

    Microsoft::Xna::Framework::GamerServices::GamerServicesComponent* gamerServicesComponent_ = nullptr;
    std::vector<Microsoft::Xna::Framework::GamerServices::SignedInGamer*> gamers_;
    int selectedIndex_ = 0;
    Microsoft::Xna::Framework::GamerServices::GamerProfile* currentProfile_ = nullptr;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
};
