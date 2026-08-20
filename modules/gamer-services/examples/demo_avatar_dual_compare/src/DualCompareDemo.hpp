#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

// Task 15.18: cna_demo_avatar_dual_compare. Two independent AvatarRenderer/SkinnedModelEXT
// instances alive and drawing simultaneously - not yet exercised anywhere else, since every
// other avatar demo/test uses exactly one of each. Male and female avatars stand side-by-side
// with visibly different appearance tints (proving per-instance appearance isolation, not shared
// global state); 1/2 selects which avatar is active, Space cycles the active avatar's own
// animation clip independently of the other.
class DualCompareDemo : public Microsoft::Xna::Framework::Game
{
public:
    DualCompareDemo();
    ~DualCompareDemo() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

    /** @brief Forces the F1 help overlay's initial visibility - Task 8 (plans/plan_net.md Phase 8):
     *  lets a non-interactive smoke/screenshot run verify the overlay actually renders. */
    void SetShowHelpForTestingEXT(bool visible) { showHelpEXT_ = visible; }

    /** @brief Saves a PNG of the backbuffer on the final smoke frame - Task 8 (plans/plan_net.md
     *  Phase 8), reusing Task 7.1's own examples/common/ScreenshotEXT.hpp helper. */
    void SetScreenshotPathEXT(std::string path) { screenshotPathEXT_ = std::move(path); }

private:
    struct AvatarSlot
    {
        Microsoft::Xna::Framework::GamerServices::AvatarBodyType gender;
        float worldX;
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model;
        std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer;
        std::vector<std::string> clipNames;
        std::size_t currentClipIndex = 0;
        double clipPositionSeconds = 0.0;
    };

    void LoadSlot(AvatarSlot& slot);
    void AdvanceSlotClip(int slotIndex);

    AvatarSlot slots_[2];
    int activeSlot_ = 0;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    float cameraYaw_ = 0.0f;

    int smokeFramesLeft_ = -1;

    // Task 8 (plans/plan_net.md Phase 8): F1 help overlay - same established pattern as demo_avatar's
    // own AvatarDemo (see that file's own comment for why this is a deliberate per-demo copy).
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
    std::string screenshotPathEXT_;
};
