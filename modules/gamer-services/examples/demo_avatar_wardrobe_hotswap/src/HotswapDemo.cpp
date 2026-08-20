#include "HotswapDemo.hpp"

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
    constexpr float kCameraDistance = 3.0f;
    constexpr float kCameraHeight = 1.0f;
    constexpr float kTargetHeight = 0.9f;

    const char* HairStateName(int state)
    {
        switch (state)
        {
            case 0: return "baked-in";
            case 1: return "wardrobe/hair_Cap";
            case 2: return "wardrobe/hair_Ponytail";
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
        "CNA Avatar Wardrobe Hotswap Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "Tab: Cycle hair (baked-in -> Cap -> Ponytail -> baked-in)",
        "",
        "Proves SkinnedModelEXT::AttachPartEXT/RemovePartEXT work live at",
        "runtime - the avatar's hairstyle changes without restarting.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
}

HotswapDemo::HotswapDemo()
    : gender_(AvatarBodyType::Male)
{
    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

HotswapDemo::~HotswapDemo() = default;

void HotswapDemo::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);
}

void HotswapDemo::ConfigureRenderer()
{
    auto& device = getGraphicsDeviceProperty();
    renderer_ = std::make_unique<AvatarRenderer>(nullptr);
    renderer_->EnableRealRenderingEXT(device, model_);

    AvatarAppearanceEXT appearance;
    appearance.setSkinColorProperty(Color(210, 170, 130, 255));
    appearance.setHairColorProperty(Color(40, 25, 15, 255));
    renderer_->SetAppearanceEXT(appearance);

    renderer_->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f));
    renderer_->setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    renderer_->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));
}

void HotswapDemo::ApplyHairState(int state)
{
    auto& content = getContentProperty();

    if (state == 0)
    {
        // No wardrobe/hair_baked folder exists to AttachPartEXT from, and RemovePartEXT already
        // freed the original hair part's GPU buffers the moment it was first replaced - restoring
        // baked-in hair requires a genuine fresh reload of the base avatar asset. Unload() clears
        // ContentManager's own cache map only; this demo's existing model_/renderer_ hold
        // independent references, so nothing dangles.
        content.Unload();
        model_ = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(gender_));
    }
    else
    {
        const char* style = (state == 1) ? "Cap" : "Ponytail";
        auto wardrobePiece = content.Load<std::shared_ptr<SkinnedModelEXT>>(
            std::string("wardrobe/hair_") + style + "/avatar");
        // AttachPartEXT's own replace-by-name semantics (Task 11.4) remove the model's existing
        // "CNAAvatarHair" part (freeing its GPU resources, Task 11.5) before attaching the new
        // one - no manual removal needed here, unlike the workaround AvatarDemo.cpp needed before
        // that fix landed.
        model_->AttachPartEXT(std::move(*wardrobePiece));
    }

    ConfigureRenderer();
    hairState_ = state;

    std::printf("[Hotswap] Hair -> %s\n", HairStateName(state));
    getWindowProperty().setTitleProperty(
        std::string("CNA Avatar Wardrobe Hotswap - hair: ") + HairStateName(state) +
        " (Tab: cycle, F1: help, Esc: quit)");
}

void HotswapDemo::LoadContent()
{
    auto& content = getContentProperty();
    model_ = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(gender_));
    ConfigureRenderer();
    hairState_ = 0;

    // Task 8.1/8.3 (plans/plan_net.md Phase 8): F1 help overlay plumbing.
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);

    getWindowProperty().setTitleProperty(
        "CNA Avatar Wardrobe Hotswap - hair: baked-in (Tab: cycle, F1: help, Esc: quit)");
}

void HotswapDemo::Update(GameTime& gameTime)
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
    cameraYaw_ += 0.5f * dt;

    const bool tabDown = kb.IsKeyDown(Keys::Tab);
    if (tabDown && !previousKeys_.IsKeyDown(Keys::Tab))
    {
        ApplyHairState((hairState_ + 1) % 3);
    }
    previousKeys_ = kb;

    // Smoke-test mode has no real keyboard driving it - deterministically cycle every 45 frames
    // (matching the established Phase 15 deterministic-nudge convention).
    if (smokeFramesLeft_ > 0 && smokeSwapsRemaining_ > 0 && smokeFramesLeft_ % 45 == 0)
    {
        ApplyHairState((hairState_ + 1) % 3);
        --smokeSwapsRemaining_;
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Hotswap] Smoke test complete: finalHairState=%s\n", HairStateName(hairState_));
            Exit();
        }
    }
}

void HotswapDemo::Draw(const GameTime& /*gameTime*/)
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

    renderer_->setWorldProperty(Matrix::getIdentityProperty());
    renderer_->setViewProperty(Matrix::CreateLookAt(eye, target, Vector3::Up));
    renderer_->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(kPiOver4, aspect, 0.1f, 100.0f));

    renderer_->DrawRealEXT("Stand0", System::TimeSpan::Zero, /*loop=*/true);

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
