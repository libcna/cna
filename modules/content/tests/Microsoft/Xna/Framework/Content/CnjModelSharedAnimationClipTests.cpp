// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-48/CNB-49: a Model's "animations" entry may now name either a raw .clip.bin
// binary blob (the original, still-supported shape) or a standalone, shareable .cnj
// AnimationClip asset (Phase 10), loaded/cached through the normal ContentManager path
// (ReadAnimationClipRefEXT, dispatched by extension). This file covers the Model side; the
// binary-blob shape's own coverage already exists in the 7 migrated examples/*model_json* fixtures
// and is unaffected by this change (same code path as before whenever "clip" doesn't end in
// ".cnj").

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::SkinningData;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_model_shared_clip_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    void AppendFloat(std::vector<std::uint8_t>& out, float v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }

    void AppendInt32(std::vector<std::uint8_t>& out, std::int32_t v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }

    void AppendUint16(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        std::uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        out.insert(out.end(), bytes, bytes + 2);
    }

    void AppendIdentityMatrix(std::vector<std::uint8_t>& out)
    {
        const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        for (float f : identity) { AppendFloat(out, f); }
    }

    // Minimal single-bone skeleton.bin: boneCount=1, SkeletonHierarchy=[-1] (root), identity
    // BindPose/InverseBindPose.
    void WriteOneBoneSkeletonBin(const std::filesystem::path& path)
    {
        std::vector<std::uint8_t> bytes;
        AppendInt32(bytes, 1);
        AppendInt32(bytes, -1);
        AppendIdentityMatrix(bytes);
        AppendIdentityMatrix(bytes);
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Byte-for-byte the .clip.bin layout ReadAnimationClipFileEXT() reads.
    void WriteClipBin(const std::filesystem::path& path, double durationSeconds)
    {
        std::vector<std::uint8_t> bytes;
        std::uint8_t durBytes[8];
        std::memcpy(durBytes, &durationSeconds, 8);
        bytes.insert(bytes.end(), durBytes, durBytes + 8);
        AppendInt32(bytes, 0); // trackCount = 0 (bone-count/keys not needed for this test)
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void WriteQuadVertsAndIndices(const std::filesystem::path& root, const std::string& baseName)
    {
        struct V { float x, y, z, nx, ny, nz, u, v; };
        const V verts[4] = {
            { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
            { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
            {  0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
            {  0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        };
        std::vector<std::uint8_t> vertBytes;
        for (const auto& v : verts) {
            AppendFloat(vertBytes, v.x);  AppendFloat(vertBytes, v.y);  AppendFloat(vertBytes, v.z);
            AppendFloat(vertBytes, v.nx); AppendFloat(vertBytes, v.ny); AppendFloat(vertBytes, v.nz);
            AppendFloat(vertBytes, v.u);  AppendFloat(vertBytes, v.v);
        }
        std::ofstream vf(root / (baseName + "_verts.bin"), std::ios::binary);
        vf.write(reinterpret_cast<const char*>(vertBytes.data()), static_cast<std::streamsize>(vertBytes.size()));
        vf.close();

        std::vector<std::uint8_t> idxBytes;
        for (std::uint16_t i : { 0, 1, 2, 0, 2, 3 }) { AppendUint16(idxBytes, i); }
        std::ofstream idxf(root / (baseName + "_idx.bin"), std::ios::binary);
        idxf.write(reinterpret_cast<const char*>(idxBytes.data()), static_cast<std::streamsize>(idxBytes.size()));
    }

    void WriteSkinnedQuadModelFixture(const std::filesystem::path& root, const std::string& modelName,
                                       const std::string& clipFileRef)
    {
        WriteQuadVertsAndIndices(root, modelName);
        WriteOneBoneSkeletonBin(root / (modelName + ".skeleton.bin"));

        WriteFile(root / (modelName + ".cnj"), R"({
  "cnjVersion": 1,
  "type": "Model",
  "skeleton": ")" + modelName + R"(.skeleton.bin",
  "animations": [ { "name": "Walk", "clip": ")" + clipFileRef + R"(" } ],
  "meshes": [
    {
      "name": "Quad",
      "vertices": ")" + modelName + R"(_verts.bin",
      "indices": ")" + modelName + R"(_idx.bin",
      "vertexStride": 32,
      "effect": "BasicEffect"
    }
  ]
})");
    }
}

class CnjModelSharedAnimationClipTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjModelSharedAnimationClipTest, TwoModelsShareOneCnjAnimationClip)
{
    // Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline rejects it
    // before the animation-clip sharing this test actually exercises is ever reached. Guarded
    // per-test (not in a fixture-wide SetUp()) so MissingSharedClipThrows below, whose throw
    // happens earlier for an unrelated reason (a missing file), keeps running everywhere.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ScratchContentRoot root;

    WriteFile(root.path() / "walk.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "duration": 2.5,
        "tracks": [ { "boneIndex": 0, "keys": [ { "time": 0.0 }, { "time": 2.5 } ] } ]
    })");

    WriteSkinnedQuadModelFixture(root.path(), "modelA", "walk.cnj");
    WriteSkinnedQuadModelFixture(root.path(), "modelB", "walk.cnj");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Model a = cm.Load<Model>("modelA");
    Model b = cm.Load<Model>("modelB");

    auto* dataA = static_cast<SkinningData*>(a.getTagProperty());
    auto* dataB = static_cast<SkinningData*>(b.getTagProperty());
    ASSERT_NE(dataA, nullptr);
    ASSERT_NE(dataB, nullptr);

    ASSERT_TRUE(dataA->AnimationClips.count("Walk"));
    ASSERT_TRUE(dataB->AnimationClips.count("Walk"));

    const auto& clipA = dataA->AnimationClips.at("Walk");
    const auto& clipB = dataB->AnimationClips.at("Walk");

    EXPECT_DOUBLE_EQ(clipA.Duration.getTotalSecondsProperty(), 2.5);
    EXPECT_DOUBLE_EQ(clipB.Duration.getTotalSecondsProperty(), 2.5);
    ASSERT_EQ(clipA.Tracks.size(), 1u);
    ASSERT_EQ(clipB.Tracks.size(), 1u);
    EXPECT_EQ(clipA.Tracks[0].Keys.size(), clipB.Tracks[0].Keys.size());
}

TEST_F(CnjModelSharedAnimationClipTest, RawClipBinStillWorksUnchanged)
{
    // See TwoModelsShareOneCnjAnimationClip above: Model loading needs a real VertexBuffer.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ScratchContentRoot root;
    WriteClipBin(root.path() / "run.clip.bin", 1.25);
    WriteSkinnedQuadModelFixture(root.path(), "modelC", "run.clip.bin");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("modelC");
    auto* data = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->AnimationClips.count("Walk"));
    EXPECT_DOUBLE_EQ(data->AnimationClips.at("Walk").Duration.getTotalSecondsProperty(), 1.25);
}

TEST_F(CnjModelSharedAnimationClipTest, MissingSharedClipThrows)
{
    ScratchContentRoot root;
    WriteSkinnedQuadModelFixture(root.path(), "modelD", "does_not_exist.cnj");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Model>("modelD"), ContentLoadException);
}
