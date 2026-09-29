#include "GalleryDemo.hpp"

#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"
#include "common/AvatarPresetNamesEXT.hpp"

#include <algorithm>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    constexpr int kColumns = 4;
    const Vector3 kEye(0.0f, 1.55f, 5.4f);
    const Vector3 kTarget(0.0f, 1.0f, 0.0f);

    Vector3 SlotPosition(std::size_t index)
    {
        const int column = static_cast<int>(index) % kColumns;
        const int row = static_cast<int>(index) / kColumns;
        // The back row sits between the front row's avatars so none hides another.
        return Vector3(static_cast<float>(column) - 1.5f + (row == 0 ? 0.5f : -0.5f), 0.0f, row == 0 ? -1.0f : 0.7f);
    }

    bool Pressed(bool down, bool& wasDown)
    {
        const bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    }
}

GalleryDemo::GalleryDemo()
{
    // HiDef lets the --screenshot option read the back buffer.
    graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    getComponentsProperty().Add(new GamerServicesComponent(*this));
}

GalleryDemo::~GalleryDemo() = default;

void GalleryDemo::Reroll()
{
    for (auto& slot : slots_)
    {
        slot.description = std::make_unique<AvatarDescription>(AvatarDescription::CreateRandom());
        slot.renderer = std::make_unique<AvatarRenderer>(slot.description.get());
    }
}

void GalleryDemo::ShowPage(int page)
{
    const int pages = static_cast<int>((CNAExamplesEXT::kAvatarPresets.size() + slots_.size() - 1) / slots_.size());
    page_ = (page % pages + pages) % pages;
    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        auto& slot = slots_[index];
        slot.preset = (static_cast<std::size_t>(page_) * slots_.size() + index) % CNAExamplesEXT::kAvatarPresets.size();
        slot.animation = std::make_unique<AvatarAnimation>(CNAExamplesEXT::kAvatarPresets[slot.preset].value);
    }
}

void GalleryDemo::LoadContent()
{
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, {255, 255, 255, 255}));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
    Reroll();
    ShowPage(0);
}

void GalleryDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);
    const auto keys = Keyboard::GetState();
    if (keys.IsKeyDown(Keys::Escape))
    {
        Exit();
        return;
    }
    if (Pressed(keys.IsKeyDown(Keys::Space), spaceWasDown_)) ShowPage(page_ + 1);
    if (Pressed(keys.IsKeyDown(Keys::R), rWasDown_)) Reroll();
    if (Pressed(keys.IsKeyDown(Keys::F1), f1WasDown_)) showHelp_ = !showHelp_;
    for (auto& slot : slots_)
    {
        slot.animation->Update(gameTime.getElapsedGameTimeProperty(), true);
    }
    getWindowProperty().setTitleProperty("CNA Avatar Animation Gallery - page " + std::to_string(page_ + 1) +
                                         "  (Space: next page, R: new avatars, F1: help)");
    if (smokeFramesLeft_ > 0 && --smokeFramesLeft_ == 0)
    {
        Exit();
    }
}

void GalleryDemo::Draw(const GameTime& gameTime)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(70, 96, 140, 255));
    const auto& viewport = device.getViewportProperty();
    const float width = static_cast<float>(viewport.getWidthProperty());
    const float height = static_cast<float>(viewport.getHeightProperty());
    const Matrix view = Matrix::CreateLookAt(kEye, kTarget, Vector3::Up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(0.72f, height > 0 ? width / height : 1.0f, 0.1f, 50.0f);
    // Back row first so the front row is drawn over it.
    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        auto& slot = slots_[index];
        slot.renderer->setWorldProperty(Matrix::CreateTranslation(SlotPosition(index)));
        slot.renderer->setViewProperty(view);
        slot.renderer->setProjectionProperty(projection);
        slot.renderer->Draw(slot.animation.get());
    }
    spriteBatch_->Begin();
    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        const Vector4 clip = Vector4::Transform(Vector4(SlotPosition(index), 1.0f), view * projection);
        const Vector2 at((clip.X / clip.W * 0.5f + 0.5f) * width, (0.5f - clip.Y / clip.W * 0.5f) * height + 6.0f);
        const char* label = CNAExamplesEXT::kAvatarPresets[slots_[index].preset].name;
        const float scale = 1.2f;
        const float textWidth = font_->MeasureString(label).X * scale;
        spriteBatch_->Draw(*whitePixel_, Rectangle(static_cast<int>(at.X - textWidth / 2 - 4), static_cast<int>(at.Y - 2),
                                                   static_cast<int>(textWidth + 8), 14), Color(0, 0, 0, 140));
        spriteBatch_->DrawString(*font_, label, Vector2(at.X - textWidth / 2, at.Y), Color::White, 0.0f, Vector2::Zero, scale,
                                 SpriteEffects::None, 0.0f);
    }
    if (showHelp_)
    {
        const char* lines[] = {"Space: next page of presets", "R: new random avatars", "F1: help, Esc: quit"};
        spriteBatch_->Draw(*whitePixel_, Rectangle(8, 8, 330, 56), Color(255, 255, 255, 210));
        float y = 16.0f;
        for (const char* line : lines)
        {
            spriteBatch_->DrawString(*font_, line, Vector2(16.0f, y), Color::Black, 0.0f, Vector2::Zero, 1.5f, SpriteEffects::None, 0.0f);
            y += 13.0f;
        }
    }
    spriteBatch_->End();
    if (smokeFramesLeft_ == 1 && !screenshotPath_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPath_);
        screenshotPath_.clear();
    }
    Game::Draw(gameTime);
}

GetTypeNameCPP(GalleryDemo, "GalleryDemo")
