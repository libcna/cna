#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/TimeSpan.hpp"

#include <memory>
#include <vector>

// Task 15.15: cna_demo_avatar_animation_gallery. A completionist version of demo_avatar's
// Space-cycling: programmatically iterates all 31 AvatarAnimationPreset values (not a
// hand-picked subset), resolving each via AvatarAnimationPresetToClipNameEXT, auto-playing/
// labeling each for ~2 seconds before advancing. Since Female*/Male* clips are only baked into
// their own gender's content (Task 11.23b/11.23c), a preset incompatible with the currently
// loaded gender is skipped instantly (0 display time) rather than attempting to draw a
// nonexistent clip; gender switches and content reloads once a full 31-preset pass completes, so
// both Male* and Female* presets eventually play against their own gender's baked clips over
// consecutive passes. Reuses demo_avatar's window/camera/renderer setup and its Content/
// directory (copied by CMake, same as demo_avatar) rather than duplicating avatar assets.
class GalleryDemo : public Microsoft::Xna::Framework::Game
{
public:
    GalleryDemo();
    ~GalleryDemo() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

    /** @brief Forces the F1 help overlay's initial visibility - Task 8.5 (plans/plan_net.md Phase 8):
     *  lets a non-interactive smoke/screenshot run verify the overlay actually renders. */
    void SetShowHelpForTestingEXT(bool visible) { showHelpEXT_ = visible; }

    /** @brief Saves a PNG of the backbuffer on the final smoke frame - Task 8.5 (plans/plan_net.md
     *  Phase 8), reusing Task 7.1's own examples/common/ScreenshotEXT.hpp helper. */
    void SetScreenshotPathEXT(std::string path) { screenshotPathEXT_ = std::move(path); }

private:
    void LoadContentForCurrentGender();
    void AdvanceToNextCompatiblePreset();
    bool IsCompatibleWithCurrentGender(const std::string& clipName) const;

    Microsoft::Xna::Framework::GamerServices::AvatarBodyType currentGender_;
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;

    std::vector<Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset> allPresets_;
    std::size_t presetIndex_ = 0;
    std::string currentClipName_;
    double clipPositionSeconds_ = 0.0;
    float cameraYaw_ = 0.0f;

    int playedCount_ = 0;
    int skippedCount_ = 0;
    int genderSwitchCount_ = 0;

    int smokeFramesLeft_ = -1;

    // Task 8.1-8.4 (plans/plan_net.md Phase 8): F1 help overlay - same established pattern as
    // demo_avatar's own AvatarDemo (see that file's own comment for why this is a deliberate
    // per-demo copy, not a missed shared-header opportunity).
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
    std::string screenshotPathEXT_;
};
