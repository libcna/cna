#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
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
// AwardAchievement(key) and flip that tile to earned with a small flash animation.
//
// Honest scope note: SignedInGamer::AwardAchievement() is a real, confirmed no-op on this
// platform (empty body, matching FNA's own stub), and GetAchievements() always returns an empty
// AchievementCollection regardless of what was "awarded" (matching FNA's own stub too) - so the
// grid this demo displays and mutates is the demo's *own* local Achievement state, built and
// rebuilt via Achievement::CreateInternal (Achievement has no isEarned setter - it's immutable
// once constructed, so "earning" one means constructing a brand-new Achievement value with
// earned=true and swapping it in), not derived from GetAchievements(). The demo calls the real
// AwardAchievement()/GetAchievements() anyway and prints proof that GetAchievements() still comes
// back empty, rather than silently working around the gap.
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
