#include "BoundaryDemo.hpp"

#include <algorithm>
#include <cstdio>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "System/InvalidOperationException.hpp"
#include "common/ScreenshotEXT.hpp"
#include "common/SimpleFontEXT.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::GamerServices::AvatarRendererState;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    const char* StateName(AvatarRendererState state)
    {
        switch (state)
        {
            case AvatarRendererState::Loading: return "Loading";
            case AvatarRendererState::Ready: return "Ready";
            case AvatarRendererState::Unavailable: return "Unavailable";
        }
        return "?";
    }

    // Post-plans/plan_net.md remediation (2026-07-18): now uses the shared, real-bitmap-font
    // CNAExamplesEXT::MakeSimpleFontEXT() (examples/common/SimpleFontEXT.hpp) instead of a
    // per-demo uniform-rectangle "block font" - the old per-file copy was confirmed unreadable
    // (every character rendered as an identical rectangle) by an independent audit.

    // Task 8.2: decision 5a's default text block, adapted per this task's own instruction (keep
    // the F1/Esc lines identical across every demo, customize the rest) - this demo's real
    // content is entirely console output, not on-screen rendering, so the overlay's job is just
    // to point that out for anyone running it without a terminal attached.
    constexpr const char* kHelpLines[] = {
        "CNA Avatar Bone State Boundary Help",
        "",
        "F1: Show/hide this help",
        "Esc: Quit",
        "",
        "This demo's real output is printed to the console/terminal, not this",
        "window - it documents the real AvatarRenderer skeleton API boundary",
        "(State/ParentBones/BindPose) versus the working SkinnedModelEXT path.",
        "The window exits itself automatically after a short delay.",
        "",
        "This demo uses CNA real avatar rendering extensions.",
        "XNA-compatible AvatarRenderer.Draw remains a no-op on Windows-like platforms.",
    };
}

void BoundaryDemo::Initialize()
{
    Game::Initialize();

    std::printf("=== cna_demo_avatar_bone_state_boundary ===\n");
    std::printf("Exercising the real, faithful-XNA-surface AvatarRenderer skeleton API "
                "(no real content loaded, no EnableRealRenderingEXT call - this is the plain, "
                "un-rendered XNA-shaped path a real game calling only the public XNA-shaped API "
                "would experience):\n\n");

    AvatarRenderer renderer(nullptr);

    const AvatarRendererState state = renderer.getStateProperty();
    std::printf("1. getStateProperty() = %s (confirmed: forces itself to Unavailable on every "
                "single read, matching the real XNA implementation - not a one-time initial "
                "value that might later change).\n",
                StateName(state));

    // Correcting an imprecision in this task's own original description, found by reading
    // AvatarRenderer.cpp directly before writing a line of demo code: getParentBonesProperty()
    // does NOT throw - it is a plain, always-succeeding getter returning a real, fixed 71-entry
    // Xbox-standard parent-bone-index hierarchy (kParentBoneIds), independent of State entirely.
    const auto parentBones = renderer.getParentBonesProperty();
    std::printf("2. getParentBonesProperty() = %d real entries (does NOT throw - confirmed by "
                "reading AvatarRenderer.cpp: it is a plain getter over a fixed, always-populated "
                "71-entry table, entirely independent of State). First 5 values: "
                "[%d, %d, %d, %d, %d]\n",
                parentBones.getCountProperty(), parentBones[0], parentBones[1], parentBones[2],
                parentBones[3], parentBones[4]);

    try
    {
        auto bindPose = renderer.getBindPoseProperty();
        std::printf("3. getBindPoseProperty() unexpectedly did not throw (%d entries).\n",
                    bindPose.getCountProperty());
    }
    catch (const System::InvalidOperationException& ex)
    {
        std::printf("3. getBindPoseProperty() threw InvalidOperationException as expected: "
                    "\"%s\" (checks the raw internal state field directly, not "
                    "getStateProperty(), but since nothing anywhere ever sets it to Ready, the "
                    "practical result is identical either way).\n",
                    ex.what());
    }

    std::printf("\nContrast: the EXT path demo_avatar/this codebase's own avatar demos actually "
                "use for real skeleton data is entirely separate from the above (this class's "
                "own doc comment: \"entirely independent of the real Xbox Avatar's 71-bone "
                "arrays\") - loading the real avatar content:\n\n");

    auto& content = getContentProperty();
    auto model = content.Load<std::shared_ptr<SkinnedModelEXT>>(
        AvatarBodyTypeToContentNameEXT(AvatarBodyType::Male));

    std::printf("4. SkinnedModelEXT::BoneCount = %d (real, working, loaded from Content - unlike "
                "AvatarRenderer's own fixed 71, this count matches whatever the content pipeline's "
                "canonical skeleton table actually defines).\n", model->BoneCount);
    std::printf("   First 5 ParentBoneIndices: [%d, %d, %d, %d, %d]\n",
                model->ParentBoneIndices[0], model->ParentBoneIndices[1], model->ParentBoneIndices[2],
                model->ParentBoneIndices[3], model->ParentBoneIndices[4]);
    std::printf("   %zu animation clips loaded, %zu renderable parts.\n",
                model->Clips.size(), model->Parts.size());

    std::printf("\n=== Summary: State always Unavailable, ParentBones genuinely works (71 "
                "fixed Xbox-standard entries), BindPose always throws, and the real usable "
                "skeleton for actual rendering/animation lives entirely in the separate "
                "SkinnedModelEXT EXT path, not in AvatarRenderer's own faithful-XNA surface. ===\n");

    // Task 8 (plans/plan_net.md Phase 8): F1 help overlay plumbing - see this class's own
    // AvatarDemo-mirroring comment in BoundaryDemo.hpp.
    auto& device = getGraphicsDeviceProperty();
    spriteBatch_ = std::make_unique<SpriteBatch>(device);
    const std::vector<uint8_t> px = {255, 255, 255, 255};
    whitePixel_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, px));
    font_ = CNAExamplesEXT::MakeSimpleFontEXT(device);
}

void BoundaryDemo::Update(GameTime& /*gameTime*/)
{
    const auto kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape)) { Exit(); return; }

    // Task 8.2: F1 toggles overlay visibility, edge-triggered.
    const bool f1Down = kb.IsKeyDown(Keys::F1);
    if (f1Down && !f1WasDownEXT_)
    {
        showHelpEXT_ = !showHelpEXT_;
    }
    f1WasDownEXT_ = f1Down;

    if (--framesBeforeExit_ <= 0)
    {
        Exit();
    }
}

void BoundaryDemo::Draw(const GameTime& /*gameTime*/)
{
    auto& device = getGraphicsDeviceProperty();
    device.Clear(Color(18, 18, 28, 255));

    // Task 8.2: 2D help overlay drawn on top - the only meaningful on-screen content this demo
    // has, since its real output is the console text printed during Initialize().
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

    // Task 8.5 (plans/plan_net.md Phase 8): same framesBeforeExit_==1 timing as demo_avatar's own
    // smokeFramesLeft_==1 convention - Game::Exit() suppresses Draw() on the frame Update()
    // actually calls it.
    if (framesBeforeExit_ == 1 && !screenshotPathEXT_.empty())
    {
        SaveBackBufferScreenshotEXT(device, screenshotPathEXT_);
        screenshotPathEXT_.clear();
    }
}
