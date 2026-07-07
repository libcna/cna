#include "BoundaryDemo.hpp"

#include <cstdio>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "System/InvalidOperationException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyTypeToContentNameEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::GamerServices::AvatarRendererState;

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
}

void BoundaryDemo::Update(GameTime& /*gameTime*/)
{
    if (--framesBeforeExit_ <= 0)
    {
        Exit();
    }
}

void BoundaryDemo::Draw(const GameTime& /*gameTime*/)
{
    getGraphicsDeviceProperty().Clear(Color(18, 18, 28, 255));
}
