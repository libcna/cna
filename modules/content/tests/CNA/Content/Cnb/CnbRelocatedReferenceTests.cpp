// SPDX-License-Identifier: MS-PL
//
// CNA-DEMOS-ISSUE-020: a compiled asset's external references follow the asset when its content
// build sits below the ContentManager's root. XNA resolves a reference from the referring asset's
// own directory (ContentReader.GetPathToReference); a .cnb names it relative to the root it was
// compiled against and records its own name there (CMET contentName), so the loaded name minus
// that compiled name is where the build sits.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"

namespace Cnb = CNA::Content::Cnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::Video;

namespace
{
    class ScratchRoot
    {
    public:
        ScratchRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_relocated_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchRoot()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    void Touch(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << "x";
    }

    void WriteSong(const std::filesystem::path& path, const std::string& contentName)
    {
        Cnb::CnbSongData song;
        song.streamReference = "music/theme.ogg";
        song.durationMs = 185000u;
        WriteBytes(path, Cnb::EncodeSongToCnb(song, contentName));
    }

    void SavePng(GraphicsDevice& device, const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        Microsoft::Xna::Framework::Graphics::Texture2D texture(device, 1, 1);
        const Microsoft::Xna::Framework::Color pixel(17, 34, 51, 255);
        texture.SetData(&pixel, 1);
        texture.SaveAsPng(path.string());
    }

    const Microsoft::Xna::Framework::Graphics::Texture2D* FirstPartTexture(const Model& model)
    {
        const auto* mesh = model.getMeshesProperty()[0];
        const auto* effect = dynamic_cast<const Microsoft::Xna::Framework::Graphics::BasicEffect*>(
            mesh->getMeshPartsProperty()[0]->getEffectProperty());
        return effect != nullptr ? effect->getTextureProperty() : nullptr;
    }
}

TEST(CnbRelocatedReferenceTest, ASongBelowTheManagerRootStreamsTheFileBesideIt)
{
    ScratchRoot root;
    WriteSong(root.Path() / "media" / "music" / "theme.cnb", "music/theme");
    Touch(root.Path() / "media" / "music" / "theme.ogg");

    ContentManager parent(nullptr, root.Path().string());
    const Song song = parent.Load<Song>("media/music/theme");
    EXPECT_NEAR(song.getDurationProperty().getTotalMillisecondsProperty(), 185000.0, 1.0);

    ContentManager literal(nullptr, root.Path().string());
    EXPECT_NO_THROW((void)literal.Load<Song>("media/music/theme.cnb"));

    ContentManager compiledRoot(nullptr, (root.Path() / "media").string());
    EXPECT_NO_THROW((void)compiledRoot.Load<Song>("music/theme"));
}

TEST(CnbRelocatedReferenceTest, AVideoBelowTheManagerRootStreamsTheFileBesideIt)
{
    ScratchRoot root;
    Cnb::CnbVideoData video;
    video.streamReference = "movies/intro.mp4";
    video.durationMs = 42000u;
    video.width = 64u;
    video.height = 32u;
    video.framesPerSecond = 30.0f;
    WriteBytes(root.Path() / "media" / "movies" / "intro.cnb",
               Cnb::EncodeVideoToCnb(video, "movies/intro"));
    Touch(root.Path() / "media" / "movies" / "intro.mp4");
    // A same-named file at the manager's root is not the asset's stream.
    Touch(root.Path() / "movies" / "intro.mp4");

    GraphicsDevice device;
    ContentManager parent(nullptr, root.Path().string());
    parent.setGraphicsDevice(device);
    const Video loaded = parent.Load<Video>("media/movies/intro");
    EXPECT_EQ(loaded.getWidthProperty(), 64);
    EXPECT_EQ(std::filesystem::path(loaded.getFileNameProperty()).lexically_normal(),
              (root.Path() / "media" / "movies" / "intro.mp4").lexically_normal());
}

// Without a compiled name that the loaded name ends in, nothing says where the build sits, so the
// reference stays relative to the manager's root as before.
TEST(CnbRelocatedReferenceTest, AnUnmatchedOrMissingCompiledNameKeepsTheRootRelativeReference)
{
    ScratchRoot root;
    WriteSong(root.Path() / "media" / "music" / "renamed.cnb", "music/theme");
    WriteSong(root.Path() / "media" / "music" / "anonymous.cnb", "");
    Touch(root.Path() / "music" / "theme.ogg");

    ContentManager cm(nullptr, root.Path().string());
    EXPECT_NO_THROW((void)cm.Load<Song>("media/music/renamed"));
    EXPECT_NO_THROW((void)cm.Load<Song>("media/music/anonymous"));
}

TEST(CnbRelocatedReferenceTest, AV2ModelBelowTheManagerRootLoadsItsTextureBesideIt)
{
    ScratchRoot root;
    GraphicsDevice device;
    SavePng(device, root.Path() / "pack" / "textures" / "atlas.png");

    Cnb::CnbModelV2Data model;
    Cnb::CnbModelV2Bone bone;
    bone.name = "Root";
    model.bones = {bone};
    model.rootBone = 0u;
    Cnb::CnbModelV2VertexDeclaration declaration;
    declaration.vertexStride = 12u;
    declaration.elements = {{0u, Cnb::CnbModelV2VertexFormat::Vector3,
                             Cnb::CnbModelV2VertexUsage::Position, 0u}};
    model.vertexDeclarations = {declaration};
    Cnb::CnbModelV2VertexBuffer vertices;
    vertices.declaration = 0u;
    vertices.vertexCount = 3u;
    vertices.bytes.assign(36u, 0u);
    model.vertexBuffers = {vertices};
    model.indexBuffers = {{2u, 3u, {0u, 0u, 1u, 0u, 2u, 0u}}};
    Cnb::CnbModelV2Effect effect;
    effect.kind = Cnb::CnbModelV2EffectKind::BasicEffect;
    effect.alpha = 1.0f;
    effect.primaryTexture = "textures/atlas";
    model.effects = {effect};
    Cnb::CnbModelV2Mesh mesh;
    mesh.name = "Crate";
    mesh.parentBone = 0;
    mesh.parts = {{0u, 3u, 0u, 1u, 0u, 0u, 0u}};
    model.meshes = {mesh};
    WriteBytes(root.Path() / "pack" / "models" / "crate.cnb",
               Cnb::EncodeModelV2ToCnb(model, "models/crate"));

    ContentManager cm(nullptr, root.Path().string());
    cm.setGraphicsDevice(device);
    const Model loaded = cm.Load<Model>("pack/models/crate");
    EXPECT_NE(FirstPartTexture(loaded), nullptr);
}

TEST(CnbRelocatedReferenceTest, AV1ModelBelowTheManagerRootLoadsItsTextureBesideIt)
{
    ScratchRoot root;
    GraphicsDevice device;
    SavePng(device, root.Path() / "pack" / "textures" / "wall.png");

    Cnb::CnbModelPart part;
    part.name = "Only";
    part.vertexStride = 16u;
    part.vertexCount = 3u;
    part.indexCount = 3u;
    part.indexElementSize = 2u;
    part.primitiveTopology = 4u;
    part.primitiveCount = 1u;
    part.vertexBytes.assign(48u, 0u);
    part.indexBytes = {0u, 0u, 1u, 0u, 2u, 0u};
    part.material.baseColorTexture = "textures/wall";
    Cnb::CnbModelData model;
    model.parts = {part};
    model.meshes = {Cnb::CnbModelMesh{"Only", -1, {0u}}};
    WriteBytes(root.Path() / "pack" / "models" / "wall.cnb",
               Cnb::EncodeModelToCnb(model, "models/wall"));

    ContentManager cm(nullptr, root.Path().string());
    cm.setGraphicsDevice(device);
    EXPECT_NO_THROW((void)cm.Load<Model>("pack/models/wall"));
}
