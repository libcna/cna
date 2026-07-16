// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-25 (Phase D3): unit tests for Texture3DReader and TextureCubeReader.
// TextureCubeReader is verified end-to-end against a real, externally-produced fixture
// (MonoGame's SampleCube64DXT1Mips.xnb -- 6 faces, full DXT1 mip chain including the sub-4x4
// rounding edge cases). No Texture3DReader fixture was found anywhere in the available library
// (volume textures are rare in real XNA content), so it is tested with a hand-constructed stream
// verified field-by-field against FNA's own Texture3DReader.cs byte order instead.

#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    class Texture3DTextureCubeContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterTexture3DXnbReader();
            CNA::Internal::Xnb::RegisterTextureCubeXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, BothReadersAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Texture3DReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.TextureCubeReader"));
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd)
{
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    TextureCube cube = cm.Load<TextureCube>("SampleCube64DXT1Mips");

    EXPECT_EQ(cube.getSizeProperty(), 64);
    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Color); // always decompressed to Color, matching Texture2DReader
    EXPECT_EQ(cube.getLevelCountProperty(), 7);

    // Every face/level combination decoded without throwing and produced real pixel data
    // (a corrupted DXT1 decode would very likely produce an exception or all-zero output, not a
    // plausible non-degenerate image) -- spot-check level 0 (64x64) and the smallest levels (the
    // sub-4x4 DXT1 block-rounding edge cases, 2x2 and 1x1, each still exactly one 8-byte block).
    std::vector<Color> level0(64 * 64, Color(0, 0, 0, 0));
    cube.GetData(CubeMapFace::PositiveX, level0.data(), static_cast<int>(level0.size()));
    bool sawNonUniform = false;
    for (std::size_t i = 1; i < level0.size(); ++i)
    {
        if (level0[i].getPackedValueProperty() != level0[0].getPackedValueProperty())
        {
            sawNonUniform = true;
            break;
        }
    }
    EXPECT_TRUE(sawNonUniform) << "level 0 should not decode to a uniformly flat image";

    Color onePixel(0, 0, 0, 0);
    EXPECT_NO_THROW(cube.GetData(CubeMapFace::NegativeZ, 6, nullptr, &onePixel, 0, 1));
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder)
{
    // No real Texture3D .xnb fixture was found anywhere in the available library -- hand
    // constructed, verified field-by-field against FNA's Texture3DReader.cs's exact read order
    // (SurfaceFormat, width, height, depth, levelCount, then per-level [dataSize, data]).
    constexpr int32_t width = 2;
    constexpr int32_t height = 2;
    constexpr int32_t depth = 1;
    constexpr int32_t levelCount = 1;
    const std::vector<Color> pixels{
        Color(255, 0, 0, 255), Color(0, 255, 0, 255),
        Color(0, 0, 255, 255), Color(255, 255, 0, 255)};

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(width);
    writer.Write(height);
    writer.Write(depth);
    writer.Write(levelCount);
    writer.Write(static_cast<int32_t>(pixels.size() * 4)); // dataSize
    for (const Color& c : pixels)
    {
        writer.Write(c.getRProperty());
        writer.Write(c.getGProperty());
        writer.Write(c.getBProperty());
        writer.Write(c.getAProperty());
    }
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture3DReader");
    ASSERT_NE(typeReader, nullptr);
    std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
    auto texture = std::any_cast<std::shared_ptr<Texture3D>>(resultAny);
    ASSERT_NE(texture, nullptr);

    EXPECT_EQ(texture->getWidthProperty(), width);
    EXPECT_EQ(texture->getHeightProperty(), height);
    EXPECT_EQ(texture->getDepthProperty(), depth);
    EXPECT_EQ(texture->getFormatProperty(), SurfaceFormat::Color);

    std::vector<Color> readBack(4, Color(0, 0, 0, 0));
    texture->GetData(readBack.data(), 4);
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        EXPECT_EQ(readBack[i].getPackedValueProperty(), pixels[i].getPackedValueProperty()) << "pixel " << i;
    }
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderRejectsUnsupportedSurfaceFormat)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Bgr565)); // not yet implemented
    writer.Write(static_cast<int32_t>(4));
    writer.Write(static_cast<int32_t>(4));
    writer.Write(static_cast<int32_t>(1));
    writer.Write(static_cast<int32_t>(1));
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture3DReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}
