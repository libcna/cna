#pragma once

#include <memory>
#include <string>

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

// Task 15.19: cna_demo_avatar_multi_attach_stress. An interactive, human-drivable version of
// avatar_attach_part_integration_test.cpp's idea. Each keypress attaches one more standalone
// wardrobe piece via SkinnedModelEXT::AttachPartEXT (hair variants first, then a growing series
// of uniquely-named synthetic quad "accessories"), with an on-screen Parts.size() counter, proving
// accumulation doesn't break skinning/tinting as part count grows.
class StressDemo : public Microsoft::Xna::Framework::Game
{
public:
    StressDemo();
    ~StressDemo() override;

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
    void AttachNext();
    void AttachSyntheticAccessory();

    Microsoft::Xna::Framework::GamerServices::AvatarBodyType gender_;
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;

    int attachCount_ = 0;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeys_;
    float cameraYaw_ = 0.0f;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;

    int smokeFramesLeft_ = -1;

    // Task 8 (plans/plan_net.md Phase 8): F1 help overlay - same established pattern as demo_avatar's
    // own AvatarDemo (see that file's own comment for why this is a deliberate per-demo copy).
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
    std::string screenshotPathEXT_;
};
