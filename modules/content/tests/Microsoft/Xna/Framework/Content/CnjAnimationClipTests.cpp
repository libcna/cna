// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-40/CNB-41: first-ever gtest coverage for AnimationClipTypeReader -- a
// directly-loadable .cnj AnimationClip document, independent of any specific Model. Before this,
// AnimationClipEXT/KeyframeEXT data only ever loaded as part of ModelTypeReader's/
// SkinnedModelTypeReader's own "animations" field, always referencing a raw .clip.bin binary
// file; cnj.md's own per-type conventions table flagged a standalone AnimationClip reader as a
// "natural, open-ended follow-up" left undone by plans/plan_cnj.md's original 9 phases.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors CnjModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_animclip_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    void AppendDouble(std::vector<std::uint8_t>& out, double v)
    {
        std::uint8_t bytes[8];
        std::memcpy(bytes, &v, 8);
        out.insert(out.end(), bytes, bytes + 8);
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

    // Byte-for-byte the same .clip.bin layout ReadAnimationClipFileEXT() reads: a double
    // duration, an int32 track count, then per track an int32 bone index + int32 key count, then
    // per key a double time and 10 floats (translation xyz, rotation xyzw, scale xyz).
    void WriteSingleTrackClipBin(const std::filesystem::path& path)
    {
        std::vector<std::uint8_t> bytes;
        AppendDouble(bytes, 2.0);   // Duration
        AppendInt32(bytes, 1);      // trackCount
        AppendInt32(bytes, 3);      // boneIndex
        AppendInt32(bytes, 1);      // keyCount
        AppendDouble(bytes, 0.5);   // key time
        AppendFloat(bytes, 1.0f); AppendFloat(bytes, 2.0f); AppendFloat(bytes, 3.0f); // translation
        AppendFloat(bytes, 0.0f); AppendFloat(bytes, 0.0f); AppendFloat(bytes, 0.0f); AppendFloat(bytes, 1.0f); // rotation
        AppendFloat(bytes, 1.0f); AppendFloat(bytes, 1.0f); AppendFloat(bytes, 1.0f); // scale

        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

class CnjAnimationClipTest : public ::testing::Test
{
};

TEST_F(CnjAnimationClipTest, LoadsInlineTracksFixture)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "walk.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "duration": 1.5,
        "tracks": [
            {
                "boneIndex": 2,
                "keys": [
                    { "time": 0.0, "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
                    { "time": 1.5, "translation": [1, 2, 3], "rotation": [0, 1, 0, 0], "scale": [2, 2, 2] }
                ]
            }
        ]
    })");

    ContentManager cm(nullptr, root.path().string());

    AnimationClipEXT clip = cm.Load<AnimationClipEXT>("walk");

    EXPECT_DOUBLE_EQ(clip.Duration.getTotalSecondsProperty(), 1.5);
    ASSERT_EQ(clip.Tracks.size(), 1u);
    EXPECT_EQ(clip.Tracks[0].BoneIndex, 2);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 2u);

    EXPECT_DOUBLE_EQ(clip.Tracks[0].Keys[0].Time.getTotalSecondsProperty(), 0.0);
    EXPECT_EQ(clip.Tracks[0].Keys[0].Translation, Vector3(0, 0, 0));
    EXPECT_EQ(clip.Tracks[0].Keys[0].Rotation, Quaternion(0, 0, 0, 1));
    EXPECT_EQ(clip.Tracks[0].Keys[0].Scale, Vector3(1, 1, 1));

    EXPECT_DOUBLE_EQ(clip.Tracks[0].Keys[1].Time.getTotalSecondsProperty(), 1.5);
    EXPECT_EQ(clip.Tracks[0].Keys[1].Translation, Vector3(1, 2, 3));
    EXPECT_EQ(clip.Tracks[0].Keys[1].Rotation, Quaternion(0, 1, 0, 0));
    EXPECT_EQ(clip.Tracks[0].Keys[1].Scale, Vector3(2, 2, 2));
}

TEST_F(CnjAnimationClipTest, KeyframeDefaultsAppliedWhenFieldsOmitted)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "bind.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "duration": 0.25,
        "tracks": [
            { "boneIndex": 0, "keys": [ { "time": 0.0 } ] }
        ]
    })");

    ContentManager cm(nullptr, root.path().string());

    AnimationClipEXT clip = cm.Load<AnimationClipEXT>("bind");

    ASSERT_EQ(clip.Tracks.size(), 1u);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 1u);
    const auto& key = clip.Tracks[0].Keys[0];
    EXPECT_EQ(key.Translation, Vector3(0, 0, 0));
    EXPECT_EQ(key.Rotation, Quaternion::Identity);
    EXPECT_EQ(key.Scale, Vector3(1, 1, 1));
}

TEST_F(CnjAnimationClipTest, LoadsClipFileFixture)
{
    ScratchContentRoot root;
    WriteSingleTrackClipBin(root.path() / "run.clip.bin");
    WriteFile(root.path() / "run.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "clipFile": "run.clip.bin"
    })");

    ContentManager cm(nullptr, root.path().string());

    AnimationClipEXT clip = cm.Load<AnimationClipEXT>("run");

    EXPECT_DOUBLE_EQ(clip.Duration.getTotalSecondsProperty(), 2.0);
    ASSERT_EQ(clip.Tracks.size(), 1u);
    EXPECT_EQ(clip.Tracks[0].BoneIndex, 3);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 1u);
    EXPECT_EQ(clip.Tracks[0].Keys[0].Translation, Vector3(1, 2, 3));
}

TEST_F(CnjAnimationClipTest, MissingTracksAndClipFileThrows)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "empty.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "duration": 1.0
    })");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<AnimationClipEXT>("empty"), ContentLoadException);
}

TEST_F(CnjAnimationClipTest, BothTracksAndClipFileThrows)
{
    ScratchContentRoot root;
    WriteSingleTrackClipBin(root.path() / "ambiguous.clip.bin");
    WriteFile(root.path() / "ambiguous.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "duration": 1.0,
        "tracks": [],
        "clipFile": "ambiguous.clip.bin"
    })");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<AnimationClipEXT>("ambiguous"), ContentLoadException);
}

TEST_F(CnjAnimationClipTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj", R"({
        "cnjVersion": 1,
        "type": "Model"
    })");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<AnimationClipEXT>("wrong"), ContentLoadException);
}
