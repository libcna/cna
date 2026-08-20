#pragma once

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
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "System/TimeSpan.hpp"
#include "CNA/CNAHelper.hpp"

#include <memory>
#include <vector>

// Task 11.11 (Phase 11b): the first real, non-synthetic-fixture proof that
// AvatarRenderer::EnableRealRenderingEXT/DrawRealEXT (Phase 10) and the
// procedurally-generated avatar content (Phase 11a, tools/avatar_builder/ +
// tools/avatar_asset_pipeline/convert_avatar.py, Task 11.10) actually work
// together: loads Content/avatar/<gender>/avatar.skinnedmodel.json via
// ContentManager and draws it, animated, in a real window — not another
// headless pixel-readback integration test (see
// examples/avatar_real_render_integration_test.cpp for that).
//
// Task 11.12: which body loads is driven by an AvatarBodyType passed to the
// constructor (see Main.cpp's --gender flag), mapped to a ContentManager
// asset name via AvatarBodyTypeToContentNameEXT — not by AvatarDescription,
// whose faithful getBodyTypeProperty() never carries real body-type data.
class AvatarDemo : public Microsoft::Xna::Framework::Game
{
public:
    explicit AvatarDemo(Microsoft::Xna::Framework::GamerServices::AvatarBodyType bodyType =
                             Microsoft::Xna::Framework::GamerServices::AvatarBodyType::Male,
                         std::string wardrobeHairStyle = "");
    ~AvatarDemo() override;

    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    /** @brief Enables smoke-test mode: exit cleanly after @p n Draw frames. */
    void SetSmokeFrames(int n) { smokeFramesLeft_ = n; }

    /** @brief Fixes the orbiting camera at a specific yaw (radians) instead of live Left/Right
     *  control - Task 7.1 (plans/plan_net.md Phase 7): reproducible front/side/back baseline captures. */
    void SetFixedCameraYawEXT(float yawRadians) { cameraYaw_ = yawRadians; fixedCameraYawEXT_ = true; }

    /** @brief Saves a PNG of the backbuffer on the final smoke frame instead of just exiting -
     *  Task 7.1 (plans/plan_net.md Phase 7). No effect unless SetSmokeFrames was also called. */
    void SetScreenshotPathEXT(std::string path) { screenshotPathEXT_ = std::move(path); }

    /** @brief Forces the F1 help overlay's initial visibility instead of the default hidden -
     *  Task 8.5 (plans/plan_net.md Phase 8): lets a non-interactive smoke/screenshot run verify the
     *  overlay actually renders without needing simulated keyboard input. */
    void SetShowHelpForTestingEXT(bool visible) { showHelpEXT_ = visible; }

    /** @brief Selects a starting clip by name instead of the default clipNames_[0] - Task 7.1
     *  (plans/plan_net.md Phase 7): lets baseline/after captures target a bent-limb pose (e.g. "Wave"),
     *  not just the default T-pose, to reveal joint deformation. No-op if name isn't found. */
    void SetInitialClipEXT(const std::string& name)
    {
        for (std::size_t i = 0; i < clipNames_.size(); ++i)
        {
            if (clipNames_[i] == name) { currentClipIndex_ = i; break; }
        }
    }

    GetTypeNameHPP()

private:
    using Keys = Microsoft::Xna::Framework::Input::Keys;
    using KbState = Microsoft::Xna::Framework::Input::KeyboardState;

    Microsoft::Xna::Framework::GamerServices::AvatarBodyType bodyType_;
    std::string wardrobeHairStyle_;
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;

    // Every AvatarAnimationPreset clip baked into the demo's Content for the
    // selected gender (Task 11.15: Stand0/Stand1/Wave/Clap/Celebrate; Task
    // 11.23a: Stand2-Stand7; Task 11.23b/11.23c: gendered Female*/Male*
    // presets, appended in the constructor since only one gender's set is
    // ever baked into a given body) — Space cycles forward through this
    // list. clipPositionSeconds_ resets to zero on every change so playback
    // always starts from the clip's own beginning, not wherever the
    // previous clip happened to leave off.
    std::vector<std::string> clipNames_;
    std::size_t currentClipIndex_ = 0;
    double clipPositionSeconds_ = 0.0;
    bool spaceWasDown_ = false;

    // Simple fixed-distance orbiting camera so the avatar is always framed;
    // Left/Right arrows rotate it around the avatar for a better look.
    float cameraYaw_ = 0.0f;
    bool fixedCameraYawEXT_ = false;

    int smokeFramesLeft_ = -1;
    std::string screenshotPathEXT_;

    // Task 8.1-8.3 (plans/plan_net.md Phase 8): F1 help overlay - the same SpriteBatch/1x1-white-
    // Texture2D/runtime-built-SpriteFont pattern already duplicated across 11+ other demos (see
    // demo_gamer_roster_hud/src/RosterGame.cpp's own MakeSimpleFont for the reference copy this
    // was copied from); no shared examples/common/ header exists for this, and this plan
    // deliberately follows that established per-demo-copy convention rather than introducing one.
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> whitePixel_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    bool showHelpEXT_ = false;
    bool f1WasDownEXT_ = false;
};
