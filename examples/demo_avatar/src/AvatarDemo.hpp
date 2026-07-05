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
                             Microsoft::Xna::Framework::GamerServices::AvatarBodyType::Male);
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
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model_;
    std::unique_ptr<Microsoft::Xna::Framework::GamerServices::AvatarRenderer> renderer_;

    // Which of the model's two clips (Stand0/Wave) is currently playing, and
    // how far into it — reset to zero whenever the clip changes so playback
    // always starts from the clip's own beginning, not wherever the other
    // clip happened to leave off.
    std::string currentClip_ = "Stand0";
    double clipPositionSeconds_ = 0.0;
    bool spaceWasDown_ = false;

    // Simple fixed-distance orbiting camera so the avatar is always framed;
    // Left/Right arrows rotate it around the avatar for a better look.
    float cameraYaw_ = 0.0f;
};
