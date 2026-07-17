// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-70/71 (Phase 13D): end-to-end regression test for runtime (non-CLI) glTF
// loading -- ContentManager::Load<Model>("name") finding and parsing a "name.gltf" file directly
// via ModelTypeReader::ReadGltfModel(), with no intermediate .cnj/binary sidecar files at all.
// Complements GltfToCnjToolTests.cpp (which tests the offline CLI tool, a separate process) --
// these tests call ContentManager in-process instead, since ReadGltfModel() is now a library
// function shared with the CLI tool via CNA::Internal::GltfImport::GltfImportCore, not a
// subprocess.
//
// Two embedded fixtures, both self-contained (single base64 data-URI buffer, no external files):
//   - kBasicTexturedGltf: an unskinned single triangle with a base-color texture -- proves the
//     extension resolution (.gltf found via ModelTypeReader::GetExtensions()), parsing, vertex/
//     index buffer construction, and in-memory texture decoding (MemoryStream + Texture2D::
//     FromStream, no temp file) all work for the simplest case.
//   - kSkinnedAnimatedGltf: a two-bone skinned, textured, animated triangle with skin.joints
//     deliberately reversed ([1, 0], child before parent) -- proves topological bone reordering,
//     SkinningData/AnimationClip construction, and SkinnedEffect wiring all work when built
//     directly in-memory (no .skeleton.bin/.clip.cnj round-trip at all).

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Tests-only scratch content root, unique per test process run. Mirrors the established
    // convention in GltfToCnjToolTests.cpp/CnjModelTests.cpp.
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_runtime_gltf_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    const char* kBasicTexturedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 3, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 165,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // Two bones: ParentBone (node 0, root) -> ChildBone (node 1). skin.joints = [1, 0] --
    // deliberately reversed (child listed before parent), the same topological-reorder stress
    // case GltfToCnjToolTests.cpp's own kTinySkinnedGltf uses. All 3 vertices are fully weighted
    // to joint index 1 (the child, per skin.joints[1]=0 -- i.e. the *second* entry names the
    // parent). One LINEAR translation channel on the child bone: (0,0,0) at t=0 -> (2,0,0) at t=1.
    const char* kSkinnedAnimatedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 2] } ],
  "nodes": [
    { "name": "ParentBone", "children": [1] },
    { "name": "ChildBone" },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
  }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 8, "mimeType": "image/png" } ],
  "skins": [ { "joints": [1, 0], "inverseBindMatrices": 5 } ],
  "animations": [ {
    "name": "Wave",
    "samplers": [ { "input": 6, "output": 7, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } } ]
  } ],
  "buffers": [ {
    "byteLength": 397,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAAABAAAAAAAAAAEAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAIlQTkcNChoKAAAADUlIRFIAAAABAAAAAQgCAAAAkHdT3gAAAAxJREFUeJxj+M/AAAADAQEAyf6S7wAAAABJRU5ErkJggg=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 296, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 304, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 328, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC3" }
  ]
})GLTF";

    // CNB-64/65 (Phase 13B): an unskinned triangle with one morph target (POSITION delta only,
    // uniform +Z=1 per vertex), a non-zero default weight (mesh.weights=[0.5], to prove the
    // *initial* upload reflects the file's own default blend, not always the raw base pose), and
    // a LINEAR "weights" animation channel (0.0 at t=0 -> 1.0 at t=1) on the mesh's own node.
    const char* kMorphedTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "targets": [ { "POSITION": 3 } ] } ],
    "weights": [0.5]
  } ],
  "animations": [ {
    "name": "Morph",
    "samplers": [ { "input": 4, "output": 5, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 0, "path": "weights" } } ]
  } ],
  "buffers": [ {
    "byteLength": 148,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAACAPw=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 132, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 140, "byteLength": 8 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] }
  ]
})GLTF";
}

TEST(RuntimeGltfModelTest, LoadsUnskinnedTexturedModelDirectlyFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "basic.gltf", kBasicTexturedGltf);

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    // No "basic.cnj" exists -- ResolveAssetPath must fall through to ModelTypeReader's own
    // GetExtensions() list and find "basic.gltf".
    Model model = cm.Load<Model>("basic");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 1);

    auto* basicFx = dynamic_cast<BasicEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(basicFx, nullptr);
    EXPECT_TRUE(basicFx->getTextureEnabledProperty());
    Texture2D* tex = basicFx->getTextureProperty();
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getWidthProperty(), 1);
    EXPECT_EQ(tex->getHeightProperty(), 1);

    // No skeleton in this fixture -- Tag must stay null (SkinningData's own documented default).
    EXPECT_EQ(model.getTagProperty(), nullptr);
}

TEST(RuntimeGltfModelTest, LoadsSkinnedAnimatedModelDirectlyFromGltfWithReversedJointOrder)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "skinned.gltf", kSkinnedAnimatedGltf);

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("skinned");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* skinnedFx = dynamic_cast<SkinnedEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedFx, nullptr);
    Texture2D* tex = skinnedFx->getTextureProperty();
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getWidthProperty(), 1);

    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_EQ(skinningData->BoneCount, 2);
    ASSERT_EQ(skinningData->SkeletonHierarchy.size(), 2u);
    // Reversed skin.joints=[1,0] must be topologically reordered so the PARENT (glTF node 0)
    // ends up before the CHILD (glTF node 1) -- new index 0 must be a root (parentIndex -1), and
    // new index 1's parent must be new index 0.
    EXPECT_EQ(skinningData->SkeletonHierarchy[0], -1);
    EXPECT_EQ(skinningData->SkeletonHierarchy[1], 0);

    ASSERT_EQ(skinningData->AnimationClips.count("Wave"), 1u);
    const AnimationClip& clip = skinningData->AnimationClips.at("Wave");
    EXPECT_NEAR(clip.Duration.getTotalSecondsProperty(), 1.0, 1e-6);
    ASSERT_EQ(clip.Tracks.size(), 1u);
    // The animated bone is the CHILD (glTF node 1), which topologically reorders to new index 1.
    EXPECT_EQ(clip.Tracks[0].BoneIndex, 1);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 2u);
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Time.getTotalSecondsProperty(), 0.0, 1e-6);
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Translation.X, 0.0f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Time.getTotalSecondsProperty(), 1.0, 1e-6);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Translation.X, 2.0f, 1e-4f);
}

// CNB-64/65 (Phase 13B): morph target deltas, default weights, and weight animation, all
// extracted directly from a glTF file with no .cnj/binary sidecars.
TEST(RuntimeGltfModelTest, LoadsMorphTargetDataWithDefaultWeightsAndWeightAnimationFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "morph.gltf", kMorphedTriangleGltf);

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("morph");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ModelMeshPart* part = mesh->getMeshPartsProperty()[0];

    auto* morph = dynamic_cast<MorphTargetDataEXT*>(part->getTagProperty());
    ASSERT_NE(morph, nullptr);
    ASSERT_EQ(morph->PositionDeltas.size(), 1u);
    ASSERT_EQ(morph->PositionDeltas[0].size(), 3u);
    EXPECT_NEAR(morph->PositionDeltas[0][0].Z, 1.0f, 1e-5f);
    // No NORMAL delta was authored for this target.
    EXPECT_TRUE(morph->NormalDeltas[0].empty());

    // mesh.weights=[0.5] must already be reflected: BaseVertexBytes is the raw (zero-weight)
    // pose, and Weights holds the applied default -- BlendMorphTargetsEXT(morph, morph->Weights)
    // must reproduce what was actually uploaded (vertex 0's Z = 0 + 0.5*1.0).
    ASSERT_EQ(morph->Weights.size(), 1u);
    EXPECT_NEAR(morph->Weights[0], 0.5f, 1e-5f);
    const auto blendedAtDefault = BlendMorphTargetsEXT(*morph, morph->Weights);
    float z0;
    std::memcpy(&z0, blendedAtDefault.data() + 2 * sizeof(float), sizeof(float));
    EXPECT_NEAR(z0, 0.5f, 1e-5f);

    // Weight animation: LINEAR 0.0 at t=0 -> 1.0 at t=1.
    ASSERT_EQ(morph->WeightTrack.Keys.size(), 2u);
    EXPECT_FALSE(morph->WeightTrack.StepInterpolation);
    EXPECT_NEAR(morph->WeightTrack.Keys[0].Weights[0], 0.0f, 1e-5f);
    EXPECT_NEAR(morph->WeightTrack.Keys[1].Weights[0], 1.0f, 1e-5f);
    const auto midWeights = EvaluateMorphWeightsEXT(morph->WeightTrack, 0.5);
    ASSERT_EQ(midWeights.size(), 1u);
    EXPECT_NEAR(midWeights[0], 0.5f, 1e-5f);
}
