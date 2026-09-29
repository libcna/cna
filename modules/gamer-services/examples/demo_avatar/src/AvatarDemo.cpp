#include "AvatarDemo.hpp"

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

    const auto& kPresets = CNAExamplesEXT::kAvatarPresets;

    constexpr const char* kHelpLines[] = {
        "CNA Avatar Demo (standard XNA avatar API)",
        "",
        "Space: next animation preset",
        "R: new random avatar",
        "G: switch body type",
        "E: cycle an expression override",
        "Left/Right: rotate camera",
        "F1: show/hide help, Esc: quit",
        "",
        "--gender male|female  --clip <preset>",
        "--smoke N  --yaw <deg>  --screenshot <png>",
    };

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

void AvatarDemo::NewAvatar()
{
    // A new description gets a new renderer, which shows the loading effect until it is ready.
    description_ = std::make_unique<AvatarDescription>(AvatarDescription::CreateRandom(bodyType_));
    renderer_ = std::make_unique<AvatarRenderer>(description_.get(), true);
    renderer_->setLightDirectionProperty(Vector3(-0.4f, -0.5f, -0.75f));
    renderer_->setLightColorProperty(Vector3(0.75f, 0.72f, 0.68f));
    renderer_->setAmbientLightColorProperty(Vector3(0.38f, 0.40f, 0.45f));
}

void AvatarDemo::StartPreset(std::size_t index)
{
    presetIndex_ = index % kPresets.size();
    animation_ = std::make_unique<AvatarAnimation>(kPresets[presetIndex_].value);
}

void AvatarDemo::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
    NewAvatar();
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
    if (Pressed(keys.IsKeyDown(Keys::Space), spaceWasDown_)) StartPreset(presetIndex_ + 1);
    if (Pressed(keys.IsKeyDown(Keys::R), rWasDown_)) NewAvatar();
    if (Pressed(keys.IsKeyDown(Keys::G), gWasDown_))
    {
        bodyType_ = bodyType_ == AvatarBodyType::Male ? AvatarBodyType::Female : AvatarBodyType::Male;
        NewAvatar();
    }
    if (Pressed(keys.IsKeyDown(Keys::E), eWasDown_)) expressionOverride_ = expressionOverride_ >= 13 ? -1 : expressionOverride_ + 1;
    if (Pressed(keys.IsKeyDown(Keys::F1), f1WasDown_)) showHelp_ = !showHelp_;

    animation_->Update(gameTime.getElapsedGameTimeProperty(), true);

    const std::string state = renderer_->getStateProperty() == AvatarRendererState::Ready ? "" : " (loading)";
    getWindowProperty().setTitleProperty(std::string("CNA Avatar Demo - ") + kPresets[presetIndex_].name + " - " +
        (description_->getBodyTypeProperty() == AvatarBodyType::Male ? "male " : "female ") +
        std::to_string(description_->getHeightProperty()).substr(0, 4) + " m" + state + "  (F1: help)");

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
    const float height = std::max(description_->getHeightProperty(), 1.4f);
    const Vector3 target(0.0f, height * 0.52f, 0.0f);
    const Vector3 eye(3.0f * std::sin(cameraYaw_), height * 0.6f, 3.0f * std::cos(cameraYaw_));
    renderer_->setWorldProperty(Matrix::getIdentityProperty());
    renderer_->setViewProperty(Matrix::CreateLookAt(eye, target, Vector3::Up));
    renderer_->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(kPi / 4.0f, aspect, 0.1f, 100.0f));

    if (expressionOverride_ < 0)
    {
        renderer_->Draw(animation_.get());
    }
    else
    {
        // Bones from the animation, face from the override: AvatarRenderer.Draw(bones, expression).
        AvatarExpression expression;
        expression.setLeftEyeProperty(static_cast<AvatarEye>(expressionOverride_));
        expression.setRightEyeProperty(static_cast<AvatarEye>(expressionOverride_));
        expression.setMouthProperty(static_cast<AvatarMouth>(expressionOverride_));
        expression.setLeftEyebrowProperty(static_cast<AvatarEyebrow>(expressionOverride_ % 5));
        expression.setRightEyebrowProperty(static_cast<AvatarEyebrow>(expressionOverride_ % 5));
        const auto bones = animation_->getBoneTransformsProperty();
        renderer_->Draw(std::vector<Matrix>(bones.begin(), bones.end()), expression);
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

    if (smokeFramesLeft_ == 1 && !screenshotPath_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPath_);
        screenshotPath_.clear();
    }
    Game::Draw(gameTime);
}

GetTypeNameCPP(AvatarDemo, "AvatarDemo")
