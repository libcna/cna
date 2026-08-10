#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
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
// Region, Reputation, TitlesPlayed, TotalAchievements, via Gamer::GetProfile() -> CreateInternal)
// and GamerPrivileges. Left/Right cycles through the 4 stub SignedInGamers, showing each one's
// profile card and privilege flags.
//
// Honest scope note: GamerProfile::CreateInternal() and GamerPrivileges::CreateInternal() both
// build fixed, hardcoded default values with no per-gamer configuration anywhere in either
// class (confirmed by reading both .cpp constructors before writing this demo) - so all 4 stub
// gamers show identical profile/privilege values. This demo displays that real, current behavior
// honestly instead of fabricating per-gamer variation the real API has no way to produce.
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
