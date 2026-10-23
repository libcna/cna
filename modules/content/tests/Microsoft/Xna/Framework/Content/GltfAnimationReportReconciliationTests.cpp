// SPDX-License-Identifier: MS-PL
//
// CNA-DEMOS-ISSUE-094/095: a loose glTF Model's import report calls an animation channel lost only
// when none of the Model's carriers keeps it. Each extractor sees one carrier -- a joint palette
// skips another palette's joints, the scene-node path skips the `weights` channel a morph track
// keeps -- so the Model's report is reconciled against all of them.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GltfImportReportEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"

using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_report_reconciliation_" +
                      std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    std::size_t CountOf(const GltfImportReportEXT& report, const std::string& code)
    {
        std::size_t count = 0;
        for (const GltfImportDiagnosticEXT& diagnostic : report.Diagnostics)
        {
            if (diagnostic.Code == code) { count += diagnostic.Count; }
        }
        return count;
    }

    void AppendFloats(std::vector<std::uint8_t>& out, const std::vector<float>& values)
    {
        for (const float value : values)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, sizeof(bytes));
            out.insert(out.end(), bytes, bytes + 4);
        }
    }

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t n =
                (static_cast<std::uint32_t>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
            out += alphabet[(n >> 18) & 63];
            out += alphabet[(n >> 12) & 63];
            out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
            out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
        }
        return out;
    }

    /// Two skins of one joint each; the one animation moves LeftJoint up and RightJoint down.
    /// @p extraChannel, when not empty, is appended to the animation's channels.
    std::string TwoSkinDocument(const std::string& extraChannel = {})
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, {0, 0, 0,  1, 0, 0,  0, 1, 0});              // 0: POSITION, 36 bytes
        buffer.insert(buffer.end(), 12, std::uint8_t{0});                  // 1: JOINTS_0, 12 bytes
        AppendFloats(buffer, {1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0});      // 2: WEIGHTS_0, 48 bytes
        AppendFloats(buffer, {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1}); // 3: IBM, 64
        AppendFloats(buffer, {0, 1});                                      // 4: times, 8 bytes
        AppendFloats(buffer, {0, 0, 0,  0, 0.2f, 0});                      // 5: left, 24 bytes
        AppendFloats(buffer, {0, 0, 0,  0, -0.3f, 0});                     // 6: right, 24 bytes
        std::string document = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3] } ],
  "nodes": [
    { "name": "LeftJoint" },
    { "name": "RightJoint" },
    { "name": "LeftMesh", "mesh": 0, "skin": 0 },
    { "name": "RightMesh", "mesh": 1, "skin": 1 }
  ],
  "skins": [
    { "name": "LeftRig", "joints": [0], "inverseBindMatrices": 3 },
    { "name": "RightRig", "joints": [1], "inverseBindMatrices": 3 }
  ],
  "meshes": [
    { "primitives": [ { "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 } } ] },
    { "primitives": [ { "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 } } ] }
  ],
  "animations": [ {
    "name": "SplitMotion",
    "samplers": [ { "input": 4, "output": 5 }, { "input": 4, "output": 6 } ],
    "channels": [
      { "sampler": 0, "target": { "node": 0, "path": "translation" } },
      { "sampler": 1, "target": { "node": 1, "path": "translation" } }@EXTRA@
    ]
  } ],
  "buffers": [ { "byteLength": 216, "uri": "data:application/octet-stream;base64,@BUFFER@" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 48, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 64 },
    { "buffer": 0, "byteOffset": 160, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 192, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1] },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3" }
  ]
})GLTF";
        document.replace(document.find("@EXTRA@"), 7, extraChannel);
        document.replace(document.find("@BUFFER@"), 8, Base64(buffer));
        return document;
    }

    Model LoadTwoSkinModel(GraphicsDevice& device, const ScratchDir& root,
                           const std::string& extraChannel = {})
    {
        std::ofstream(root.path() / "two_skins.gltf", std::ios::binary) << TwoSkinDocument(extraChannel);
        ContentManager content(nullptr, root.path().string());
        content.setGraphicsDevice(device);
        return content.Load<Model>("two_skins");
    }
}

TEST(GltfAnimationReportReconciliation, AMorphWeightChannelKeptByItsTrackIsNotReportedLost)
{
    // CNA-DEMOS-ISSUE-094. anim-weights-path drives one morph node's rotation and weights; the
    // rotation becomes a scene-node clip and the weights become the part's morph track.
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus)) { GTEST_SKIP() << "the fixture corpus is not present"; }
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ContentManager content(nullptr, corpus.string());
    content.setGraphicsDevice(device);
    Model model = content.Load<Model>("anim-weights-path");

    const MorphTargetDataEXT* morph = nullptr;
    for (ModelMesh* mesh : model.getMeshesProperty())
    {
        for (ModelMeshPart* part : mesh->getMeshPartsProperty())
        {
            if (auto* data = dynamic_cast<MorphTargetDataEXT*>(part->getTagProperty())) { morph = data; }
        }
    }
    ASSERT_NE(nullptr, morph);
    EXPECT_FALSE(morph->WeightTrack.Keys.empty());

    const GltfImportReportEXT& report = model.getGltfImportReportEXTProperty();
    EXPECT_EQ(0u, CountOf(report, "animation-paths-unsupported"));
    EXPECT_EQ(0u, CountOf(report, "animations-empty"));
    EXPECT_EQ(0u, CountOf(report, "animation-targets-unavailable"));
    EXPECT_EQ(1u, report.AnimationCount);
}

TEST(GltfAnimationReportReconciliation, AChannelAnotherPaletteKeepsIsNotReportedLost)
{
    // CNA-DEMOS-ISSUE-095. Each palette retains the channel on its own joint; the other channel
    // is skipped by that palette but kept by the other one.
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ScratchDir root;
    Model model = LoadTwoSkinModel(device, root);

    const std::vector<ModelSkinEXT>& skins = model.getSkinsEXTProperty();
    ASSERT_EQ(2u, skins.size());
    for (const ModelSkinEXT& skin : skins)
    {
        SCOPED_TRACE(skin.Name);
        ASSERT_NE(nullptr, skin.Data);
        const auto clip = skin.Data->AnimationClips.find("SplitMotion");
        ASSERT_NE(skin.Data->AnimationClips.end(), clip);
        EXPECT_EQ(1u, clip->second.Tracks.size());
    }

    const GltfImportReportEXT& report = model.getGltfImportReportEXTProperty();
    EXPECT_EQ(0u, CountOf(report, "animation-targets-unavailable"));
    EXPECT_EQ(0u, CountOf(report, "animations-empty"));
    EXPECT_EQ(0u, CountOf(report, "animation-paths-unsupported"));
    EXPECT_EQ(1u, report.AnimationCount);
    EXPECT_EQ(2u, report.ClipCount);
}

TEST(GltfAnimationReportReconciliation, AChannelNoCarrierKeepsIsStillReportedOnce)
{
    // The control: a translation channel on LeftMesh -- a scene node, not a joint of either
    // palette -- has nowhere to go on a skinned Model, so it is reported lost exactly once rather
    // than once per palette.
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ScratchDir root;
    Model model = LoadTwoSkinModel(
        device, root, R"(,
      { "sampler": 0, "target": { "node": 2, "path": "translation" } })");

    const GltfImportReportEXT& report = model.getGltfImportReportEXTProperty();
    EXPECT_EQ(1u, CountOf(report, "animation-targets-unavailable"));
    EXPECT_EQ(0u, CountOf(report, "animations-empty"));
    EXPECT_TRUE(report.AnythingLost());
}
