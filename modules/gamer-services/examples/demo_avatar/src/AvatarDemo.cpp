#include "AvatarDemo.hpp"

#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"
#include "common/AvatarPresetNamesEXT.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    // How far apart a lineup stands, and how many a lineup shows.
    constexpr float kSpacing = 0.9f;
    constexpr int kLineup = 5;

    const auto& kPresets = CNAExamplesEXT::kAvatarPresets;

    constexpr const char* kHelpLines[] = {
        "CNA Avatar Demo (standard XNA avatar API)",
        "",
        "Space / Backspace: next / previous animation preset",
        "R: new random avatars    G: switch body type",
        "L: one avatar or a lineup    Tab: next avatar of the lineup",
        "V: view (body, head, hands, feet)    K: lighting",
        "E: cycle an expression override    P: pause",
        "Left/Right: turn    Up/Down: tilt    PgUp/PgDn: zoom",
        "F1: show/hide help, Esc: quit",
        "",
        "--gender male|female  --clip <preset>  --lineup N",
        "--view body|head|hands|feet  --description <hex|file>",
        "--smoke N  --yaw <deg>  --screenshot <png>",
    };

    // Lighting a game might use: AvatarRenderer takes exactly a key light and an ambient colour.
    struct Lighting
    {
        const char* name;
        Vector3 direction, color, ambient;
    };
    const std::array<Lighting, 4> kLightings{{
        {"studio", Vector3(-0.4f, -0.5f, -0.75f), Vector3(0.75f, 0.72f, 0.68f), Vector3(0.38f, 0.40f, 0.45f)},
        {"daylight", Vector3(0.3f, -0.85f, -0.4f), Vector3(0.90f, 0.88f, 0.80f), Vector3(0.30f, 0.33f, 0.40f)},
        {"sunset", Vector3(0.85f, -0.25f, -0.45f), Vector3(0.95f, 0.62f, 0.38f), Vector3(0.22f, 0.22f, 0.36f)},
        {"dim room", Vector3(0.0f, -1.0f, -0.2f), Vector3(0.38f, 0.37f, 0.40f), Vector3(0.18f, 0.18f, 0.22f)},
    }};

    constexpr std::array<const char*, 4> kViewNames{"body", "head", "hands", "feet"};

    bool Pressed(bool down, bool& wasDown)
    {
        const bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    }
}

AvatarDemo::AvatarDemo(AvatarBodyType bodyType)
    : bodyType_(bodyType)
{
    // HiDef lets the --screenshot option read the back buffer.
    graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    getComponentsProperty().Add(new GamerServicesComponent(*this));
    setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(166667));
}

AvatarDemo::~AvatarDemo() = default;

void AvatarDemo::SetInitialPresetEXT(const std::string& name)
{
    for (std::size_t index = 0; index < kPresets.size(); ++index)
    {
        if (name == kPresets[index].name)
        {
            presetIndex_ = index;
        }
    }
}

void AvatarDemo::ApplyLighting(AvatarRenderer& renderer) const
{
    const auto& lighting = kLightings[static_cast<std::size_t>(lighting_)];
    renderer.setLightDirectionProperty(Vector3::Normalize(lighting.direction));
    renderer.setLightColorProperty(lighting.color);
    renderer.setAmbientLightColorProperty(lighting.ambient);
}

void AvatarDemo::NewAvatars()
{
    // A new description gets a new renderer, which shows the loading effect until it is ready. A
    // lineup alternates body types, starting with the chosen one.
    shown_.clear();
    const int count = given_ ? 1 : std::max(1, lineup_);
    const auto other = bodyType_ == AvatarBodyType::Male ? AvatarBodyType::Female : AvatarBodyType::Male;
    for (int index = 0; index < count; ++index)
    {
        Shown entry;
        entry.description = given_ ? std::make_unique<AvatarDescription>(*given_)
                                   : std::make_unique<AvatarDescription>(AvatarDescription::CreateRandom(index % 2 == 0 ? bodyType_ : other));
        entry.renderer = std::make_unique<AvatarRenderer>(entry.description.get(), true);
        ApplyLighting(*entry.renderer);
        shown_.push_back(std::move(entry));
    }
}

void AvatarDemo::StartPreset(std::size_t index)
{
    presetIndex_ = index % kPresets.size();
    animation_ = std::make_unique<AvatarAnimation>(kPresets[presetIndex_].value);
}

Vector3 AvatarDemo::FocusOf(const Shown& shown, float x) const
{
    const float height = std::max(shown.description->getHeightProperty(), 1.4f);
    auto& renderer = *shown.renderer;
    if (view_ == View::Body || renderer.getStateProperty() != AvatarRendererState::Ready)
    {
        return Vector3(x, height * 0.52f, 0.0f);
    }
    // The joints as animated now, the XNA way (BoneTransforms[i] * BindPose[i] * parent), in XNA's
    // avatar space, turned the half turn the avatar is drawn with.
    const auto parents = renderer.getParentBonesProperty();
    const auto bind = renderer.getBindPoseProperty();
    const auto bones = animation_->getBoneTransformsProperty();
    std::vector<Matrix> joints(static_cast<std::size_t>(bind.getCountProperty()));
    for (int bone = 0; bone < bind.getCountProperty(); ++bone)
    {
        const auto local = bones[static_cast<std::size_t>(bone)] * bind[bone];
        joints[static_cast<std::size_t>(bone)] = parents[bone] < 0 ? local : local * joints[static_cast<std::size_t>(parents[bone])];
    }
    auto at = [&](AvatarBone bone) {
        const auto p = joints[static_cast<std::size_t>(bone)].getTranslationProperty();
        return Vector3(x - p.X, p.Y, -p.Z);
    };
    switch (view_)
    {
    case View::Head: {
        auto head = at(AvatarBone::Head);
        head.Y += (height - head.Y) * 0.45f;
        return head;
    }
    case View::Hands: {
        // Between the wrist and the middle finger's tip: the whole hand.
        return at(AvatarBone::WristRight) * 0.45f + at(AvatarBone::FingerMiddle3Right) * 0.55f;
    }
    case View::Feet: {
        auto feet = (at(AvatarBone::AnkleLeft) + at(AvatarBone::AnkleRight)) * 0.5f;
        feet.Y *= 0.6f;
        return feet;
    }
    case View::Body:
        break;
    }
    return Vector3(x, height * 0.52f, 0.0f);
}

float AvatarDemo::StandAt(int index) const
{
    return (static_cast<float>(index) - static_cast<float>(static_cast<int>(shown_.size()) - 1) * 0.5f) * kSpacing;
}

void AvatarDemo::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
    NewAvatars();
    StartPreset(presetIndex_);
}

void AvatarDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);
    const auto keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Escape))
    {
        Exit();
        return;
    }
    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    if (!fixedCameraYaw_)
    {
        if (keys.IsKeyDown(Keys::Left)) cameraYaw_ -= 1.6f * dt;
        if (keys.IsKeyDown(Keys::Right)) cameraYaw_ += 1.6f * dt;
    }
    if (keys.IsKeyDown(Keys::Up)) cameraPitch_ = std::min(cameraPitch_ + 0.8f * dt, 0.9f);
    if (keys.IsKeyDown(Keys::Down)) cameraPitch_ = std::max(cameraPitch_ - 0.8f * dt, -0.5f);
    if (keys.IsKeyDown(Keys::PageUp)) zoom_ = std::max(zoom_ * (1.0f - 1.2f * dt), 0.35f);
    if (keys.IsKeyDown(Keys::PageDown)) zoom_ = std::min(zoom_ * (1.0f + 1.2f * dt), 3.0f);
    if (Pressed(keys.IsKeyDown(Keys::Space), spaceWasDown_)) StartPreset(presetIndex_ + 1);
    if (Pressed(keys.IsKeyDown(Keys::Back), backWasDown_)) StartPreset(presetIndex_ + kPresets.size() - 1);
    if (Pressed(keys.IsKeyDown(Keys::R), rWasDown_)) NewAvatars();
    if (Pressed(keys.IsKeyDown(Keys::G), gWasDown_))
    {
        bodyType_ = bodyType_ == AvatarBodyType::Male ? AvatarBodyType::Female : AvatarBodyType::Male;
        NewAvatars();
    }
    if (Pressed(keys.IsKeyDown(Keys::L), lWasDown_) && !given_)
    {
        lineup_ = lineup_ > 1 ? 1 : kLineup;
        focus_ = lineup_ / 2;
        NewAvatars();
    }
    if (Pressed(keys.IsKeyDown(Keys::Tab), tabWasDown_) && !shown_.empty()) focus_ = (focus_ + 1) % static_cast<int>(shown_.size());
    if (Pressed(keys.IsKeyDown(Keys::V), vWasDown_)) view_ = static_cast<View>((static_cast<int>(view_) + 1) % 4);
    if (Pressed(keys.IsKeyDown(Keys::K), kWasDown_))
    {
        lighting_ = (lighting_ + 1) % static_cast<int>(kLightings.size());
        for (auto& shown : shown_) ApplyLighting(*shown.renderer);
    }
    if (Pressed(keys.IsKeyDown(Keys::P), pWasDown_)) paused_ = !paused_;
    if (Pressed(keys.IsKeyDown(Keys::E), eWasDown_)) expressionOverride_ = expressionOverride_ >= 13 ? -1 : expressionOverride_ + 1;
    if (Pressed(keys.IsKeyDown(Keys::F1), f1WasDown_)) showHelp_ = !showHelp_;

    if (!paused_) animation_->Update(gameTime.getElapsedGameTimeProperty(), true);

    // The camera eases to what it looks at, so a close view follows a hand or the head smoothly.
    const int count = static_cast<int>(shown_.size());
    const bool lineupInView = view_ == View::Body && count > 1;
    focus_ = std::clamp(focus_, 0, std::max(0, count - 1));
    const Vector3 wanted = lineupInView ? Vector3(0.0f, std::max(shown_[0].description->getHeightProperty(), 1.4f) * 0.52f, 0.0f)
                                        : FocusOf(shown_[static_cast<std::size_t>(focus_)], StandAt(focus_));
    target_ = haveTarget_ ? Vector3::Lerp(target_, wanted, 1.0f - std::exp(-6.0f * dt)) : wanted;
    haveTarget_ = true;

    const auto& shown = shown_[static_cast<std::size_t>(focus_)];
    const std::string state = shown.renderer->getStateProperty() == AvatarRendererState::Ready ? "" : " (loading)";
    getWindowProperty().setTitleProperty(std::string("CNA Avatar Demo - ") + kPresets[presetIndex_].name + " - " +
        (shown.description->getBodyTypeProperty() == AvatarBodyType::Male ? "male " : "female ") +
        std::to_string(shown.description->getHeightProperty()).substr(0, 4) + " m" + state + " - " +
        kViewNames[static_cast<std::size_t>(view_)] + ", " + kLightings[static_cast<std::size_t>(lighting_)].name +
        (shown_.size() > 1 ? ", " + std::to_string(focus_ + 1) + " of " + std::to_string(shown_.size()) : std::string()) +
        "  (F1: help)");

    if (smokeFramesLeft_ > 0 && --smokeFramesLeft_ == 0)
    {
        Exit();
    }
}

void AvatarDemo::Draw(const GameTime& gameTime)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(92, 128, 176, 255));
    const auto& viewport = device.getViewportProperty();
    const float aspect = viewport.getHeightProperty() > 0
        ? static_cast<float>(viewport.getWidthProperty()) / static_cast<float>(viewport.getHeightProperty())
        : 1.0f;
    // The lineup stands side by side along X; the camera orbits the avatar in focus.
    const int count = static_cast<int>(shown_.size());
    const bool lineupInView = view_ == View::Body && count > 1;
    const Vector3 target = target_;
    constexpr std::array<float, 4> kDistance{3.0f, 0.95f, 0.55f, 0.95f};
    constexpr std::array<float, 4> kPitch{0.05f, 0.05f, 0.35f, 0.45f};
    const float distance = (kDistance[static_cast<std::size_t>(view_)] + (lineupInView ? kSpacing * 0.6f * static_cast<float>(count - 1) : 0.0f)) * zoom_;
    const float pitch = std::clamp(kPitch[static_cast<std::size_t>(view_)] + cameraPitch_, -0.6f, 1.2f);
    const Vector3 eye = target + Vector3(std::sin(cameraYaw_) * std::cos(pitch), std::sin(pitch), std::cos(cameraYaw_) * std::cos(pitch)) * distance;
    const auto view = Matrix::CreateLookAt(eye, target, Vector3::Up);
    const auto projection = Matrix::CreatePerspectiveFieldOfView(view_ == View::Body ? kPi / 4.0f : kPi / 6.0f, aspect, 0.05f, 100.0f);

    AvatarExpression expression;
    if (expressionOverride_ >= 0)
    {
        expression.setLeftEyeProperty(static_cast<AvatarEye>(expressionOverride_));
        expression.setRightEyeProperty(static_cast<AvatarEye>(expressionOverride_));
        expression.setMouthProperty(static_cast<AvatarMouth>(expressionOverride_));
        expression.setLeftEyebrowProperty(static_cast<AvatarEyebrow>(expressionOverride_ % 5));
        expression.setRightEyebrowProperty(static_cast<AvatarEyebrow>(expressionOverride_ % 5));
    }
    for (int index = 0; index < count; ++index)
    {
        auto& renderer = *shown_[static_cast<std::size_t>(index)].renderer;
        // XNA avatars face -Z: a half turn faces them to the camera, which starts on +Z.
        renderer.setWorldProperty(Matrix::CreateRotationY(kPi) * Matrix::CreateTranslation(StandAt(index), 0.0f, 0.0f));
        renderer.setViewProperty(view);
        renderer.setProjectionProperty(projection);
        if (expressionOverride_ < 0)
        {
            renderer.Draw(animation_.get());
        }
        else
        {
            // Bones from the animation, face from the override: AvatarRenderer.Draw(bones, expression).
            const auto bones = animation_->getBoneTransformsProperty();
            renderer.Draw(std::vector<Matrix>(bones.begin(), bones.end()), expression);
        }
    }

    if (showHelp_)
    {
        constexpr float kScale = 1.5f;
        constexpr float kLine = 13.0f;
        constexpr float kPad = 12.0f;
        float widest = 0.0f;
        for (const char* line : kHelpLines)
        {
            widest = std::max(widest, font_->MeasureString(line).X * kScale);
        }
        const int lines = static_cast<int>(std::size(kHelpLines));
        const Rectangle panel(8, 8, static_cast<int>(widest + kPad * 2), static_cast<int>(lines * kLine + kPad * 2));
        spriteBatch_->Begin();
        spriteBatch_->Draw(*whitePixel_, panel, Color(255, 255, 255, 210));
        float y = panel.Y + kPad;
        for (const char* line : kHelpLines)
        {
            spriteBatch_->DrawString(*font_, line, Vector2(panel.X + kPad, y), Color(0, 0, 0, 255), 0.0f, Vector2::Zero, kScale,
                                     SpriteEffects::None, 0.0f);
            y += kLine;
        }
        spriteBatch_->End();
    }

    // The first frame drawn near the end of a smoke run: a fixed-step catch-up may update twice
    // before drawing, so waiting for exactly the last one can miss it.
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ <= 5 && !screenshotPath_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPath_);
        screenshotPath_.clear();
    }
    Game::Draw(gameTime);
}

GetTypeNameCPP(AvatarDemo, "AvatarDemo")
