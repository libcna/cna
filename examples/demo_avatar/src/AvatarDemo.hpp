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

    GetTypeNameHPP()

private:
    using Keys = Microsoft::Xna::Framework::Input::Keys;
    using KbState = Microsoft::Xna::Framework::Input::KeyboardState;

    Microsoft::Xna::Framework::GamerServices::AvatarBodyType bodyType_;
    std::string wardrobeHairStyle_;
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;

    // All 11 AvatarAnimationPreset clips baked into the demo's Content (Task
    // 11.15: Stand0/Stand1/Wave/Clap/Celebrate; Task 11.23a: Stand2-Stand7) —
    // Space cycles forward through this list. clipPositionSeconds_ resets to
    // zero on every change so playback always starts from the clip's own
    // beginning, not wherever the previous clip happened to leave off.
    std::vector<std::string> clipNames_{
        "Stand0", "Stand1", "Stand2", "Stand3", "Stand4", "Stand5", "Stand6", "Stand7",
        "Wave", "Clap", "Celebrate",
    };
    std::size_t currentClipIndex_ = 0;
    double clipPositionSeconds_ = 0.0;
    bool spaceWasDown_ = false;

    // Simple fixed-distance orbiting camera so the avatar is always framed;
    // Left/Right arrows rotate it around the avatar for a better look.
    float cameraYaw_ = 0.0f;
};
