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
};
