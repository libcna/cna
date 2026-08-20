#include "StressDemo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
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

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy (and this demo's own
    // Parts.size() counter text using it) was confirmed unreadable (every character rendered as
    // an identical rectangle) by an independent audit.

    // Task 8.2 (plans/plan_net.md Phase 8): decision 5a's default text block, adapted per this task's
    // own instruction (keep the F1/Esc lines identical across every demo, customize the rest).
    constexpr const char* kHelpLines[] = {
        "CNA Avatar Multi-Attach Stress Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "Space: Attach next part (hair variants first, then synthetic accessories)",
        "",
        "The Parts.size() counter (bottom-left of the 3D view) proves",
        "accumulation doesn't break skinning/tinting as part count grows.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
}

StressDemo::StressDemo()
    : gender_(AvatarBodyType::Male)
{
    static constexpr int FPS = 60;
    Game::setTargetElapsedTimeProperty(System::TimeSpan::FromTicks(static_cast<long>(500000L * 20 / FPS)));
}

StressDemo::~StressDemo() = default;

void StressDemo::Initialize()
{
    Game::Initialize();

    auto& device = getGraphicsDeviceProperty();
    device.SetDepthTestEnabled(true);
}

void StressDemo::LoadContent()
{
    auto& content = getContentProperty();
    model_ = content.Load<std::shared_ptr<SkinnedModelEXT>>(AvatarBodyTypeToContentNameEXT(gender_));

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

    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);

    getWindowProperty().setTitleProperty(
        "CNA Avatar Multi-Attach Stress (Space: attach next, F1: help, Esc: quit)");
}

void StressDemo::AttachSyntheticAccessory()
{
    auto& device = getGraphicsDeviceProperty();

    // A small quad, rigidly skinned entirely to bone 0 (weight 1.0, no blending) - built to
    // exactly match the real avatar model's own BoneCount/ParentBoneIndices/BindPoseLocal/
    // InverseBindPoseGlobal (copied straight off the already-loaded host model), which is what
    // AttachPartEXT requires ("share this model's exact bone count and index order"). The
    // accessory's own Clips are irrelevant post-attach - DrawRealEXT always uses the host
    // model's own clip to compute bone transforms for every part, including newly-attached ones.
    auto accessory = std::make_shared<SkinnedModelEXT>();
    accessory->BoneCount = model_->BoneCount;
    accessory->ParentBoneIndices = model_->ParentBoneIndices;
    accessory->BindPoseLocal = model_->BindPoseLocal;
    accessory->InverseBindPoseGlobal = model_->InverseBindPoseGlobal;

    // Small palette cycling by accessory index, so successive accessories are visually
    // distinguishable even though PartTintEXT falls back to the shared skin tint for any name
    // that doesn't match Hair/Shirt/Pants/Shoes (confirmed by reading AvatarRenderer::PartTintEXT
    // before writing this - not a bug, an intentional existing fallback).
    const Color palette[4] = {
        Color(255, 80, 80, 255), Color(80, 255, 80, 255), Color(80, 140, 255, 255), Color(255, 220, 60, 255),
    };
    Texture2D tex = Texture2D::CreateFromPixels(
        device, 1, 1,
        std::vector<uint8_t>{palette[attachCount_ % 4].getRProperty(), palette[attachCount_ % 4].getGProperty(),
                              palette[attachCount_ % 4].getBProperty(), 255});

    const float size = 0.12f;
    const float yOffset = 1.4f + 0.05f * static_cast<float>(attachCount_);
    const VertexPositionNormalTextureSkinned verts[6] = {
        {Vector3(-size, yOffset + size, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        {Vector3(-size, yOffset - size, 0), Vector3(0, 0, 1), Vector2(0, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        {Vector3(size, yOffset - size, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        {Vector3(-size, yOffset + size, 0), Vector3(0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        {Vector3(size, yOffset - size, 0), Vector3(0, 0, 1), Vector2(1, 1), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
        {Vector3(size, yOffset + size, 0), Vector3(0, 0, 1), Vector2(1, 0), Vector4(1, 0, 0, 0), {0, 0, 0, 0}},
    };
    const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};

    auto vb = std::make_unique<VertexBuffer>(device, 6);
    vb->SetData(verts, 6);
    auto ib = std::make_unique<IndexBuffer>(device, 6);
    ib->SetData(indices, 6);
    auto part = std::make_unique<ModelMeshPart>(vb.get(), ib.get(), 6, 2, 0, 0);

    const std::string partName = "CNAAccessory" + std::to_string(attachCount_);
    accessory->AddPartEXT(partName, std::move(vb), std::move(ib), std::move(part), std::move(tex));

    model_->AttachPartEXT(std::move(*accessory));
}

void StressDemo::AttachNext()
{
    auto& content = getContentProperty();
    ++attachCount_;

    if (attachCount_ == 1)
    {
        auto piece = content.Load<std::shared_ptr<SkinnedModelEXT>>("wardrobe/hair_Cap/avatar");
        model_->AttachPartEXT(std::move(*piece));
        std::printf("[Stress] Attached wardrobe/hair_Cap - Parts.size()=%zu\n", model_->Parts.size());
    }
    else if (attachCount_ == 2)
    {
        auto piece = content.Load<std::shared_ptr<SkinnedModelEXT>>("wardrobe/hair_Ponytail/avatar");
        model_->AttachPartEXT(std::move(*piece));
        std::printf("[Stress] Attached wardrobe/hair_Ponytail - Parts.size()=%zu\n", model_->Parts.size());
    }
    else
    {
        AttachSyntheticAccessory();
        std::printf("[Stress] Attached synthetic accessory #%d - Parts.size()=%zu\n",
                    attachCount_ - 2, model_->Parts.size());
    }
}

void StressDemo::Update(GameTime& gameTime)
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

    if (kb.IsKeyDown(Keys::Space) && !previousKeys_.IsKeyDown(Keys::Space))
    {
        AttachNext();
    }
    previousKeys_ = kb;

    // Smoke-test mode has no real keyboard driving it - deterministically attach one more part
    // every 20 frames (matching the established Phase 15 deterministic-nudge convention).
    if (smokeFramesLeft_ > 0 && smokeFramesLeft_ % 20 == 0)
    {
        AttachNext();
    }

    if (smokeFramesLeft_ > 0)
    {
        if (--smokeFramesLeft_ == 0)
        {
            std::printf("[Stress] Smoke test complete: attachCount=%d finalPartsSize=%zu\n",
                        attachCount_, model_->Parts.size());
            Exit();
        }
    }
}

void StressDemo::Draw(const GameTime& /*gameTime*/)
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

    device.SetDepthTestEnabled(false);
    spriteBatch_->Begin();
    char line[96];
    std::snprintf(line, sizeof(line), "Parts.size() = %zu  (Space: attach next)", model_->Parts.size());
    spriteBatch_->DrawString(*font_, line, Vector2(16.0f, 16.0f), Color(255, 255, 255, 255),
                              0.0f, Vector2::Zero, 1.5f, SpriteEffects::None, 0.0f);
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
        float longestLineWidth = 0.0f;
        for (const char* line2 : kHelpLines)
        {
            longestLineWidth = std::max(longestLineWidth, font_->MeasureString(line2).X * kTextScale);
        }
        const Rectangle panel(8, 8, static_cast<int>(longestLineWidth + kPadding * 2.0f),
                               static_cast<int>(kLineCount * kLineHeight + kPadding * 2.0f));

        spriteBatch_->Begin();
        spriteBatch_->Draw(*whitePixel_, panel, Color(255, 255, 255, 210));
        float y = panel.Y + kPadding;
        for (const char* line2 : kHelpLines)
        {
            spriteBatch_->DrawString(*font_, line2, Vector2(panel.X + kPadding, y), Color(0, 0, 0, 255),
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
