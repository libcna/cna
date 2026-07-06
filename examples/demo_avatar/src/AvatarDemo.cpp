#include "AvatarDemo.hpp"

#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::Input::Keyboard;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kPiOver4 = kPi * 0.25f;
    constexpr float kCameraDistance = 3.0f;
    constexpr float kCameraHeight = 1.0f;
    constexpr float kTargetHeight = 0.9f; // roughly chest height on our ~1.7m-tall avatar
}

AvatarDemo::AvatarDemo(AvatarBodyType bodyType, std::string wardrobeHairStyle)
    : bodyType_(bodyType)
    , wardrobeHairStyle_(std::move(wardrobeHairStyle))
    , clipNames_{"Stand0", "Stand1", "Stand2", "Stand3", "Stand4", "Stand5", "Stand6", "Stand7",
                 "Wave", "Clap", "Celebrate"}
{
    // Task 11.23b/11.23c: gendered presets are baked only into their own gender's
    // content (Female* for Female, Male* for Male) — appending them here keeps a
    // single clipNames_ list in sync with whichever body actually got loaded, instead
    // of hardcoding a fixed list that would throw on the wrong gender.
    if (bodyType_ == AvatarBodyType::Female)
    {
        for (const char* name : {"FemaleIdleCheckNails", "FemaleIdleLookAround", "FemaleIdleShiftWeight",
                                  "FemaleIdleFixShoe", "FemaleAngry", "FemaleConfused", "FemaleLaugh",
                                  "FemaleCry", "FemaleShocked", "FemaleYawn"})
        {
            clipNames_.emplace_back(name);
        }
    }
    else
    {
        for (const char* name : {"MaleIdleLookAround", "MaleIdleStretch", "MaleIdleShiftWeight",
                                  "MaleIdleCheckHand", "MaleAngry", "MaleConfused", "MaleLaugh",
                                  "MaleCry", "MaleSurprised", "MaleYawn"})
        {
            clipNames_.emplace_back(name);
        }
    }

    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

AvatarDemo::~AvatarDemo() = default;

void AvatarDemo::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);
}

void AvatarDemo::LoadContent()
{
    // AvatarBodyTypeToContentNameEXT (Task 11.12) maps bodyType_ to
    // "avatar/male/avatar" or "avatar/female/avatar", resolving to
    // Content/avatar/<gender>/avatar.skinnedmodel.json — real content produced
    // by tools/avatar_builder/generate_avatar.py + convert_avatar.py (Tasks
    // 11.1-11.10), not a synthetic fixture. ContentManager's default
    // RootDirectory ("Content") already matches where CMake copies this demo's
    // own Content/ directory next to the built executable.
    auto& content = getContentProperty();
    model_ = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(bodyType_));

    // Task 11.22: --wardrobe-hair <Style> proves SkinnedModelEXT::AttachPartEXT (Task
    // 11.21) end-to-end at runtime, not just that it compiles: load a standalone
    // wardrobe piece (Content/wardrobe/hair_<Style>/, converted independently via
    // generate_wardrobe.py + convert_avatar.py, Task 11.14) and swap it in for the
    // avatar's baked-in hair -- remove the old "CNAAvatarHair" part first, since
    // AttachPartEXT only adds a part, it doesn't replace one by name.
    if (!wardrobeHairStyle_.empty())
    {
        auto wardrobePiece = content.Load<std::shared_ptr<SkinnedModelEXT>>(
            "wardrobe/hair_" + wardrobeHairStyle_ + "/avatar");
        auto& parts = model_->Parts;
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                                    [](const SkinnedModelEXT::PartEXT& p) { return p.Name == "CNAAvatarHair"; }),
                    parts.end());
        model_->AttachPartEXT(std::move(*wardrobePiece));
    }

    auto& device = getGraphicsDeviceProperty();
    renderer_ = std::make_unique<AvatarRenderer>(nullptr);
    renderer_->EnableRealRenderingEXT(device, model_);

    // Exercise SetAppearanceEXT explicitly (rather than relying on its
    // NavajoWhite/SaddleBrown defaults) so a visible tint change is part of
    // this proof, not just the untinted default.
    AvatarAppearanceEXT appearance;
    appearance.setSkinColorProperty(Color(210, 170, 130, 255));
    appearance.setHairColorProperty(Color(40, 25, 15, 255));
    renderer_->SetAppearanceEXT(appearance);

    // AvatarRenderer's LightColor/LightDirection/AmbientLightColor default to
    // black (matching the real, never-drawing XNA implementation's untouched
    // value-type defaults) — a real caller must configure these before
    // DrawRealEXT does anything visible, same as
    // examples/avatar_real_render_integration_test.cpp.
    renderer_->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f));
    renderer_->setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    renderer_->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));

    getWindowProperty().setTitleProperty(
        (bodyType_ == AvatarBodyType::Female ? "CNA Avatar Demo [female] - " : "CNA Avatar Demo [male] - ")
        + clipNames_[currentClipIndex_] + "  (Space: next anim, Left/Right: rotate, Esc: quit)");
}

void AvatarDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape)) { Exit(); return; }

    const float dt = static_cast<float>(
        gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

    const float rotSpeed = 1.6f * dt;
    if (kb.IsKeyDown(Keys::Left))  cameraYaw_ -= rotSpeed;
    if (kb.IsKeyDown(Keys::Right)) cameraYaw_ += rotSpeed;

    const bool spaceDown = kb.IsKeyDown(Keys::Space);
    if (spaceDown && !spaceWasDown_) {
        currentClipIndex_ = (currentClipIndex_ + 1) % clipNames_.size();
        clipPositionSeconds_ = 0.0;
        getWindowProperty().setTitleProperty(
            (bodyType_ == AvatarBodyType::Female ? "CNA Avatar Demo [female] - " : "CNA Avatar Demo [male] - ")
            + clipNames_[currentClipIndex_] + "  (Space: next anim, Left/Right: rotate, Esc: quit)");
    }
    spaceWasDown_ = spaceDown;

    clipPositionSeconds_ += static_cast<double>(dt);
}

void AvatarDemo::Draw(const GameTime&)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color::CornflowerBlue);
    device.SetDepthTestEnabled(true);

    const auto& vp = device.getViewportProperty();
    const float aspect =
        (vp.getHeightProperty() > 0)
            ? static_cast<float>(vp.getWidthProperty()) / static_cast<float>(vp.getHeightProperty())
            : 1.0f;

    const Vector3 target(0.0f, kTargetHeight, 0.0f);
    const Vector3 eye(kCameraDistance * std::sin(cameraYaw_), kCameraHeight,
                       kCameraDistance * std::cos(cameraYaw_));

    renderer_->setWorldProperty(Matrix::getIdentityProperty());
    renderer_->setViewProperty(Matrix::CreateLookAt(eye, target, Vector3::Up));
    renderer_->setProjectionProperty(
        Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 100.0f));

    renderer_->DrawRealEXT(clipNames_[currentClipIndex_], System::TimeSpan::FromSeconds(clipPositionSeconds_), /*loop=*/true);
}

GetTypeNameCPP(AvatarDemo, "AvatarDemo")
