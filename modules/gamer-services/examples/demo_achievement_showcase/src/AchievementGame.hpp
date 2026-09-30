#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.9: cna_demo_achievement_showcase. Achievement, AchievementCollection,
// SignedInGamer::AwardAchievement/GetAchievements. A grid of achievement tiles shows locked/
// hidden/earned art, gamerscore badges, and EarnedDateTime; number keys 1-6 call
// AwardAchievement(key) for the first signed-in gamer and flip that tile to earned with a small
// flash animation.
//
// Each tile's earned state and EarnedDateTime come from GetAchievements(). Without a CNA account
// service the gamer is a local offline profile, whose awards are kept in the local achievement
// store, so a tile earned in one run is still earned in the next. A title that ships
// GamerServices/Achievements.json next to its executable gets every achievement's name,
// description and score from GetAchievements() as well, and AwardAchievement refuses keys that
// catalog does not define; this demo ships none, so GetAchievements() lists only the earned keys,
// the tiles take their text from the demo's own table, and every score is 0.
//
// Nobody is signed in at startup unless CNA_GAMER_SERVICES_AUTO_SIGN_IN names a profile (--smoke
// sets it when it is unset); otherwise the demo opens the Guide's sign-in pane.
class AchievementGame : public Microsoft::Xna::Framework::Game
{
public:
    AchievementGame();
    ~AchievementGame() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

private:
    // The Guide draws its panes and notifications through the game's graphics device service.
    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_{this};

    int RefreshTiles();
    void AwardTile(std::size_t index);

    Microsoft::Xna::Framework::GamerServices::GamerServicesComponent* gamerServicesComponent_ = nullptr;
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* localGamer_ = nullptr;

    std::vector<Microsoft::Xna::Framework::GamerServices::Achievement> tiles_;
    std::vector<float> flashTimers_;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;
    int smokeNextAwardIndex_ = 0;
};
