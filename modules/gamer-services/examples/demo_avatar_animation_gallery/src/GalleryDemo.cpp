#include "GalleryDemo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPresetToClipNameEXT;
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
    constexpr float kTargetHeight = 0.9f;
    constexpr double kSecondsPerClip = 2.0;

    // All 31 AvatarAnimationPreset values, in the enum's own declaration order - matches the
    // stringizing-macro pattern already established for AvatarAnimationPresetNamesEXTTests
    // (Task 13.5), reused here so this list can never silently drift out of sync with the enum.
    std::vector<AvatarAnimationPreset> AllPresetsInOrder()
    {
#define PRESET(x) AvatarAnimationPreset::x,
        return {
            PRESET(Stand0) PRESET(Stand1) PRESET(Stand2) PRESET(Stand3) PRESET(Stand4)
            PRESET(Stand5) PRESET(Stand6) PRESET(Stand7) PRESET(Clap) PRESET(Wave) PRESET(Celebrate)
            PRESET(FemaleIdleCheckNails) PRESET(FemaleIdleLookAround) PRESET(FemaleIdleShiftWeight)
            PRESET(FemaleIdleFixShoe) PRESET(FemaleAngry) PRESET(FemaleConfused) PRESET(FemaleLaugh)
            PRESET(FemaleCry) PRESET(FemaleShocked) PRESET(FemaleYawn)
            PRESET(MaleIdleLookAround) PRESET(MaleIdleStretch) PRESET(MaleIdleShiftWeight)
            PRESET(MaleIdleCheckHand) PRESET(MaleAngry) PRESET(MaleConfused) PRESET(MaleLaugh)
            PRESET(MaleCry) PRESET(MaleSurprised) PRESET(MaleYawn)
        };
#undef PRESET
    }

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy was confirmed unreadable
    // (every character rendered as an identical rectangle) by an independent audit.

    // Task 8.2: decision 5a's default text block, adapted per this task's own instruction (keep
    // the F1/Esc lines identical across every demo, customize the rest) - this demo has no
    // command-line args and no interactive camera/animation controls at all (auto-cycles/
    // auto-rotates), so the "Command line"/"Space"/"Left/Right" lines don't apply here.
    constexpr const char* kHelpLines[] = {
        "CNA Avatar Animation Gallery Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "",
        "Auto-cycles through all 31 AvatarAnimationPreset clips, ~2s each,",
        "switching between male and female content after each full pass.",
        "Camera rotates automatically.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
}

GalleryDemo::GalleryDemo()
    : currentGender_(AvatarBodyType::Male)
    , allPresets_(AllPresetsInOrder())
{
    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

GalleryDemo::~GalleryDemo() = default;

void GalleryDemo::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);
}

void GalleryDemo::LoadContentForCurrentGender()
{
    auto& content = getContentProperty();
    model_ = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(currentGender_));

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

void GalleryDemo::LoadContent()
{
    LoadContentForCurrentGender();
    AdvanceToNextCompatiblePreset();

    // Task 8.1/8.3 (plans/plan_net.md Phase 8): F1 help overlay plumbing - see this class's own
    // AvatarDemo-mirroring comment in GalleryDemo.hpp.
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
}

bool GalleryDemo::IsCompatibleWithCurrentGender(const std::string& clipName) const
{
    const bool isFemaleOnly = clipName.rfind("Female", 0) == 0;
    const bool isMaleOnly = clipName.rfind("Male", 0) == 0;
    if (isFemaleOnly) { return currentGender_ == AvatarBodyType::Female; }
    if (isMaleOnly) { return currentGender_ == AvatarBodyType::Male; }
    return true; // gender-neutral (Stand*/Clap/Wave/Celebrate) - baked into both genders' content.
}

void GalleryDemo::AdvanceToNextCompatiblePreset()
{
    for (;;)
    {
        const std::string clipName = AvatarAnimationPresetToClipNameEXT(allPresets_[presetIndex_]);
        if (IsCompatibleWithCurrentGender(clipName))
        {
            currentClipName_ = clipName;
            clipPositionSeconds_ = 0.0;
            ++playedCount_;
            std::printf("[Gallery] Playing %s (%s) [%zu/31 this pass]\n", clipName.c_str(),
                        currentGender_ == AvatarBodyType::Female ? "female" : "male", presetIndex_ + 1);
            getWindowProperty().setTitleProperty(
                std::string("CNA Avatar Animation Gallery [") +
                (currentGender_ == AvatarBodyType::Female ? "female" : "male") + "] - " + clipName +
                "  (F1: help, Esc: quit)");
            return;
        }
        ++skippedCount_;
        ++presetIndex_;
        if (presetIndex_ >= allPresets_.size())
        {
            presetIndex_ = 0;
            currentGender_ = (currentGender_ == AvatarBodyType::Female) ? AvatarBodyType::Male
                                                                          : AvatarBodyType::Female;
            ++genderSwitchCount_;
            std::printf("[Gallery] Full pass complete - switching to %s and reloading content.\n",
                        currentGender_ == AvatarBodyType::Female ? "female" : "male");
            LoadContentForCurrentGender();
        }
    }
}

void GalleryDemo::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Microsoft::Xna::Framework::Input::Keys::Escape)) { Exit(); return; }

    // Task 8.2: F1 toggles overlay visibility, edge-triggered.
    const bool f1Down = kb.IsKeyDown(Microsoft::Xna::Framework::Input::Keys::F1);
    if (f1Down && !f1WasDownEXT_)
    {
        showHelpEXT_ = !showHelpEXT_;
    }
    f1WasDownEXT_ = f1Down;

    const float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    const float rotSpeed = 0.6f * dt;
    cameraYaw_ += rotSpeed;

    clipPositionSeconds_ += static_cast<double>(dt);
    if (clipPositionSeconds_ >= kSecondsPerClip)
    {
        ++presetIndex_;
        if (presetIndex_ >= allPresets_.size())
        {
            presetIndex_ = 0;
            currentGender_ = (currentGender_ == AvatarBodyType::Female) ? AvatarBodyType::Male
                                                                          : AvatarBodyType::Female;
            ++genderSwitchCount_;
            std::printf("[Gallery] Full pass complete - switching to %s and reloading content.\n",
                        currentGender_ == AvatarBodyType::Female ? "female" : "male");
            LoadContentForCurrentGender();
        }
        AdvanceToNextCompatiblePreset();
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Gallery] Smoke test complete: played=%d skipped=%d genderSwitches=%d "
                        "(expected played+skipped >= 31)\n",
                        playedCount_, skippedCount_, genderSwitchCount_);
            Exit();
        }
    }
}

void GalleryDemo::Draw(const GameTime& /*gameTime*/)
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

    renderer_->DrawRealEXT(currentClipName_, System::TimeSpan::FromSeconds(clipPositionSeconds_), /*loop=*/true);

    // Task 8.2: 3D scene drawn first (above), then the 2D help overlay on top.
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
