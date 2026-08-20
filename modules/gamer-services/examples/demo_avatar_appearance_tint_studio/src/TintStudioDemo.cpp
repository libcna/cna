#include "TintStudioDemo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"

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

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy was confirmed unreadable
    // (every character rendered as an identical rectangle) by an independent audit.

    // Task 8.2: decision 5a's default text block, adapted per this task's own instruction (keep
    // the F1/Esc lines identical across every demo, customize the rest).
    constexpr const char* kHelpLines[] = {
        "CNA Avatar Tint Studio Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "1-5: Select tint slot (Skin/Hair/Shirt/Pants/Shoes)",
        "Up/Down: Cycle preset color for selected slot",
        "",
        "The 5 swatches top-left show the current tint per slot;",
        "the selected slot has a white outline.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
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
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);

    getWindowProperty().setTitleProperty(
        "CNA Avatar Tint Studio (1-5 select slot, Up/Down cycle color, F1: help, Esc quit)");
}

void TintStudioDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape)) { Exit(); return; }

    // Task 8.2: F1 toggles overlay visibility, edge-triggered.
    const bool f1Down = kb.IsKeyDown(Keys::F1);
    if (f1Down && !f1WasDownEXT_)
    {
        showHelpEXT_ = !showHelpEXT_;
    }
    f1WasDownEXT_ = f1Down;

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

    // Task 8.2: 2D help overlay drawn last, on top of everything else.
    if (showHelpEXT_)
    {
        constexpr int kLineCount = static_cast<int>(sizeof(kHelpLines) / sizeof(kHelpLines[0]));
        // The real 5x7 bitmap font (CNAExamplesEXT::MakeSimpleFontEXT) is drawn at 1.5x scale -
        // legible, and small enough that the longest help line still fits an 800px-wide window.
        constexpr float kTextScale = 1.5f;
        constexpr float kLineHeight = 13.0f;
        constexpr float kPadding = 12.0f;
        // Measure via the actual SpriteFont (at 1x, then scaled) rather than a hand-rolled
        // char-count * advance guess - SpriteFont::spacing_/kerning already account for the
        // real per-glyph advance, so a naive strlen()*N estimate would silently undercount and
        // the longest line would overflow the panel's right edge.
        float longestLineWidth = 0.0f;
        for (const char* line : kHelpLines)
        {
            longestLineWidth = std::max(longestLineWidth, font_->MeasureString(line).X * kTextScale);
        }
        const Rectangle panel(8, 8, static_cast<int>(longestLineWidth + kPadding * 2.0f),
                               static_cast<int>(kLineCount * kLineHeight + kPadding * 2.0f));

        spriteBatch_->Begin();
        spriteBatch_->Draw(*whitePixel_, panel, Color(255, 255, 255, 210));
        float y = panel.Y + kPadding;
        for (const char* line : kHelpLines)
        {
            spriteBatch_->DrawString(*font_, line, Vector2(panel.X + kPadding, y), Color(0, 0, 0, 255),
                                      0.0f, Vector2::Zero, kTextScale, SpriteEffects::None, 0.0f);
            y += kLineHeight;
        }
        spriteBatch_->End();
    }

    // Task 8.5 (plans/plan_net.md Phase 8): same smokeFramesLeft_==1 timing as demo_avatar's own
    // AvatarDemo - Game::Exit() suppresses Draw() on the frame Update() actually calls it.
    if (smokeFramesLeft_ == 1 && !screenshotPathEXT_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPathEXT_);
        screenshotPathEXT_.clear();
    }
}
