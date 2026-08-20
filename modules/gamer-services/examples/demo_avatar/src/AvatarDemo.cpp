#include "AvatarDemo.hpp"

#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"

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

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy was confirmed unreadable
    // (every character rendered as an identical rectangle) by an independent audit.

    // Task 8.2 (plans/plan_net.md Phase 8): decision 5a's own default text block, verbatim - kept as
    // one line per array entry rather than embedded '\n's, since MakeSimpleFont's synthetic glyph
    // table above only maps printable ASCII 32-126 (no newline glyph to fall back on).
    constexpr const char* kHelpLines[] = {
        "CNA Avatar Demo Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "Space: Next animation",
        "Left/Right: Rotate camera",
        "",
        "Command line:",
        "--gender male|female",
        "--wardrobe-hair Cap|Ponytail",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
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
    // avatar's baked-in hair. AttachPartEXT (Task 11.4) now has its own replace-by-name
    // semantics, so it removes the old "CNAAvatarHair" part (freeing its GPU resources,
    // Task 11.5) before attaching the new one -- no manual workaround needed here anymore.
    if (!wardrobeHairStyle_.empty())
    {
        auto wardrobePiece = content.Load<std::shared_ptr<SkinnedModelEXT>>(
            "wardrobe/hair_" + wardrobeHairStyle_ + "/avatar");
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
    // audit_net.md remediation (2026-07-18, fourth round): back at its original 0.35. The third
    // round had raised this to 0.5 to fight the avatar's near-black regions, but that was
    // compensating for a real shader bug, not a lighting-setup problem: EasyGL's skinned shaders
    // multiplied EmissiveColor (which carries the pre-folded ambient term) by DiffuseColor a
    // second time, so ambient landed as ambient*diffuse^2 and dark materials were crushed. With
    // that fixed (see EnsureSkinnedProgram's own comment), 0.35 measures strictly better than 0.5
    // on every region this project's own scripts/avatar_visual_regression_check.py tracks - so the
    // compensation hack is removed rather than left to double up with the real fix.
    renderer_->setAmbientLightColorProperty(Vector3(0.35f, 0.35f, 0.35f));
    renderer_->setLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    renderer_->setLightDirectionProperty(Vector3(-0.4f, -0.6f, -0.7f));

    // Task 8.1/8.3 (plans/plan_net.md Phase 8): F1 help overlay's own SpriteBatch/white-pixel/font -
    // must not crash if this fails, so LoadContent()'s own real device/content are reused here
    // (already known-good, since model_ loaded successfully above) rather than anything that
    // could plausibly fail independently.
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);

    getWindowProperty().setTitleProperty(
        (bodyType_ == AvatarBodyType::Female ? "CNA Avatar Demo [female] - " : "CNA Avatar Demo [male] - ")
        + clipNames_[currentClipIndex_] + "  (F1: help, Space: next anim, Left/Right: rotate, Esc: quit)");
}

void AvatarDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape)) { Exit(); return; }

    const float dt = static_cast<float>(
        gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

    if (!fixedCameraYawEXT_)
    {
        const float rotSpeed = 1.6f * dt;
        if (kb.IsKeyDown(Keys::Left))  cameraYaw_ -= rotSpeed;
        if (kb.IsKeyDown(Keys::Right)) cameraYaw_ += rotSpeed;
    }

    const bool spaceDown = kb.IsKeyDown(Keys::Space);
    if (spaceDown && !spaceWasDown_) {
        currentClipIndex_ = (currentClipIndex_ + 1) % clipNames_.size();
        clipPositionSeconds_ = 0.0;
        getWindowProperty().setTitleProperty(
            (bodyType_ == AvatarBodyType::Female ? "CNA Avatar Demo [female] - " : "CNA Avatar Demo [male] - ")
            + clipNames_[currentClipIndex_] + "  (Space: next anim, Left/Right: rotate, Esc: quit)");
    }
    spaceWasDown_ = spaceDown;

    // Task 8.2 (plans/plan_net.md Phase 8): F1 toggles overlay visibility - edge-triggered, matching
    // the Space-key clip-cycling pattern just above.
    const bool f1Down = kb.IsKeyDown(Keys::F1);
    if (f1Down && !f1WasDownEXT_)
    {
        showHelpEXT_ = !showHelpEXT_;
    }
    f1WasDownEXT_ = f1Down;

    clipPositionSeconds_ += static_cast<double>(dt);

    // Task 7.1 (plans/plan_net.md Phase 7): matches every other smoke-testable demo's own convention -
    // guarded by > 0, not >= 0, so Exit() (which doesn't halt Update()/Draw() immediately) can't
    // keep re-triggering this every subsequent frame.
    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            Exit();
        }
    }
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

    // Task 8.2 (plans/plan_net.md Phase 8): 3D scene drawn first (above), then the 2D help overlay on
    // top - decision 5d: translucent white rectangle behind black text.
    if (showHelpEXT_)
    {
        constexpr int kLineCount = static_cast<int>(sizeof(kHelpLines) / sizeof(kHelpLines[0]));
        // The real 5x7 bitmap font (CNAExamplesEXT::MakeSimpleFontEXT) is drawn at 2x scale -
        // legible at native size, but small enough at 1x to be hard to read comfortably at
        // typical window resolutions.
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

    // Task 7.1 (plans/plan_net.md Phase 7): captured after DrawRealEXT so the just-rendered frame is
    // what ends up in the backbuffer read below. Fires on smokeFramesLeft_ == 1, not 0 - Exit()
    // (called by Update() once the countdown reaches 0) sets Game's own suppressDraw_ flag, which
    // skips this Draw() call entirely on that final frame, so capturing at 0 would never run at
    // all. At 1, Exit() hasn't been called yet this frame, so Draw() still runs normally.
    if (smokeFramesLeft_ == 1 && !screenshotPathEXT_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPathEXT_);
        screenshotPathEXT_.clear(); // guards against a duplicate save if Draw() runs once more
    }
}

GetTypeNameCPP(AvatarDemo, "AvatarDemo")
