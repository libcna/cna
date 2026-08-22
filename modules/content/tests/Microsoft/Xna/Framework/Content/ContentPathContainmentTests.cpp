// SPDX-License-Identifier: MS-PL
//
// REMED-CONTENT-007/-008: public-caller traversal oracles for XNB Song/Video media references
// and ContentManager sidecar fields. The fixtures place valid payloads in a controlled sibling-
// prefix directory so a pre-fix load proves which outside path was selected instead of merely
// observing an arbitrary exception.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"
#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::SkinnedModelEXT;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

namespace
{
    namespace fs = std::filesystem;

    class ScratchTree
    {
    public:
        ScratchTree()
            : base_(fs::temp_directory_path()
                    / ("cna_content_path_containment_test_" +
                       std::to_string(reinterpret_cast<std::uintptr_t>(this))))
            , root_(base_ / "content")
            , sibling_(base_ / "content-evil")
        {
            fs::create_directories(root_);
            fs::create_directories(sibling_);
        }

        ~ScratchTree()
        {
            std::error_code ec;
            fs::remove_all(base_, ec);
        }

        ScratchTree(const ScratchTree&) = delete;
        ScratchTree& operator=(const ScratchTree&) = delete;

        [[nodiscard]] const fs::path& root() const { return root_; }
        [[nodiscard]] const fs::path& sibling() const { return sibling_; }

    private:
        fs::path base_;
        fs::path root_;
        fs::path sibling_;
    };

    void WriteText(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    void WriteBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }

    std::string ReadText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    template <typename T>
    void AppendValue(std::vector<std::uint8_t>& bytes, const T& value)
    {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + sizeof(T));
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    void AppendIdentityMatrix(std::vector<std::uint8_t>& bytes)
    {
        const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        for (float value : identity)
        {
            AppendValue(bytes, value);
        }
    }

    std::vector<std::uint8_t> FinishXnb(System::IO::MemoryStream& body)
    {
        const auto bodyBytes = body.ToArray();

        System::IO::MemoryStream file;
        System::IO::BinaryWriter writer(&file, true);
        writer.Write(static_cast<std::uint8_t>('X'));
        writer.Write(static_cast<std::uint8_t>('N'));
        writer.Write(static_cast<std::uint8_t>('B'));
        writer.Write(static_cast<std::uint8_t>('w'));
        writer.Write(static_cast<std::uint8_t>(5));
        writer.Write(static_cast<std::uint8_t>(0));
        writer.Write(static_cast<std::int32_t>(10 + bodyBytes.size()));
        writer.Write(bodyBytes.data(), 0, static_cast<std::int32_t>(bodyBytes.size()));
        writer.Flush();

        const auto fileBytes = file.ToArray();
        return {fileBytes.begin(), fileBytes.end()};
    }

    std::vector<std::uint8_t> BuildSongXnb(const std::string& reference)
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter writer(&body, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(std::string("Microsoft.Xna.Framework.Content.SongReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        writer.Write(reference);
        writer.Write(static_cast<std::int32_t>(1234));
        writer.Flush();
        return FinishXnb(body);
    }

    std::vector<std::uint8_t> BuildVideoXnb(const std::string& reference)
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter writer(&body, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(std::string("Microsoft.Xna.Framework.Content.VideoReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write7BitEncodedInt(0);
        writer.Write7BitEncodedInt(1);
        writer.Write(reference);
        writer.Write(static_cast<std::int32_t>(2000));
        writer.Write(static_cast<std::int32_t>(160));
        writer.Write(static_cast<std::int32_t>(90));
        writer.Write(25.0f);
        writer.Write(static_cast<std::int32_t>(VideoSoundtrackType::Music));
        writer.Flush();
        return FinishXnb(body);
    }

    void WriteClip(const fs::path& path, double durationSeconds)
    {
        std::vector<std::uint8_t> bytes;
        AppendValue(bytes, durationSeconds);
        AppendValue(bytes, std::int32_t{0});
        WriteBytes(path, bytes);
    }

    void WriteZeroBoneSkeleton(const fs::path& path)
    {
        std::vector<std::uint8_t> bytes;
        AppendValue(bytes, std::int32_t{0});
        WriteBytes(path, bytes);
    }

    void WriteOneBoneSkeleton(const fs::path& path)
    {
        std::vector<std::uint8_t> bytes;
        AppendValue(bytes, std::int32_t{1});
        AppendValue(bytes, std::int32_t{-1});
        AppendIdentityMatrix(bytes);
        AppendIdentityMatrix(bytes);
        WriteBytes(path, bytes);
    }

    void WriteTriangle(const fs::path& vertexPath, const fs::path& indexPath,
                       std::size_t stride)
    {
        std::vector<std::uint8_t> vertices(3U * stride, 0);
        std::vector<std::uint8_t> indices;
        AppendValue(indices, std::uint16_t{0});
        AppendValue(indices, std::uint16_t{1});
        AppendValue(indices, std::uint16_t{2});
        WriteBytes(vertexPath, vertices);
        WriteBytes(indexPath, indices);
    }

    void WriteEmptyMorphTargets(const fs::path& path)
    {
        std::vector<std::uint8_t> bytes;
        AppendValue(bytes, std::int32_t{0});
        WriteBytes(path, bytes);
    }

    void WriteTinyPng(GraphicsDevice& device, const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        Texture2D texture(device, 1, 1);
        Color pixel(17, 34, 51, 255);
        texture.SetData(&pixel, 1);
        texture.SaveAsPng(path.string());
    }

    constexpr const char* kVertexShader = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

    constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
out vec4 FragColor;
void main() { FragColor = vec4(1.0); }
)";

    struct ModelPaths
    {
        std::string skeleton = "safe.skeleton.bin";
        std::string vertices = "safe.verts.bin";
        std::string indices = "safe.idx.bin";
        std::string morphTargets;
        std::string clip;
        std::string effect = "BasicEffect";
        std::string texture;
        std::string texture2;
        std::string normalMap;
        std::string metallicRoughnessMap;
        std::string emissiveMap;
        std::string occlusionMap;
    };

    void WriteModelManifest(const fs::path& path, const ModelPaths& paths)
    {
        std::string animation;
        if (!paths.clip.empty())
        {
            animation = "\n  \"animations\": [{\"name\": \"Walk\", \"clip\": \"" +
                        paths.clip + "\"}],";
        }
        std::string morph;
        if (!paths.morphTargets.empty())
        {
            morph = ",\n      \"morphTargets\": \"" + paths.morphTargets + "\"";
        }
        std::string material;
        const auto appendField = [&](const char* name, const std::string& value)
        {
            if (!value.empty())
            {
                material += ",\n      \"" + std::string(name) + "\": \"" + value + "\"";
            }
        };
        appendField("texture", paths.texture);
        appendField("texture2", paths.texture2);
        appendField("normalMap", paths.normalMap);
        appendField("metallicRoughnessMap", paths.metallicRoughnessMap);
        appendField("emissiveMap", paths.emissiveMap);
        appendField("occlusionMap", paths.occlusionMap);
        WriteText(path,
                  "{\n  \"cnjVersion\": 1,\n  \"type\": \"Model\",\n  \"skeleton\": \"" +
                      paths.skeleton + "\"," + animation +
                      "\n  \"meshes\": [{\n      \"name\": \"Triangle\",\n      \"vertices\": \"" +
                      paths.vertices + "\",\n      \"indices\": \"" + paths.indices +
                      "\",\n      \"vertexStride\": 32,\n      \"effect\": \"" + paths.effect + "\"" +
                      morph + material + "\n  }]\n}\n");
    }

    struct SkinnedModelPaths
    {
        std::string skeleton = "safe.skeleton.bin";
        std::string vertices = "safe.verts.bin";
        std::string indices = "safe.idx.bin";
        std::string texture;
        std::string clip;
    };

    void WriteSkinnedModelManifest(const fs::path& path, const SkinnedModelPaths& paths)
    {
        std::string texture;
        if (!paths.texture.empty())
        {
            texture = ", \"texture\": \"" + paths.texture + "\"";
        }
        std::string animation;
        if (!paths.clip.empty())
        {
            animation = ",\n  \"animations\": [{\"name\": \"Walk\", \"clip\": \"" +
                        paths.clip + "\"}]";
        }
        WriteText(path,
                  "{\n  \"skeleton\": \"" + paths.skeleton +
                      "\",\n  \"parts\": [{\"name\": \"Triangle\", \"vertices\": \"" +
                      paths.vertices + "\", \"indices\": \"" + paths.indices +
                      "\", \"vertexStride\": 52" + texture + "}]" + animation + "\n}\n");
    }

    std::string RequireContainmentRejection(const std::function<void()>& action,
                                             const std::string& selectedOutsidePath)
    {
        try
        {
            action();
            ADD_FAILURE() << "outside payload was selected: " << selectedOutsidePath;
        }
        catch (const ContentLoadException& ex)
        {
            return ex.what();
        }
        catch (const std::exception& ex)
        {
            ADD_FAILURE() << "wrong exception type: " << ex.what();
        }
        return {};
    }
}

class ContentPathContainmentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ContentTypeReaderManager::ClearTypeCreators();
        CNA::Internal::Xnb::RegisterSongXnbReader();
        CNA::Internal::Xnb::RegisterVideoXnbReader();
    }

    void TearDown() override
    {
        ContentTypeReaderManager::ClearTypeCreators();
    }

    GraphicsDevice gd;
};

TEST_F(ContentPathContainmentTest, SongXnbDeepTraversalRejectsBeforeCreationAndDoesNotPoisonCache)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret..mix.ogg";
    WriteText(outside, "SONG_OUTSIDE_SENTINEL");
    const fs::path xnb = tree.root() / "music" / "album" / "escape.xnb";
    WriteBytes(xnb, BuildSongXnb("../../../content-evil/secret..mix.ogg"));

    ContentManager cm(nullptr, tree.root().string());
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const std::string message = RequireContainmentRejection(
            [&] {
                const Song selected = cm.Load<Song>("music/album/escape");
                ADD_FAILURE() << "SongReader returned outside handle " << selected.getHandle();
            },
            outside.string());
        EXPECT_NE(message.find("SongReader"), std::string::npos) << message;
        EXPECT_EQ(message.find("SONG_OUTSIDE_SENTINEL"), std::string::npos) << message;
    }

    const fs::path valid = tree.root() / "music" / "album" / "track..mix.ogg";
    WriteText(valid, "SONG_INSIDE_SENTINEL");
    WriteBytes(xnb, BuildSongXnb("./media/../track..mix.ogg"));
    const Song song = cm.Load<Song>("music/album/escape");
    EXPECT_EQ(fs::path(song.getHandle()).lexically_normal(), valid.lexically_normal());
    EXPECT_EQ(ReadText(outside), "SONG_OUTSIDE_SENTINEL");
}

TEST_F(ContentPathContainmentTest, SongXnbAbsoluteEmbeddedPathIsRejected)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "absolute.ogg";
    WriteText(outside, "ABSOLUTE_SONG_SENTINEL");
    WriteBytes(tree.root() / "absolute_song.xnb", BuildSongXnb(outside.string()));

    ContentManager cm(nullptr, tree.root().string());
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<Song>("absolute_song"); }, outside.string());
    EXPECT_NE(message.find("SongReader"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, SongExtensionProbeCannotSelectOutsideSymlink)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "probe.ogg";
    WriteText(outside, "SONG_PROBE_OUTSIDE_SENTINEL");
    fs::create_directories(tree.root() / "music");
    std::error_code ec;
    fs::create_symlink(outside, tree.root() / "music" / "probe.ogg", ec);
    if (ec)
    {
        GTEST_SKIP() << "symlink creation not permitted in this environment";
    }
    WriteBytes(tree.root() / "music" / "probe_asset.xnb", BuildSongXnb("probe.wav"));

    ContentManager cm(nullptr, tree.root().string());
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<Song>("music/probe_asset"); }, outside.string());
    EXPECT_NE(message.find("SongReader"), std::string::npos) << message;
    EXPECT_EQ(message.find("SONG_PROBE_OUTSIDE_SENTINEL"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, ExplicitExternalSongXnbRemainsConfinedAndLoadable)
{
    ScratchTree tree;
    const fs::path bundle = tree.sibling() / "song_bundle";
    WriteText(bundle / "song.ogg", "EXTERNAL_SONG_BUNDLE_SENTINEL");
    WriteBytes(bundle / "song_asset.xnb", BuildSongXnb("audio/../song.ogg"));

    ContentManager cm(nullptr, tree.root().string());
    const Song song = cm.Load<Song>((bundle / "song_asset").string());
    EXPECT_EQ(fs::path(song.getHandle()).lexically_normal(),
              (bundle / "song.ogg").lexically_normal());
}

TEST_F(ContentPathContainmentTest, VideoXnbTraversalRejectsBeforeCreationAndDoesNotPoisonCache)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret.mkv";
    WriteText(outside, "VIDEO_OUTSIDE_SENTINEL");
    const fs::path xnb = tree.root() / "movies" / "set" / "escape.xnb";
    WriteBytes(xnb, BuildVideoXnb("../../../content-evil/secret.mkv"));

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const std::string message = RequireContainmentRejection(
            [&] {
                const Video selected = cm.Load<Video>("movies/set/escape");
                ADD_FAILURE() << "VideoReader returned outside filename "
                              << selected.getFileNameProperty();
            },
            outside.string());
        EXPECT_NE(message.find("VideoReader"), std::string::npos) << message;
        EXPECT_EQ(message.find("VIDEO_OUTSIDE_SENTINEL"), std::string::npos) << message;
    }

    const fs::path valid = tree.root() / "movies" / "set" / "inside..cut.mkv";
    WriteText(valid, "VIDEO_INSIDE_SENTINEL");
    WriteBytes(xnb, BuildVideoXnb("./clips/../inside..cut.mkv"));
    const Video video = cm.Load<Video>("movies/set/escape");
    EXPECT_EQ(fs::path(video.getFileNameProperty()).lexically_normal(), valid.lexically_normal());
    EXPECT_EQ(ReadText(outside), "VIDEO_OUTSIDE_SENTINEL");
}

TEST_F(ContentPathContainmentTest, VideoXnbAbsoluteEmbeddedPathIsRejected)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "absolute.mkv";
    WriteText(outside, "ABSOLUTE_VIDEO_SENTINEL");
    WriteBytes(tree.root() / "absolute_video.xnb", BuildVideoXnb(outside.string()));

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<Video>("absolute_video"); }, outside.string());
    EXPECT_NE(message.find("VideoReader"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, VideoExtensionProbeCannotSelectOutsideSymlink)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "probe.ogv";
    WriteText(outside, "VIDEO_PROBE_OUTSIDE_SENTINEL");
    fs::create_directories(tree.root() / "movies");
    std::error_code ec;
    fs::create_symlink(outside, tree.root() / "movies" / "probe.ogv", ec);
    if (ec)
    {
        GTEST_SKIP() << "symlink creation not permitted in this environment";
    }
    WriteBytes(tree.root() / "movies" / "probe_asset.xnb", BuildVideoXnb("probe.wmv"));

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<Video>("movies/probe_asset"); }, outside.string());
    EXPECT_NE(message.find("VideoReader"), std::string::npos) << message;
    EXPECT_EQ(message.find("VIDEO_PROBE_OUTSIDE_SENTINEL"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, ExplicitExternalVideoXnbRemainsConfinedAndLoadable)
{
    ScratchTree tree;
    const fs::path bundle = tree.sibling() / "video_bundle";
    WriteText(bundle / "video.ogv", "EXTERNAL_VIDEO_BUNDLE_SENTINEL");
    WriteBytes(bundle / "video_asset.xnb", BuildVideoXnb("clips/../video.ogv"));

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const Video video = cm.Load<Video>((bundle / "video_asset").string());
    EXPECT_EQ(fs::path(video.getFileNameProperty()).lexically_normal(),
              (bundle / "video.ogv").lexically_normal());
}

TEST_F(ContentPathContainmentTest, Texture3DDataJsonUnicodeTraversalIsRejectedBeforeRead)
{
    ScratchTree tree;
    WriteBytes(tree.sibling() / "volume.bin", {1, 2, 3, 255});
    WriteText(tree.root() / "volume.cnj", R"({
        "cnjVersion": 1,
        "type": "Texture3D",
        "width": 1,
        "height": 1,
        "depth": 1,
        "data": "\u002e\u002e\u002fcontent-evil/volume.bin"
    })");

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<std::shared_ptr<Texture3D>>("volume"); },
        (tree.sibling() / "volume.bin").string());
    EXPECT_NE(message.find("data"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, EffectVertexTraversalIsRejectedBeforeShaderRead)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret.vert.glsl";
    WriteText(outside, std::string("// SECRET_VERTEX_SHADER_MARKER\n") + kVertexShader);
    WriteText(tree.root() / "safe.frag.glsl", kFragmentShader);
    WriteText(tree.root() / "effect.cnj", R"({
        "cnjVersion": 1,
        "type": "Effect",
        "vertex": "../content-evil/secret.vert.glsl",
        "fragment": "safe.frag.glsl"
    })");

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<std::shared_ptr<Effect>>("effect"); }, outside.string());
    EXPECT_NE(message.find("vertex"), std::string::npos) << message;
    EXPECT_EQ(message.find("SECRET_VERTEX_SHADER_MARKER"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, EffectFragmentAbsolutePathIsRejectedBeforeShaderRead)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret.frag.glsl";
    WriteText(outside, std::string("// SECRET_FRAGMENT_SHADER_MARKER\n") + kFragmentShader);
    WriteText(tree.root() / "safe.vert.glsl", kVertexShader);
    WriteText(tree.root() / "effect.cnj",
              "{\n  \"cnjVersion\": 1,\n  \"type\": \"Effect\",\n"
              "  \"vertex\": \"safe.vert.glsl\",\n  \"fragment\": \"" +
                  outside.generic_string() + "\"\n}\n");

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<std::shared_ptr<Effect>>("effect"); }, outside.string());
    EXPECT_NE(message.find("fragment"), std::string::npos) << message;
    EXPECT_EQ(message.find("SECRET_FRAGMENT_SHADER_MARKER"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, IndirectContentAssetReferencesCannotEscapeContentRoot)
{
    ScratchTree tree;
    const fs::path outsideTexture = tree.sibling() / "outside.png";
    WriteTinyPng(gd, outsideTexture);
    WriteText(tree.sibling() / "custom_effect.cnj",
              R"({"cnjVersion":1,"type":"BasicEffect","marker":"INDIRECT_OUTSIDE_SENTINEL"})");

    struct EffectCase
    {
        const char* asset;
        const char* type;
        const char* field;
    };
    for (const EffectCase& testCase : {
             EffectCase{"stock_texture", "BasicEffect", "texture"},
             EffectCase{"stock_texture2", "DualTextureEffect", "texture2"},
             EffectCase{"stock_environment", "EnvironmentMapEffect", "environmentMap"},
         })
    {
        WriteText(tree.root() / (std::string(testCase.asset) + ".cnj"),
                  "{\"cnjVersion\":1,\"type\":\"" + std::string(testCase.type) +
                      "\",\"" + testCase.field +
                      "\":\"../content-evil/outside.png\"}");
        ContentManager cm(nullptr, tree.root().string());
        cm.setGraphicsDevice(gd);
        const std::string message = RequireContainmentRejection(
            [&] { (void)cm.Load<std::shared_ptr<Effect>>(testCase.asset); },
            outsideTexture.string());
        EXPECT_NE(message.find(testCase.field), std::string::npos) << message;
        EXPECT_EQ(message.find("INDIRECT_OUTSIDE_SENTINEL"), std::string::npos) << message;
    }

    WriteText(tree.root() / "font.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "texture": "../content-evil/outside.png",
        "lineSpacing": 1,
        "spacing": 0,
        "glyphs": [
            {"char": 65, "source": [0,0,1,1], "crop": [0,0,1,1], "kerning": [0,1,0]}
        ]
    })");
    {
        ContentManager cm(nullptr, tree.root().string());
        cm.setGraphicsDevice(gd);
        const std::string message = RequireContainmentRejection(
            [&] { (void)cm.Load<SpriteFont>("font"); }, outsideTexture.string());
        EXPECT_NE(message.find("texture"), std::string::npos) << message;
    }

    WriteZeroBoneSkeleton(tree.root() / "safe.skeleton.bin");
    WriteTriangle(tree.root() / "safe.verts.bin", tree.root() / "safe.idx.bin", 32);

    struct ModelCase
    {
        std::string asset;
        std::string field;
        ModelPaths paths;
        fs::path outside;
    };
    std::vector<ModelCase> modelCases;

    ModelPaths customEffect;
    customEffect.effect = "../content-evil/custom_effect";
    modelCases.push_back({"indirect_effect", "effect", customEffect,
                          tree.sibling() / "custom_effect.cnj"});

    ModelPaths texture;
    texture.texture = "../content-evil/outside.png";
    modelCases.push_back({"indirect_texture", "texture", texture, outsideTexture});

    ModelPaths texture2;
    texture2.effect = "DualTextureEffect";
    texture2.texture2 = "../content-evil/outside.png";
    modelCases.push_back({"indirect_texture2", "texture2", texture2, outsideTexture});

    for (const char* field : {"normalMap", "metallicRoughnessMap", "emissiveMap", "occlusionMap"})
    {
        ModelPaths map;
        map.effect = "PbrEffect";
        if (std::string(field) == "normalMap") map.normalMap = "../content-evil/outside.png";
        if (std::string(field) == "metallicRoughnessMap")
            map.metallicRoughnessMap = "../content-evil/outside.png";
        if (std::string(field) == "emissiveMap") map.emissiveMap = "../content-evil/outside.png";
        if (std::string(field) == "occlusionMap") map.occlusionMap = "../content-evil/outside.png";
        modelCases.push_back({"indirect_" + std::string(field), field, map, outsideTexture});
    }

    // Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline rejects it
    // before the path-containment checks below are ever reached. The Effect/SpriteFont
    // containment checks above this point are unaffected (neither needs a VertexBuffer) and have
    // already run and recorded their own results, so only this trailing Model section is skipped.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    for (const ModelCase& testCase : modelCases)
    {
        WriteModelManifest(
            tree.root() / (std::string(testCase.asset) + ".cnj"), testCase.paths);
        ContentManager cm(nullptr, tree.root().string());
        cm.setGraphicsDevice(gd);
        const std::string message = RequireContainmentRejection(
            [&] { (void)cm.Load<Model>(testCase.asset); }, testCase.outside.string());
        EXPECT_NE(message.find(testCase.field), std::string::npos) << message;
        EXPECT_EQ(message.find("INDIRECT_OUTSIDE_SENTINEL"), std::string::npos) << message;
    }
}

TEST_F(ContentPathContainmentTest, AnimationClipTraversalRejectsDeterministicallyWithoutCachePoisoning)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret.clip.bin";
    WriteClip(outside, 91.25);
    WriteText(tree.root() / "walk.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "clipFile": "../content-evil/secret.clip.bin"
    })");

    ContentManager cm(nullptr, tree.root().string());
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const std::string message = RequireContainmentRejection(
            [&] {
                const AnimationClipEXT selected = cm.Load<AnimationClipEXT>("walk");
                ADD_FAILURE() << "outside clip duration was selected: "
                              << selected.Duration.getTotalSecondsProperty();
            },
            outside.string());
        EXPECT_NE(message.find("clipFile"), std::string::npos) << message;
    }

    WriteClip(tree.root() / "inside.clip.bin", 1.5);
    WriteText(tree.root() / "walk.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "clipFile": "clips/../inside.clip.bin"
    })");
    const AnimationClipEXT valid = cm.Load<AnimationClipEXT>("walk");
    EXPECT_DOUBLE_EQ(valid.Duration.getTotalSecondsProperty(), 1.5);
}

TEST_F(ContentPathContainmentTest, AnimationClipAbsoluteAndUnicodeTraversalPathsAreRejected)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret.clip.bin";
    WriteClip(outside, 42.0);
    WriteText(tree.root() / "absolute.cnj",
              "{\"cnjVersion\":1,\"type\":\"AnimationClip\",\"clipFile\":\"" +
                  outside.generic_string() + "\"}");
    WriteText(tree.root() / "encoded.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "clipFile": "\u002e\u002e\u002fcontent-evil/secret.clip.bin"
    })");

    ContentManager cm(nullptr, tree.root().string());
    for (const std::string& asset : {"absolute", "encoded"})
    {
        const std::string message = RequireContainmentRejection(
            [&] { (void)cm.Load<AnimationClipEXT>(asset); }, outside.string());
        EXPECT_NE(message.find("clipFile"), std::string::npos) << message;
    }
}

TEST_F(ContentPathContainmentTest, AnimationClipSymlinkEscapeIsRejected)
{
    ScratchTree tree;
    const fs::path outside = tree.sibling() / "secret.clip.bin";
    WriteClip(outside, 73.0);
    std::error_code ec;
    fs::create_directory_symlink(tree.sibling(), tree.root() / "escape", ec);
    if (ec)
    {
        GTEST_SKIP() << "symlink creation not permitted in this environment";
    }
    WriteText(tree.root() / "linked.cnj", R"({
        "cnjVersion": 1,
        "type": "AnimationClip",
        "clipFile": "escape/secret.clip.bin"
    })");

    ContentManager cm(nullptr, tree.root().string());
    const std::string message = RequireContainmentRejection(
        [&] { (void)cm.Load<AnimationClipEXT>("linked"); }, outside.string());
    EXPECT_NE(message.find("clipFile"), std::string::npos) << message;
}

TEST_F(ContentPathContainmentTest, ModelRootRelativeBinaryFieldsCannotEscapeContentRoot)
{
    ScratchTree tree;
    WriteZeroBoneSkeleton(tree.root() / "safe.skeleton.bin");
    WriteTriangle(tree.root() / "safe.verts.bin", tree.root() / "safe.idx.bin", 32);
    WriteEmptyMorphTargets(tree.root() / "safe.morph.bin");
    WriteClip(tree.root() / "safe.clip.bin", 1.0);

    WriteZeroBoneSkeleton(tree.sibling() / "outside.skeleton.bin");
    WriteTriangle(tree.sibling() / "outside.verts.bin", tree.sibling() / "outside.idx.bin", 32);
    WriteEmptyMorphTargets(tree.sibling() / "outside.morph.bin");
    WriteClip(tree.sibling() / "outside.clip.bin", 99.0);

    struct Case
    {
        const char* asset;
        const char* field;
        ModelPaths paths;
        fs::path outside;
    };
    std::vector<Case> cases;

    ModelPaths skeleton;
    skeleton.skeleton = "../content-evil/outside.skeleton.bin";
    cases.push_back({"escape_skeleton", "skeleton", skeleton,
                     tree.sibling() / "outside.skeleton.bin"});

    ModelPaths vertices;
    vertices.vertices = "../content-evil/outside.verts.bin";
    cases.push_back({"escape_vertices", "vertices", vertices,
                     tree.sibling() / "outside.verts.bin"});

    ModelPaths indices;
    indices.indices = (tree.sibling() / "outside.idx.bin").generic_string();
    cases.push_back({"escape_indices", "indices", indices,
                     tree.sibling() / "outside.idx.bin"});

    ModelPaths morph;
    morph.morphTargets = "../content-evil/outside.morph.bin";
    cases.push_back({"escape_morph", "morphTargets", morph,
                     tree.sibling() / "outside.morph.bin"});

    ModelPaths clip;
    clip.clip = "../content-evil/outside.clip.bin";
    cases.push_back({"escape_clip", "clip", clip,
                     tree.sibling() / "outside.clip.bin"});

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    for (const Case& testCase : cases)
    {
        WriteModelManifest(tree.root() / (std::string(testCase.asset) + ".cnj"), testCase.paths);
        const std::string message = RequireContainmentRejection(
            [&] { (void)cm.Load<Model>(testCase.asset); }, testCase.outside.string());
        EXPECT_NE(message.find(testCase.field), std::string::npos) << message;
    }
}

TEST_F(ContentPathContainmentTest, ModelNormalizedInRootBinaryFieldsStillLoad)
{
    // Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline rejects it
    // before the path-normalization this test actually exercises is ever reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ScratchTree tree;
    fs::create_directories(tree.root() / "nested");
    WriteOneBoneSkeleton(tree.root() / "safe.skeleton.bin");
    WriteTriangle(tree.root() / "safe.verts.bin", tree.root() / "safe.idx.bin", 32);
    WriteEmptyMorphTargets(tree.root() / "safe.morph.bin");
    WriteClip(tree.root() / "safe.clip.bin", 3.25);

    ModelPaths paths;
    paths.skeleton = "nested/../safe.skeleton.bin";
    paths.vertices = "nested/../safe.verts.bin";
    paths.indices = "nested/../safe.idx.bin";
    paths.morphTargets = "nested/../safe.morph.bin";
    paths.clip = "nested/../safe.clip.bin";
    WriteModelManifest(tree.root() / "valid.cnj", paths);

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    const Model model = cm.Load<Model>("valid");
    EXPECT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    const auto* skinning = static_cast<const Microsoft::Xna::Framework::Graphics::SkinningData*>(
        model.getTagProperty());
    ASSERT_NE(skinning, nullptr);
    ASSERT_TRUE(skinning->AnimationClips.count("Walk"));
    EXPECT_DOUBLE_EQ(skinning->AnimationClips.at("Walk").Duration.getTotalSecondsProperty(), 3.25);
}

TEST_F(ContentPathContainmentTest, SkinnedModelManifestFieldsCannotEscapeContentRoot)
{
    // SkinnedModelEXT loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before the path-containment checks this test actually exercises are ever
    // reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ScratchTree tree;
    const fs::path bundle = tree.root() / "avatar";
    WriteZeroBoneSkeleton(bundle / "safe.skeleton.bin");
    WriteTriangle(bundle / "safe.verts.bin", bundle / "safe.idx.bin", 52);
    WriteTinyPng(gd, bundle / "safe.png");
    WriteClip(bundle / "safe.clip.bin", 1.0);

    WriteZeroBoneSkeleton(tree.sibling() / "outside.skeleton.bin");
    WriteTriangle(tree.sibling() / "outside.verts.bin", tree.sibling() / "outside.idx.bin", 52);
    WriteTinyPng(gd, tree.sibling() / "outside.png");
    WriteClip(tree.sibling() / "outside.clip.bin", 88.0);

    struct Case
    {
        const char* asset;
        const char* field;
        SkinnedModelPaths paths;
        fs::path outside;
    };
    std::vector<Case> cases;
    const std::string escapePrefix = "../../content-evil/";

    SkinnedModelPaths skeleton;
    skeleton.skeleton = escapePrefix + "outside.skeleton.bin";
    cases.push_back({"avatar/escape_skeleton", "skeleton", skeleton,
                     tree.sibling() / "outside.skeleton.bin"});

    SkinnedModelPaths vertices;
    vertices.vertices = escapePrefix + "outside.verts.bin";
    cases.push_back({"avatar/escape_vertices", "vertices", vertices,
                     tree.sibling() / "outside.verts.bin"});

    SkinnedModelPaths indices;
    indices.indices = (tree.sibling() / "outside.idx.bin").generic_string();
    cases.push_back({"avatar/escape_indices", "indices", indices,
                     tree.sibling() / "outside.idx.bin"});

    SkinnedModelPaths texture;
    texture.texture = escapePrefix + "outside.png";
    cases.push_back({"avatar/escape_texture", "texture", texture,
                     tree.sibling() / "outside.png"});

    SkinnedModelPaths clip;
    clip.clip = escapePrefix + "outside.clip.bin";
    cases.push_back({"avatar/escape_clip", "clip", clip,
                     tree.sibling() / "outside.clip.bin"});

    ContentManager cm(nullptr, tree.root().string());
    cm.setGraphicsDevice(gd);
    for (const Case& testCase : cases)
    {
        WriteSkinnedModelManifest(
            tree.root() / (std::string(testCase.asset) + ".skinnedmodel.json"), testCase.paths);
        const std::string message = RequireContainmentRejection(
            [&] { (void)cm.Load<std::shared_ptr<SkinnedModelEXT>>(testCase.asset); },
            testCase.outside.string());
        EXPECT_NE(message.find(testCase.field), std::string::npos) << message;
    }
}
