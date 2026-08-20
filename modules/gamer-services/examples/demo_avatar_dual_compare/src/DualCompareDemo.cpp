#include "DualCompareDemo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kPiOver4 = kPi * 0.25f;
    constexpr float kCameraDistance = 4.5f;
    constexpr float kCameraHeight = 1.0f;
    constexpr float kTargetHeight = 0.9f;

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy was confirmed unreadable
    // (every character rendered as an identical rectangle) by an independent audit.

    // Task 8.2: decision 5a's default text block, adapted per this task's own instruction (keep
    // the F1/Esc lines identical across every demo, customize the rest).
    constexpr const char* kHelpLines[] = {
        "CNA Avatar Dual Compare Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "1/2: Select active avatar (male/female)",
        "Space: Next animation for the active avatar",
        "",
        "Two independent AvatarRenderer/SkinnedModelEXT instances draw",
        "simultaneously with distinct tints, proving per-instance appearance",
        "state is not shared/global.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
}

DualCompareDemo::DualCompareDemo()
{
    slots_[0].gender = AvatarBodyType::Male;
    slots_[0].worldX = -1.0f;
    slots_[0].clipNames = {"Stand0", "Stand1", "Stand2", "Stand3", "Stand4", "Stand5", "Stand6",
                            "Stand7", "Wave", "Clap", "Celebrate", "MaleIdleLookAround",
                            "MaleIdleStretch", "MaleIdleShiftWeight", "MaleIdleCheckHand",
                            "MaleAngry", "MaleConfused", "MaleLaugh", "MaleCry", "MaleSurprised",
                            "MaleYawn"};

    slots_[1].gender = AvatarBodyType::Female;
    slots_[1].worldX = 1.0f;
    slots_[1].clipNames = {"Stand0", "Stand1", "Stand2", "Stand3", "Stand4", "Stand5", "Stand6",
                            "Stand7", "Wave", "Clap", "Celebrate", "FemaleIdleCheckNails",
                            "FemaleIdleLookAround", "FemaleIdleShiftWeight", "FemaleIdleFixShoe",
                            "FemaleAngry", "FemaleConfused", "FemaleLaugh", "FemaleCry",
                            "FemaleShocked", "FemaleYawn"};

    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

DualCompareDemo::~DualCompareDemo() = default;

void DualCompareDemo::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);
}

void DualCompareDemo::LoadSlot(AvatarSlot& slot)
{
    auto& content = getContentProperty();
    slot.model = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(slot.gender));

    auto& device = getGraphicsDeviceProperty();
    slot.renderer = std::make_unique<AvatarRenderer>(nullptr);
    slot.renderer->EnableRealRenderingEXT(device, slot.model);

    // Deliberately distinct tints per slot - proves SetAppearanceEXT is genuine per-AvatarRenderer-
    // instance state, not shared/global, since both renderers exist and draw simultaneously.
    AvatarAppearanceEXT appearance;
    if (slot.gender == AvatarBodyType::Male)
    {
        appearance.setSkinColorProperty(Color(210, 170, 130, 255));
        appearance.setHairColorProperty(Color(40, 25, 15, 255));
        appearance.setShirtColorProperty(Color(60, 90, 160, 255));
    }
    else
    {
        appearance.setSkinColorProperty(Color(235, 200, 170, 255));
        appearance.setHairColorProperty(Color(200, 60, 30, 255));
        appearance.setShirtColorProperty(Color(200, 60, 140, 255));
    }
    slot.renderer->SetAppearanceEXT(appearance);

    slot.renderer->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f));
    slot.renderer->setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    slot.renderer->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));
}

void DualCompareDemo::LoadContent()
{
    LoadSlot(slots_[0]);
    LoadSlot(slots_[1]);

    // Task 8.1/8.3 (plans/plan_net.md Phase 8): F1 help overlay plumbing.
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);

    getWindowProperty().setTitleProperty(
        "CNA Avatar Dual Compare (1/2 select avatar, Space cycle its clip, F1: help, Esc quit)");
}

void DualCompareDemo::AdvanceSlotClip(int slotIndex)
{
    AvatarSlot& slot = slots_[slotIndex];
    slot.currentClipIndex = (slot.currentClipIndex + 1) % slot.clipNames.size();
    slot.clipPositionSeconds = 0.0;
    std::printf("[DualCompare] %s avatar -> %s\n",
                slot.gender == AvatarBodyType::Male ? "Male" : "Female",
                slot.clipNames[slot.currentClipIndex].c_str());
}

void DualCompareDemo::Update(GameTime& gameTime)
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
    cameraYaw_ += 0.3f * dt;

    if (kb.IsKeyDown(Keys::D1) && !previousKeys_.IsKeyDown(Keys::D1)) { activeSlot_ = 0; }
    if (kb.IsKeyDown(Keys::D2) && !previousKeys_.IsKeyDown(Keys::D2)) { activeSlot_ = 1; }
    if (kb.IsKeyDown(Keys::Space) && !previousKeys_.IsKeyDown(Keys::Space))
    {
        AdvanceSlotClip(activeSlot_);
    }
    previousKeys_ = kb;

    // Smoke-test mode has no real keyboard driving it - deterministically alternate which
    // avatar is active and advance its clip every 25 frames (matching the established Phase 15
    // deterministic-nudge convention).
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 25 == 0)
    {
        activeSlot_ = 1 - activeSlot_;
        AdvanceSlotClip(activeSlot_);
    }

    for (AvatarSlot& slot : slots_)
    {
        slot.clipPositionSeconds += static_cast<double>(dt);
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[DualCompare] Smoke test complete: maleClip=%s femaleClip=%s\n",
                        slots_[0].clipNames[slots_[0].currentClipIndex].c_str(),
                        slots_[1].clipNames[slots_[1].currentClipIndex].c_str());
            Exit();
        }
    }
}

void DualCompareDemo::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color::CornflowerBlue);
    device.SetDepthTestEnabled(true);

    const auto& vp = device.getViewportProperty();
    const float aspect = (vp.getHeightProperty() > 0)
                              ? static_cast<float>(vp.getWidthProperty()) / static_cast<float>(vp.getHeightProperty())
                              : 1.0f;

    const Vector3 target(0.0f, kTargetHeight, 0.0f);
    const Vector3 eye(kCameraDistance * std::sin(cameraYaw_), kCameraHeight, kCameraDistance * std::cos(cameraYaw_));
    const Matrix view = Matrix::CreateLookAt(eye, target, Vector3::Up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 100.0f);

    for (AvatarSlot& slot : slots_)
    {
        slot.renderer->setWorldProperty(Matrix::CreateTranslation(Vector3(slot.worldX, 0.0f, 0.0f)));
        slot.renderer->setViewProperty(view);
        slot.renderer->setProjectionProperty(projection);
        slot.renderer->DrawRealEXT(slot.clipNames[slot.currentClipIndex],
                                    System::TimeSpan::FromSeconds(slot.clipPositionSeconds), /*loop=*/true);
    }

    // Task 8.2: 3D scene drawn first (above), then the 2D help overlay on top.
    if (showHelpEXT_)
    {
        constexpr int kLineCount = static_cast<int>(sizeof(kHelpLines) / sizeof(kHelpLines[0]));
        // The real 5x7 bitmap font (CNAExamplesEXT::MakeSimpleFontEXT) is drawn at 1.5x scale -
        // legible, and small enough that the longest help line still fits an 800px-wide window.
        constexpr float kTextScale = 1.5f;
        constexpr float kLineHeight = 13.0f;
        constexpr float kPadding = 12.0f;
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
