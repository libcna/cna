// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-101A/B/C: the texture schemas as seen from the outside.
//
// CnbTextureCodecTests covers bytes-in/bytes-out. This file covers the half that needs a real
// GraphicsDevice: that a texture .cnb actually reaches ContentManager::Load<T>(), produces a
// texture with the right dimensions, and hands back the pixels that went in. A codec round trip
// cannot show any of that -- it never touches the registry, the resolution order, or a GPU object.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

using CNA::Content::Cnb::CnbTextureData;
using CNA::Content::Cnb::CnbTextureFormat;
using CNA::Content::Cnb::CnbTextureRepresentation;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class ScratchRoot
    {
    public:
        ScratchRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_texture_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~ScratchRoot() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
        ScratchRoot(const ScratchRoot&) = delete;
        ScratchRoot& operator=(const ScratchRoot&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    /// A 2x2 image whose four texels are all different, so a transposed or shifted upload is
    /// visible rather than merely "some pixels came back".
    std::vector<std::uint8_t> FourDistinctTexels()
    {
        return {255u, 0u,   0u,   255u,
                0u,   255u, 0u,   200u,
                0u,   0u,   255u, 150u,
                17u,  34u,  51u,  68u};
    }
}

TEST(CnbTextureContentManagerTest, ATexture2DCnbLoadsThroughContentManagerWithItsPixelsIntact)
{
    ScratchRoot root;
    const CnbTextureData source =
        CNA::Content::Cnb::MakeRgba8Texture2DData(2u, 2u, FourDistinctTexels());
    WriteBytes(root.path() / "flat.cnb",
               CNA::Content::Cnb::EncodeTexture2DToCnb(source, "flat"));

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);

    // No per-type reader is registered for this ContentManager at all: a .cnb is self-describing,
    // and the file's own asset type identifier is what selects the loader.
    auto texture = cm.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("flat");
    EXPECT_EQ(texture.getWidthProperty(), 2);
    EXPECT_EQ(texture.getHeightProperty(), 2);
    EXPECT_EQ(texture.getLevelCountProperty(), 1);

    std::vector<Color> readBack(4);
    texture.GetData(readBack.data(), 4);
    const std::vector<std::uint8_t>& expected = source.representations[0].levels[0];
    for (std::size_t i = 0; i < readBack.size(); ++i)
    {
        EXPECT_EQ(readBack[i].getRProperty(), expected[i * 4u + 0u]) << "texel " << i;
        EXPECT_EQ(readBack[i].getGProperty(), expected[i * 4u + 1u]) << "texel " << i;
        EXPECT_EQ(readBack[i].getBProperty(), expected[i * 4u + 2u]) << "texel " << i;
        EXPECT_EQ(readBack[i].getAProperty(), expected[i * 4u + 3u]) << "texel " << i;
    }
}

TEST(CnbTextureContentManagerTest, AMippedTexture2DCnbProducesEveryDeclaredLevel)
{
    ScratchRoot root;
    CnbTextureData source;
    source.width = 4u;
    source.height = 4u;
    source.depth = 1u;
    source.faceCount = 1u;
    source.mipCount = 3u;
    CnbTextureRepresentation representation;
    representation.format = CnbTextureFormat::Rgba8;
    representation.levels.push_back(std::vector<std::uint8_t>(4u * 4u * 4u, 10u));
    representation.levels.push_back(std::vector<std::uint8_t>(2u * 2u * 4u, 20u));
    representation.levels.push_back(std::vector<std::uint8_t>(1u * 1u * 4u, 30u));
    source.representations.push_back(std::move(representation));
    WriteBytes(root.path() / "mips.cnb", CNA::Content::Cnb::EncodeTexture2DToCnb(source, "mips"));

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    auto texture = cm.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("mips");
    EXPECT_EQ(texture.getWidthProperty(), 4);
    EXPECT_EQ(texture.getLevelCountProperty(), 3);

    // The smallest level is the one a wrong mip-size computation gets wrong, so read that one.
    std::vector<Color> smallest(1);
    texture.GetData(2, nullptr, smallest.data(), 0, 1);
    EXPECT_EQ(smallest[0].getRProperty(), 30u);
}

TEST(CnbTextureContentManagerTest, ATextureCubeCnbLoadsAllSixFacesDistinctly)
{
    ScratchRoot root;
    CnbTextureData source;
    source.width = 2u;
    source.height = 2u;
    source.depth = 1u;
    source.faceCount = CNA::Content::Cnb::CnbTextureCubeFaceCount;
    source.mipCount = 1u;
    CnbTextureRepresentation representation;
    representation.format = CnbTextureFormat::Rgba8;
    for (std::uint32_t face = 0u; face < source.faceCount; ++face)
    {
        representation.levels.push_back(
            std::vector<std::uint8_t>(2u * 2u * 4u, static_cast<std::uint8_t>(face * 40u + 1u)));
    }
    source.representations.push_back(std::move(representation));
    WriteBytes(root.path() / "sky.cnb", CNA::Content::Cnb::EncodeTextureCubeToCnb(source, "sky"));

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    auto cube = cm.Load<Microsoft::Xna::Framework::Graphics::TextureCube>("sky");
    EXPECT_EQ(cube.getSizeProperty(), 2);

    for (std::uint32_t face = 0u; face < source.faceCount; ++face)
    {
        std::vector<Color> texels(4);
        cube.GetData(static_cast<Microsoft::Xna::Framework::Graphics::CubeMapFace>(face),
                     texels.data(), 4);
        EXPECT_EQ(texels[0].getRProperty(), static_cast<std::uint8_t>(face * 40u + 1u))
            << "face " << face << " did not keep its own pixels";
    }
}

TEST(CnbTextureContentManagerTest, ATexture3DCnbLoadsAsASharedPointerWithItsDepth)
{
    ScratchRoot root;
    CnbTextureData source;
    source.width = 2u;
    source.height = 2u;
    source.depth = 2u;
    source.faceCount = 1u;
    source.mipCount = 1u;
    CnbTextureRepresentation representation;
    representation.format = CnbTextureFormat::Rgba8;
    representation.levels.push_back(std::vector<std::uint8_t>(2u * 2u * 2u * 4u, 77u));
    source.representations.push_back(std::move(representation));
    WriteBytes(root.path() / "fog.cnb", CNA::Content::Cnb::EncodeTexture3DToCnb(source, "fog"));

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    // Texture3D is non-copyable, so ContentManager boxes it as a shared_ptr -- the same shape the
    // .xnb reader registers. Asking for the bare type is the mistake this pins.
    auto volume = cm.Load<std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D>>("fog");
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->getWidthProperty(), 2);
    EXPECT_EQ(volume->getHeightProperty(), 2);
    EXPECT_EQ(volume->getDepthProperty(), 2);
}

TEST(CnbTextureContentManagerTest, AFormatThisBuildCannotUploadIsRefusedByName)
{
    // The codec accepts any KNOWN format when reading -- it only checks that the payload sizes
    // match the dimensions -- so a Bc7 file is a perfectly valid container that this build simply
    // cannot upload. That refusal has to name the format, not fail somewhere inside SetData.
    //
    // The file is written with CnbWriter directly because the encoder deliberately refuses to
    // produce one (schema 1 writes Rgba8 alone); going through the writer keeps every checksum
    // and offset correct, which patching bytes in a finished file would not.
    ScratchRoot root;

    CNA::Content::Cnb::CnbByteWriter header;
    header.WriteU32(4u); header.WriteU32(4u); header.WriteU32(1u);
    header.WriteU32(1u); header.WriteU32(1u); header.WriteU32(1u);

    CNA::Content::Cnb::CnbByteWriter descriptors;
    descriptors.WriteU32(static_cast<std::uint32_t>(CnbTextureFormat::Bc7));
    descriptors.WriteU32(0u);
    descriptors.WriteU32(1u);
    descriptors.WriteU32(0u);
    descriptors.WriteU64(16u); // one 4x4 BC7 block

    CNA::Content::Cnb::CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Texture2D,
                                        CNA::Content::Cnb::CnbTextureSchemaVersion);
    writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Texture2D", "compressed");
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Header, header.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Representations, descriptors.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Payload,
                    std::vector<std::uint8_t>(16u, 0xABu),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 16u);
    const std::vector<std::uint8_t> bytes = writer.Build();
    WriteBytes(root.path() / "compressed.cnb", bytes);

    // The container half is fine: the codec reads it back without complaint.
    const CnbTextureData decoded = CNA::Content::Cnb::DecodeTexture2DFromCnb(
        CNA::Content::Cnb::CnbDocument::Parse(bytes, "compressed.cnb"));
    EXPECT_EQ(decoded.representations.at(0).format, CnbTextureFormat::Bc7);

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    try
    {
        (void)cm.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("compressed");
        FAIL() << "a format this build cannot upload must be refused";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("Bc7"), std::string::npos)
            << "the refusal must name the format it could not use: " << message;
    }
}

TEST(CnbTextureContentManagerTest, ARepresentationThisBuildCanUploadIsPreferredOverOneItCannot)
{
    // The selection is the point of the multi-representation layout, and this is the case that
    // proves it works before there is a second writer: a file offering Bc7 FIRST and Rgba8 second
    // loads on a build that can only upload Rgba8.
    ScratchRoot root;

    CNA::Content::Cnb::CnbByteWriter header;
    header.WriteU32(2u); header.WriteU32(2u); header.WriteU32(1u);
    header.WriteU32(1u); header.WriteU32(1u); header.WriteU32(2u);

    CNA::Content::Cnb::CnbByteWriter descriptors;
    descriptors.WriteU32(static_cast<std::uint32_t>(CnbTextureFormat::Bc7));
    descriptors.WriteU32(0u); descriptors.WriteU32(1u); descriptors.WriteU32(0u);
    descriptors.WriteU64(16u);
    descriptors.WriteU32(static_cast<std::uint32_t>(CnbTextureFormat::Rgba8));
    descriptors.WriteU32(1u); descriptors.WriteU32(1u); descriptors.WriteU32(0u);
    descriptors.WriteU64(16u);

    CNA::Content::Cnb::CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Texture2D,
                                        CNA::Content::Cnb::CnbTextureSchemaVersion);
    writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Texture2D", "both");
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Header, header.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Representations, descriptors.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Payload,
                    std::vector<std::uint8_t>(16u, 0xABu),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 16u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Payload,
                    std::vector<std::uint8_t>{9u, 8u, 7u, 6u, 9u, 8u, 7u, 6u,
                                              9u, 8u, 7u, 6u, 9u, 8u, 7u, 6u},
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 16u);
    WriteBytes(root.path() / "both.cnb", writer.Build());

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    auto texture = cm.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("both");
    EXPECT_EQ(texture.getWidthProperty(), 2);

    std::vector<Color> texels(4);
    texture.GetData(texels.data(), 4);
    // The Rgba8 payload, not the Bc7 one: proof the second representation was the one used.
    EXPECT_EQ(texels[0].getRProperty(), 9u);
    EXPECT_EQ(texels[0].getGProperty(), 8u);
    EXPECT_EQ(texels[0].getBProperty(), 7u);
    EXPECT_EQ(texels[0].getAProperty(), 6u);
}
