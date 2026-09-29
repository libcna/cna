#include "DualCompareDemo.hpp"

#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"
#include "common/AvatarPresetNamesEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    bool Pressed(bool down, bool& wasDown)
    {
        const bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    }
}

DualCompareDemo::DualCompareDemo()
{
    // HiDef lets the --screenshot option read the back buffer.
    graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    getComponentsProperty().Add(new GamerServicesComponent(*this));
}

DualCompareDemo::~DualCompareDemo() = default;

void DualCompareDemo::NewPair()
{
    const AvatarBodyType bodies[] = {AvatarBodyType::Female, AvatarBodyType::Male};
    for (int side = 0; side < 2; ++side)
    {
        descriptions_[side] = std::make_unique<AvatarDescription>(AvatarDescription::CreateRandom(bodies[side]));
        renderers_[side] = std::make_unique<AvatarRenderer>(descriptions_[side].get());
    }
}

void DualCompareDemo::StartPreset(std::size_t index)
{
    preset_ = index % CNAExamplesEXT::kAvatarPresets.size();
    animation_ = std::make_unique<AvatarAnimation>(CNAExamplesEXT::kAvatarPresets[preset_].value);
}

void DualCompareDemo::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
    NewPair();
    StartPreset(0);
}

void DualCompareDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);
    const auto keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Escape))
    {
        Exit();
        return;
    }
    if (Pressed(keys.IsKeyDown(Keys::Space), spaceWasDown_)) StartPreset(preset_ + 1);
    if (Pressed(keys.IsKeyDown(Keys::R), rWasDown_)) NewPair();
    if (Pressed(keys.IsKeyDown(Keys::F1), f1WasDown_)) showHelp_ = !showHelp_;
    animation_->Update(gameTime.getElapsedGameTimeProperty(), true);
    getWindowProperty().setTitleProperty(std::string("CNA Avatar Dual Compare - ") + CNAExamplesEXT::kAvatarPresets[preset_].name +
                                         "  (Space: next preset, R: new pair, F1: help)");
    if (smokeFramesLeft_ > 0 && --smokeFramesLeft_ == 0)
    {
        Exit();
    }
}

void DualCompareDemo::Draw(const GameTime& gameTime)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(96, 120, 150, 255));
    const auto& viewport = device.getViewportProperty();
    const float aspect = viewport.getHeightProperty() > 0
        ? static_cast<float>(viewport.getWidthProperty()) / static_cast<float>(viewport.getHeightProperty())
        : 1.0f;
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 1.2f, 3.4f), Vector3(0.0f, 0.95f, 0.0f), Vector3::Up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(0.75f, aspect, 0.1f, 50.0f);
    for (int side = 0; side < 2; ++side)
    {
        renderers_[side]->setWorldProperty(Matrix::CreateRotationY(side == 0 ? 0.25f : -0.25f) *
                                           Matrix::CreateTranslation(side == 0 ? -0.55f : 0.55f, 0.0f, 0.0f));
        renderers_[side]->setViewProperty(view);
        renderers_[side]->setProjectionProperty(projection);
        renderers_[side]->Draw(animation_.get());
    }
    if (showHelp_)
    {
        spriteBatch_->Begin();
        const char* lines[] = {"Female (left) and male (right), same preset", "Space: next preset, R: new pair",
                               "F1: help, Esc: quit"};
        spriteBatch_->Draw(*whitePixel_, Rectangle(8, 8, 420, 56), Color(255, 255, 255, 210));
        float y = 16.0f;
        for (const char* line : lines)
        {
            spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color::Black, 0.0f, Vector2::Zero, 1.5f, SpriteEffects::None, 0.0f);
            y += 13.0f;
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

GetTypeNameCPP(DualCompareDemo, "DualCompareDemo")
