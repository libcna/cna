#include "TintStudioDemo.hpp"

#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kPiOver4 = kPi * 0.25f;
    constexpr float kCameraDistance = 3.0f;
    constexpr float kCameraHeight = 1.0f;
    constexpr float kTargetHeight = 0.9f;

    const Color kPalette[6] = {
        Color(220, 60, 60, 255), Color(60, 200, 90, 255), Color(60, 130, 220, 255),
        Color(230, 210, 60, 255), Color(240, 240, 240, 255), Color(30, 30, 30, 255),
    };
    constexpr int kPaletteSize = 6;

    const char* SlotName(int slot)
    {
        switch (slot)
        {
            case 0: return "Skin";
            case 1: return "Hair";
            case 2: return "Shirt";
            case 3: return "Pants";
            case 4: return "Shoes";
        }
        return "?";
    }
}

TintStudioDemo::TintStudioDemo()
    : gender_(AvatarBodyType::Male)
{
    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

TintStudioDemo::~TintStudioDemo() = default;

void TintStudioDemo::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);
}

void TintStudioDemo::ApplyAppearance()
{
    appearance_.setSkinColorProperty(kPalette[paletteIndex_[0]]);
    appearance_.setHairColorProperty(kPalette[paletteIndex_[1]]);
    appearance_.setShirtColorProperty(kPalette[paletteIndex_[2]]);
    appearance_.setPantsColorProperty(kPalette[paletteIndex_[3]]);
    appearance_.setShoesColorProperty(kPalette[paletteIndex_[4]]);
    renderer_->SetAppearanceEXT(appearance_);
}

void TintStudioDemo::LoadContent()
{
    auto& content = getContentProperty();
    model_ = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(gender_));

    auto& device = getGraphicsDeviceProperty();
    renderer_ = std::make_unique<AvatarRenderer>(nullptr);
    renderer_->EnableRealRenderingEXT(device, model_);
    ApplyAppearance();

    renderer_->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f));
    renderer_->setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    renderer_->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));

    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    spriteBatch_ = std::make_unique<SpriteBatch>(device);

    getWindowProperty().setTitleProperty("CNA Avatar Tint Studio (1-5 select slot, Up/Down cycle color, Esc quit)");
}

void TintStudioDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape)) { Exit(); return; }

    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    cameraYaw_ += 0.4f * dt;

    const Keys numberKeys[5] = {Keys::D1, Keys::D2, Keys::D3, Keys::D4, Keys::D5};
    for (int i = 0; i < 5; ++i)
    {
        if (kb.IsKeyDown(numberKeys[i]) && !previousKeys_.IsKeyDown(numberKeys[i]))
        {
            selectedSlot_ = i;
        }
    }
    bool changed = false;
    if (kb.IsKeyDown(Keys::Up) && !previousKeys_.IsKeyDown(Keys::Up))
    {
        paletteIndex_[selectedSlot_] = (paletteIndex_[selectedSlot_] + 1) % kPaletteSize;
        changed = true;
    }
    if (kb.IsKeyDown(Keys::Down) && !previousKeys_.IsKeyDown(Keys::Down))
    {
        paletteIndex_[selectedSlot_] = (paletteIndex_[selectedSlot_] - 1 + kPaletteSize) % kPaletteSize;
        changed = true;
    }
    previousKeys_ = kb;

    // Smoke-test mode has no real keyboard driving it - deterministically select each slot in
    // turn and cycle its color every 20 frames (matching the established Phase 15
    // deterministic-nudge convention).
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 20 == 0)
    {
        selectedSlot_ = smokeStep_ % 5;
        paletteIndex_[selectedSlot_] = (paletteIndex_[selectedSlot_] + 1) % kPaletteSize;
        ++smokeStep_;
        changed = true;
    }

    if (changed)
    {
        ApplyAppearance();
        std::printf("[TintStudio] %s -> palette index %d\n", SlotName(selectedSlot_), paletteIndex_[selectedSlot_]);
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[TintStudio] Smoke test complete: paletteIndices=[%d,%d,%d,%d,%d]\n",
                        paletteIndex_[0], paletteIndex_[1], paletteIndex_[2], paletteIndex_[3], paletteIndex_[4]);
            Exit();
        }
    }
}

void TintStudioDemo::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(30, 30, 40, 255));
    device.SetDepthTestEnabled(true);

    const auto& vp = device.getViewportProperty();
    const float aspect = (vp.getHeightProperty() > 0)
                              ? static_cast<float>(vp.getWidthProperty()) / static_cast<float>(vp.getHeightProperty())
                              : 1.0f;

    const Vector3 target(0.0f, kTargetHeight, 0.0f);
    const Vector3 eye(kCameraDistance * std::sin(cameraYaw_), kCameraHeight, kCameraDistance * std::cos(cameraYaw_));

    renderer_->setWorldProperty(Matrix::getIdentityProperty());
    renderer_->setViewProperty(Matrix::CreateLookAt(eye, target, Vector3::Up));
    renderer_->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 100.0f));
    renderer_->DrawRealEXT("Stand0", System::TimeSpan::Zero, /*loop=*/true);

    device.SetDepthTestEnabled(false);
    spriteBatch_->Begin();
    const Color slotColors[5] = {
        appearance_.getSkinColorProperty(), appearance_.getHairColorProperty(),
        appearance_.getShirtColorProperty(), appearance_.getPantsColorProperty(),
        appearance_.getShoesColorProperty(),
    };
    for (int i = 0; i < 5; ++i)
    {
        const Rectangle rect(16 + i * 60, 16, 48, 48);
        spriteBatch_->Draw(*whitePixel_, rect, slotColors[i]);
        if (i == selectedSlot_)
        {
            // Selection border: a thin outline drawn as 4 rectangles.
            const Color border(255, 255, 255, 255);
            spriteBatch_->Draw(*whitePixel_, Rectangle(rect.X - 2, rect.Y - 2, rect.Width + 4, 2), border);
            spriteBatch_->Draw(*whitePixel_, Rectangle(rect.X - 2, rect.Y + rect.Height, rect.Width + 4, 2), border);
            spriteBatch_->Draw(*whitePixel_, Rectangle(rect.X - 2, rect.Y - 2, 2, rect.Height + 4), border);
            spriteBatch_->Draw(*whitePixel_, Rectangle(rect.X + rect.Width, rect.Y - 2, 2, rect.Height + 4), border);
        }
    }
    spriteBatch_->End();
}
