// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-52: end-to-end regression test for the offline glTF -> .cnj converter
// (tools/gltf_to_cnj/gltf_to_cnj.cpp), spawned as a real subprocess (CNA_GLTF_TO_CNJ_TOOL_PATH,
// baked in by cmake/UnitTests.cmake) -- same "needs a real separate executable, not a library
// call" reasoning as TwoProcessLoopbackTest.cpp/AudioMixerTests.cpp.
//
// The embedded fixture below is a deliberately adversarial hand-built glTF (self-contained via a
// base64 data-URI buffer, no external files) exercising, in one small file, both real bugs
// tools/avatar_asset_pipeline/convert_avatar.py already found and this converter's own doc
// comment says it carries forward:
//   - Non-indexed primitive (no "indices" on the mesh primitive) -- Khronos's own "Fox" sample has
//     this shape; the fixture's single triangle omits "indices" entirely.
//   - Non-topological skin.joints order -- "skin.joints": [1, 0] deliberately lists the CHILD
//     bone (node 1) before the PARENT bone (node 0), so a converter that failed to reorder would
//     produce SkeletonHierarchy[1] (a real bone) with a parent index >= its own index, or would
//     mis-map the vertex JOINTS_0/animation-channel bone indices against the wrong entry.
// It additionally exercises the "channel missing for a bone that has other animated channels"
// fallback: only the child bone's rotation is animated (no translation/scale channel at all), so
// a converter that defaulted a missing channel to Vector3.Zero/Quaternion.Identity instead of the
// bone's own real bind pose would produce a wrong translation, not just a missing one.
//
// This test was also verified against a real official Khronos glTF-Sample-Assets model
// (CesiumMan.glb, 19 real bones, a real 2-second walk-cycle animation) during development --
// downloading that ~430KB asset is not appropriate for an automated, network-free test, so this
// fixture reproduces the specific hard cases in miniature instead.

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"

extern char** environ;

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_tool_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    // Deliberately adversarial: non-indexed triangle; skin.joints lists the child bone (node 1)
    // before the parent (node 0); only the child bone's rotation is animated. See file header.
    const char* kTinySkinnedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 2] } ],
  "nodes": [
    { "name": "RootBone", "children": [1], "translation": [0, 0, 0] },
    { "name": "ChildBone", "translation": [0, 1.5, 0] },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [
    { "primitives": [ { "attributes": {
        "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
    } } ] }
  ],
  "skins": [ { "joints": [1, 0], "inverseBindMatrices": 5 } ],
  "animations": [ {
    "name": "Spin",
    "samplers": [ { "input": 6, "output": 7, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ]
  } ],
  "buffers": [ {
    "byteLength": 336,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA8wQ1PwAAAADzBDU/"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 296, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 304, "byteLength": 32 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC4" }
  ]
})GLTF";

    // Spawns the real cna_tool_gltf_to_cnj executable and waits for it to exit. Returns the exit
    // code, or -1 on a spawn-side failure (already reported via ADD_FAILURE).
    int RunGltfToCnjTool(const std::string& input, const std::string& outDir, const std::string& baseName)
    {
        char* argv[] = {
            const_cast<char*>(CNA_GLTF_TO_CNJ_TOOL_PATH),
            const_cast<char*>(input.c_str()),
            const_cast<char*>(outDir.c_str()),
            const_cast<char*>(baseName.c_str()),
            nullptr,
        };

        pid_t pid = -1;
        const int rc = posix_spawn(&pid, CNA_GLTF_TO_CNJ_TOOL_PATH, nullptr, nullptr, argv, environ);
        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_GLTF_TO_CNJ_TOOL_PATH << ") failed: " << std::strerror(rc);
            return -1;
        }

        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
}

TEST(GltfToCnjToolTest, ConvertsIndexlessDualBoneSkinnedFixtureAndLoadsBackThroughContentManager)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "tiny.gltf";
    WriteFile(gltfPath, kTinySkinnedGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "tiny");
    ASSERT_EQ(exitCode, 0);

    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "tiny.cnj"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "tiny.skeleton.bin"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "tiny_Spin.cnj"));

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("tiny");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);

    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_EQ(skinningData->BoneCount, 2);

    // The fixture authored skin.joints as [ChildBone, RootBone] (child first) specifically to
    // force a real topological reorder -- after conversion, bone 0 must be the root (RootBone,
    // parent -1) and bone 1 must be its child (ChildBone, parent 0), regardless of glTF's own
    // authoring order.
    ASSERT_EQ(skinningData->SkeletonHierarchy.size(), 2u);
    EXPECT_EQ(skinningData->SkeletonHierarchy[0], -1);
    EXPECT_EQ(skinningData->SkeletonHierarchy[1], 0);

    // ChildBone's own authored bind-pose translation is [0, 1.5, 0] -- BindPose[1] must reflect
    // it (proves BuildSkeleton's node-transform extraction, not just the parent-index remap).
    ASSERT_EQ(skinningData->BindPose.size(), 2u);
    EXPECT_NEAR(skinningData->BindPose[1].getTranslationProperty().Y, 1.5f, 1e-4f);

    ASSERT_TRUE(skinningData->AnimationClips.count("Spin"));
    const auto& clip = skinningData->AnimationClips.at("Spin");
    ASSERT_EQ(clip.Tracks.size(), 1u);
    // The animated bone must be reported as new-index 1 (ChildBone after reorder), not the
    // old, pre-reorder glTF joints-array index 0 the animation channel's target node actually
    // had.
    EXPECT_EQ(clip.Tracks[0].BoneIndex, 1);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 2u);

    // Only "rotation" was animated (no translation/scale channel at all) -- every key's
    // Translation must still be ChildBone's own bind-pose value (1.5), not an unrelated
    // Vector3.Zero default.
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Translation.Y, 1.5f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Translation.Y, 1.5f, 1e-4f);

    // Rotation samples: identity at t=0, a 90-degree turn about Y at t=1 (as authored).
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Rotation.W, 1.0f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Rotation.Y, 0.70710678f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Rotation.W, 0.70710678f, 1e-4f);

    // Real playback end-to-end: at the animation's midpoint, the child bone's skin transform
    // must differ from bind pose (proves the resampled keyframes actually drive AnimationPlayer,
    // not just that the .cnj text looks right).
    AnimationPlayer player(*skinningData);
    player.StartClip(clip);
    player.Update(System::TimeSpan::FromSeconds(0.5), false, true);
    const auto& skin = player.GetSkinTransforms();
    ASSERT_EQ(skin.size(), 2u);
    const Matrix identity = Matrix::getIdentityProperty();
    const Matrix& childSkin = skin[1];
    const bool differsFromIdentity =
        std::fabs(childSkin.M11 - identity.M11) > 1e-4f || std::fabs(childSkin.M13 - identity.M13) > 1e-4f;
    EXPECT_TRUE(differsFromIdentity);
}

TEST(GltfToCnjToolTest, MissingInputFileFailsCleanly)
{
    ScratchDir contentRoot;
    const int exitCode = RunGltfToCnjTool("/nonexistent/path/does_not_exist.gltf",
                                           contentRoot.path().string(), "wont_happen");
    EXPECT_NE(exitCode, 0);
    EXPECT_FALSE(std::filesystem::exists(contentRoot.path() / "wont_happen.cnj"));
}
