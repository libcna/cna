// SPDX-License-Identifier: MS-PL
//
// XNB-25 (Phase D3): unit tests for Texture3DReader and TextureCubeReader.
// TextureCubeReader is verified end-to-end against a real, externally-produced fixture
// (MonoGame's SampleCube64DXT1Mips.xnb -- 6 faces, full DXT1 mip chain including the sub-4x4
// rounding edge cases). No Texture3DReader fixture was found anywhere in the available library
// (volume textures are rare in real XNA content), so it is tested with a hand-constructed stream
// verified field-by-field against FNA's own Texture3DReader.cs byte order instead.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <limits>
#include <exception>
#include <gtest/gtest.h>
#include <stdexcept>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guard it replaced.
using namespace CNA::Testing::Renderers;
#include <sstream>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

// -----------------------------------------------------------------------
// Every renderer except STUB stores cube faces and reads every level back -- HEADLESS too, in
// CPU storage since d73f1aeab (2026-10-16). STUB creates no cube-map resource, so its
// TextureCube::SetData refuses with NotSupportedException, and the tests assert that instead.
[[nodiscard]] inline bool CubeStorageSupported()
{
    return !CNA_RENDERER_IS(Stub);
}

[[nodiscard]] inline bool CubeLevel0ReadbackSupported() { return CubeStorageSupported(); }

/// Readback above the base level is its own question: a renderer can store a whole declared chain
/// and still only be able to read level 0 back. Every current renderer that reads level 0 back
/// reads every level back, so the answer is the same set today.
[[nodiscard]] inline bool CubeMipReadbackSupported()
{
    return CubeLevel0ReadbackSupported();
}

/// Whether this renderer can fetch a volume texture's voxels back to the CPU. A renderer that owns
/// volume pixels without a readback route refuses GetData rather than fabricating voxels, and the
/// shared layer raises NotSupportedException; no current renderer is in that position.
[[nodiscard]] inline bool VolumeReadbackSupported()
{
    return true;
}

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

    std::shared_ptr<Texture3D> ReadHandConstructedVolume(
        GraphicsDevice& device, SurfaceFormat format, int32_t width, int32_t height,
        int32_t depth, const std::vector<std::vector<std::uint8_t>>& levels)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(width);
        writer.Write(height);
        writer.Write(depth);
        writer.Write(static_cast<int32_t>(levels.size()));
        for (const auto& bytes : levels)
        {
            writer.Write(static_cast<int32_t>(bytes.size()));
            for (const std::uint8_t value : bytes) writer.Write(value);
        }
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(device);
        System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture3DReader");
        if (!typeReader) return nullptr;
        return std::any_cast<std::shared_ptr<Texture3D>>(
            typeReader->ReadUntyped(reader, std::any{}));
    }

    TextureCube ReadHandConstructedCube(
        GraphicsDevice& device, SurfaceFormat format,
        const std::array<std::vector<std::uint8_t>, 6>& faces, int size = 1)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(static_cast<int32_t>(size));
        writer.Write(static_cast<int32_t>(1));
        for (const auto& bytes : faces)
        {
            writer.Write(static_cast<int32_t>(bytes.size()));
            for (const std::uint8_t value : bytes) writer.Write(value);
        }
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(device);
        System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.TextureCubeReader");
        if (!typeReader)
            throw std::runtime_error("TextureCubeReader is not registered");
        return std::any_cast<TextureCube>(typeReader->ReadUntyped(reader, std::any{}));
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

        GraphicsDevice gd{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(800, 480)};
    };
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, BothReadersAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Texture3DReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.TextureCubeReader"));
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd)
{
    ContentManager cm(CNA::TestSupport::EmptyServices(), "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    // On a renderer with no cube storage, TextureCubeReader's own SetData call now
    // throws instead of silently discarding all 42 face/level uploads, so the load fails as a whole
    // rather than handing back a TextureCube that reports LevelCount 7 and holds nothing. That is
    // The content-pipeline consequence of the finding, asserted here rather than worked around.
    if (!CubeStorageSupported())
    {
        EXPECT_THROW((void)cm.Load<TextureCube>("SampleCube64DXT1Mips"),
                     System::NotSupportedException);
        return;
    }

    TextureCube cube = cm.Load<TextureCube>("SampleCube64DXT1Mips");

    EXPECT_EQ(cube.getSizeProperty(), 64);
    const bool preservesDxt1 = gd.GetRenderer().LoadsCompressedContentNativelyEXT()
        && gd.GetRenderer().IsCompressedCubeTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt1));
    EXPECT_EQ(cube.getFormatProperty(),
              preservesDxt1 ? SurfaceFormat::Dxt1 : SurfaceFormat::Color);
    EXPECT_EQ(cube.getLevelCountProperty(), 7);

    // Every face/level combination decoded without throwing and produced real pixel data
    // (a corrupted DXT1 decode would very likely produce an exception or all-zero output, not a
    // plausible non-degenerate image) -- spot-check level 0 (64x64) and the smallest levels (the
    // sub-4x4 DXT1 block-rounding edge cases, 2x2 and 1x1, each still exactly one 8-byte block).
    //
    // The readback half of this test is only meaningful where the renderer really
    // reads a cube face back. It used to be issued unconditionally, and the smallest-level check
    // was a bare EXPECT_NO_THROW that asserted nothing at all -- a renderer answering with the
    // shared layer's fabricated transparent-black face passed it. Both halves now assert the real
    // outcome for this renderer, and the sentinel proves the rejection path writes nothing.
    const Color sentinel(0xA5, 0xA5, 0xA5, 0xA5);
    std::vector<Color> level0(64 * 64, sentinel);
    Color onePixel = sentinel;
    // WINCLOSE-0013: a cube kept as Dxt1 is read back as the blocks it holds. Reading it as Color
    // asks the shared layer to decode DXT on GetData, which it deliberately refuses (SOFTWARE-277/
    // 282, TextureCubeTest.CompressedTransfersRejectNonByteElementTypes -- FNA does not decode
    // there either), so this test failed on every renderer that preserves the blocks, Software
    // included, while asserting a contract the framework had already reversed.
    if (preservesDxt1 && CubeLevel0ReadbackSupported())
    {
        constexpr std::uint8_t kSentinelByte = 0xA5;
        std::vector<std::uint8_t> level0Blocks(16u * 16u * 8u, kSentinelByte); // 64x64 as 4x4 blocks
        ASSERT_NO_THROW(cube.GetData(CubeMapFace::PositiveX, level0Blocks.data(),
                                     static_cast<int>(level0Blocks.size())));
        bool sawDistinctBlock = false;
        for (std::size_t block = 8; block < level0Blocks.size(); block += 8)
        {
            if (!std::equal(level0Blocks.begin(), level0Blocks.begin() + 8,
                            level0Blocks.begin() + static_cast<std::ptrdiff_t>(block)))
            {
                sawDistinctBlock = true;
                break;
            }
        }
        EXPECT_TRUE(sawDistinctBlock) << "level 0 should not be one repeated DXT1 block";

        if (CubeMipReadbackSupported())
        {
            // The 1x1 level is still exactly one 8-byte block -- the sub-4x4 rounding edge case.
            std::vector<std::uint8_t> oneBlock(8u, kSentinelByte);
            ASSERT_NO_THROW(cube.GetData(CubeMapFace::NegativeZ, 6, nullptr, oneBlock.data(), 0, 8));
            EXPECT_TRUE(std::any_of(oneBlock.begin(), oneBlock.end(),
                                    [](std::uint8_t b) { return b != kSentinelByte; }))
                << "the 1x1 mip level should hold a real DXT1 block";
        }
        return;
    }
    if (CubeLevel0ReadbackSupported())
    {
        ASSERT_NO_THROW(cube.GetData(CubeMapFace::PositiveX, level0.data(),
                                     static_cast<int>(level0.size())));
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

        // The smallest mip level (1x1) is the sub-4x4 DXT1 block-rounding edge case. Every renderer
        // that stores cube faces at all now stores the whole declared chain (the cube/volume upload contract gave
        // Software the per-mip storage it was the last to be missing).
        if (CubeMipReadbackSupported())
        {
            ASSERT_NO_THROW(cube.GetData(CubeMapFace::NegativeZ, 6, nullptr, &onePixel, 0, 1));
            EXPECT_NE(onePixel.getPackedValueProperty(), sentinel.getPackedValueProperty())
                << "the 1x1 mip level should decode to a real texel";
        }
        else
        {
            EXPECT_THROW(cube.GetData(CubeMapFace::NegativeZ, 6, nullptr, &onePixel, 0, 1),
                         System::NotSupportedException);
            EXPECT_EQ(onePixel.getPackedValueProperty(), sentinel.getPackedValueProperty());
        }
    }
    else
    {
        EXPECT_THROW(cube.GetData(CubeMapFace::PositiveX, level0.data(),
                                  static_cast<int>(level0.size())),
                     System::NotSupportedException);
        EXPECT_EQ(level0[0].getPackedValueProperty(), sentinel.getPackedValueProperty());
    }
}

// SOFTWARE-275: FNA's TextureCubeReader constructs the serialized format and forwards every face
// byte array unchanged. CNA accepted only Color/DXT and converted every non-native-compressed
// payload through RGBA8, so no packed, integer, half or floating-point XNA cube could be loaded.
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       TextureCubeReaderPreservesEveryClassicUncompressedFormatAndExactBytes)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    constexpr std::array<SurfaceFormat, 15> formats{{
        SurfaceFormat::Color,
        SurfaceFormat::Bgr565,
        SurfaceFormat::Bgra5551,
        SurfaceFormat::Bgra4444,
        SurfaceFormat::Rgba1010102,
        SurfaceFormat::Rg32,
        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable,
    }};

    for (const SurfaceFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        const std::size_t byteCount = static_cast<std::size_t>(
            Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format));
        std::array<std::vector<std::uint8_t>, 6> expected;
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            expected[face].resize(byteCount);
            for (std::size_t index = 0; index < byteCount; ++index)
            {
                expected[face][index] = static_cast<std::uint8_t>(
                    0x21u + face * 23u + index * 11u + static_cast<std::size_t>(format));
            }
        }

        TextureCube cube = ReadHandConstructedCube(gd, format, expected);
        EXPECT_EQ(cube.getFormatProperty(), format);
        EXPECT_EQ(cube.getLevelCountProperty(), 1);
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            std::vector<std::uint8_t> actual(byteCount, 0xCDu);
            cube.GetData(static_cast<CubeMapFace>(face), actual.data(),
                         static_cast<int>(actual.size()));
            EXPECT_EQ(actual, expected[face]) << "face " << face;
        }
    }
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       TextureCubeReaderPreservesEveryClassicCompressedFormatAndExactBlocks)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    constexpr std::array<SurfaceFormat, 3> formats{{
        SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
    }};
    for (const SurfaceFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        const std::size_t byteCount = static_cast<std::size_t>(
            Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format));
        std::array<std::vector<std::uint8_t>, 6> expected;
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            expected[face].resize(byteCount);
            for (std::size_t index = 0; index < byteCount; ++index)
            {
                expected[face][index] = static_cast<std::uint8_t>(
                    0x13u + face * 29u + index * 7u);
            }
        }

        TextureCube cube = ReadHandConstructedCube(gd, format, expected, 4);
        EXPECT_EQ(cube.getFormatProperty(), format);
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            std::vector<std::uint8_t> actual(byteCount, 0xCDu);
            cube.GetData(static_cast<CubeMapFace>(face), actual.data(),
                         static_cast<int>(actual.size()));
            EXPECT_EQ(actual, expected[face]) << "face " << face;
        }
    }
}

// HND-TQ-16: Refresh's TextureCube constructor refuses a format outside the profile's cube list
// with NotSupportedException (ProfileCapabilities.ThrowNotSupportedException).
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       TextureCubeReaderRejectsSignedNormalizedFormats)
{
    for (const SurfaceFormat format :
         {SurfaceFormat::NormalizedByte2, SurfaceFormat::NormalizedByte4})
    {
        SCOPED_TRACE(static_cast<int>(format));
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(static_cast<int32_t>(1));
        writer.Write(static_cast<int32_t>(1));
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(gd);
        System::IO::MemoryStream body(
            serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.TextureCubeReader");
        ASSERT_NE(typeReader, nullptr);
        EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
    }
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, ContentManagerStronglyCachesTextureCubeUntilUnload)
{
    if (!CubeStorageSupported())
    {
        GTEST_SKIP() << "The active renderer has no cube texture storage.";
    }

    ContentManager cm(CNA::TestSupport::EmptyServices(), "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    std::weak_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> cachedRenderer;
    {
        TextureCube first = cm.Load<TextureCube>("SampleCube64DXT1Mips");
        cachedRenderer = first.GetRenderer().weak_from_this();
    }

    ASSERT_FALSE(cachedRenderer.expired())
        << "ContentManager must own the loaded cube after caller wrappers are destroyed";
    const auto originalRenderer = cachedRenderer.lock();

    TextureCube cached = cm.Load<TextureCube>("SampleCube64DXT1Mips");
    EXPECT_EQ(originalRenderer.get(), &cached.GetRenderer());

    cm.Unload();
    TextureCube reloaded = cm.Load<TextureCube>("SampleCube64DXT1Mips");
    EXPECT_NE(originalRenderer.get(), &reloaded.GetRenderer());
}

// Texture3D is a documented, renderer-dependent capability -- Headless has no
// real GPU resource of any kind. On a renderer that doesn't support it, reading now throws a clean
// System::NotSupportedException (from Texture3D's own constructor) instead of previously silently
// succeeding with all-zero pixel data.
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

    if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
    {
        EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
        return;
    }

    std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
    auto texture = std::any_cast<std::shared_ptr<Texture3D>>(resultAny);
    ASSERT_NE(texture, nullptr);

    EXPECT_EQ(texture->getWidthProperty(), width);
    EXPECT_EQ(texture->getHeightProperty(), height);
    EXPECT_EQ(texture->getDepthProperty(), depth);
    EXPECT_EQ(texture->getFormatProperty(), SurfaceFormat::Color);

    std::vector<Color> readBack(4, Color(0, 0, 0, 0));
    if (!VolumeReadbackSupported())
    {
        // The reader's own parsing is what this test is about, and it has been fully checked above;
        // only the round trip needs a renderer that can fetch voxels back.
        EXPECT_THROW((void)texture->GetData(readBack.data(), 4), System::NotSupportedException);
        return;
    }
    texture->GetData(readBack.data(), 4);
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        EXPECT_EQ(readBack[i].getPackedValueProperty(), pixels[i].getPackedValueProperty()) << "pixel " << i;
    }
}

// SOFTWARE-274: FNA does not reinterpret a Texture3DReader payload as Color. It constructs the
// declared format and forwards the exact byte array for each mip level to Texture3D.SetData<byte>.
// CNA previously accepted only Color/DXT here and always produced a Color texture, making fourteen
// legal XNA HiDef volume formats impossible to load even though both parity renderers store them.
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       Texture3DReaderPreservesEveryClassicVolumeFormatAndExactBytes)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    struct FormatCase
    {
        SurfaceFormat format;
        std::size_t bytesPerVoxel;
    };
    constexpr std::array<FormatCase, 15> cases{{
        {SurfaceFormat::Color, 4u},
        {SurfaceFormat::Bgr565, 2u},
        {SurfaceFormat::Bgra5551, 2u},
        {SurfaceFormat::Bgra4444, 2u},
        {SurfaceFormat::Rgba1010102, 4u},
        {SurfaceFormat::Rg32, 4u},
        {SurfaceFormat::Rgba64, 8u},
        {SurfaceFormat::Alpha8, 1u},
        {SurfaceFormat::Single, 4u},
        {SurfaceFormat::Vector2, 8u},
        {SurfaceFormat::Vector4, 16u},
        {SurfaceFormat::HalfSingle, 2u},
        {SurfaceFormat::HalfVector2, 4u},
        {SurfaceFormat::HalfVector4, 8u},
        {SurfaceFormat::HdrBlendable, 8u},
    }};

    for (const FormatCase& formatCase : cases)
    {
        SCOPED_TRACE(static_cast<int>(formatCase.format));
        std::vector<std::uint8_t> expected(formatCase.bytesPerVoxel * 2u);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            expected[index] = static_cast<std::uint8_t>(
                0x21u + index * 17u + static_cast<std::size_t>(formatCase.format));
        }

        const auto texture = ReadHandConstructedVolume(
            gd, formatCase.format, 2, 1, 1, {expected});
        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(texture->getFormatProperty(), formatCase.format);
        EXPECT_EQ(texture->getLevelCountProperty(), 1);

        std::vector<std::uint8_t> actual(expected.size(), 0xCDu);
        texture->GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);
    }
}

// SOFTWARE-274: the canonical XNB validator retained the old width/height-only volume mip rule
// after SOFTWARE-273 corrected Texture3D itself. A 1x1x8 authored volume therefore rejected its
// legal second through fourth levels before the resource constructor could see them.
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       Texture3DReaderAcceptsDepthDominantMipChainAndPreservesEveryLevel)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    const std::vector<std::vector<std::uint8_t>> expected{
        {0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u},
        {0x20u, 0x21u, 0x22u, 0x23u},
        {0x30u, 0x31u},
        {0x40u},
    };
    const auto texture = ReadHandConstructedVolume(
        gd, SurfaceFormat::Alpha8, 1, 1, 8, expected);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getFormatProperty(), SurfaceFormat::Alpha8);
    EXPECT_EQ(texture->getLevelCountProperty(), 4);

    int depth = 8;
    for (int level = 0; level < 4; ++level)
    {
        std::vector<std::uint8_t> actual(expected[static_cast<std::size_t>(level)].size(), 0xCDu);
        texture->GetData(level, 0, 0, 1, 1, 0, depth,
                         actual.data(), 0, static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected[static_cast<std::size_t>(level)]);
        depth = std::max(depth >> 1, 1);
    }
}

// TextureCubeReader was the one sibling among the three XNB texture readers
// missing this check -- a crafted declared byteCount undersized relative to faceSize*faceSize*4
// previously reached the unchecked pixel-unpacking loop, an out-of-bounds heap read. Ported
// verbatim from Texture2DReader's own identical, already-tested check.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderRejectsByteCountMismatchedWithSize)
{
    constexpr int32_t size = 2;
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(size);
    writer.Write(static_cast<int32_t>(1)); // levels
    writer.Write(static_cast<int32_t>(4)); // byteCount for face 0 level 0 -- should be 2*2*4=16, not 4
    writer.Write(static_cast<uint8_t>(0));
    writer.Write(static_cast<uint8_t>(0));
    writer.Write(static_cast<uint8_t>(0));
    writer.Write(static_cast<uint8_t>(0));
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.TextureCubeReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// Texture3DReader's own decoded-byte-size check has one more factor than
// Texture2DReader's (width*height*depth*4 vs width*height*4), found to share the identical
// signed-int64-overflow UB shape during that task's root-cause sweep. This is the exact adversarial
// shape confirmed by UBSan pre-fix.
// KI-CONTENT-REACH3D: the texture is now constructed from the header, where Refresh's
// Texture3D.CreateTexture refuses an extent past the profile's MaxVolumeExtent (HiDef 256) with
// ProfileCapabilities.ThrowNotSupportedException (IL_00a9-00d8); this used to expect CNA's own
// ContentLoadException from its decoded-size bound, which came first.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderAbsurdlyLargeDimensionsThrowNotSupportedExceptionNotBadAlloc)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // width -- adversarially huge
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // height -- adversarially huge
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // depth -- adversarially huge
    writer.Write(static_cast<int32_t>(1));           // levelCount
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
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
}

namespace
{
    /// Reads a header-only body of @p readerName (format, then @p fields) and returns what it threw.
    std::exception_ptr ReadTextureHeaderOnly(GraphicsDevice& device, const char* readerName,
                                             const SurfaceFormat format,
                                             const std::vector<int32_t>& fields)
    {
        System::IO::MemoryStream ms;
        System::IO::BinaryWriter writer(&ms, true);
        writer.Write(static_cast<int32_t>(format));
        for (const int32_t field : fields) { writer.Write(field); }
        writer.Flush();
        const auto buf = ms.ToArray();

        ContentManager cm;
        cm.setGraphicsDevice(device);
        System::IO::MemoryStream body(buf.data(), static_cast<int32_t>(buf.size()));
        ContentReader reader(&cm, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(readerName);
        try
        {
            (void)typeReader->ReadUntyped(reader, std::any{});
        }
        catch (...)
        {
            return std::current_exception();
        }
        return nullptr;
    }

    /// Expects Refresh's ArgumentOutOfRangeException for @p parameter, with its resource text.
    void ExpectSizeRefusal(const std::exception_ptr& thrown, const std::string& parameter)
    {
        ASSERT_NE(thrown, nullptr);
        try
        {
            std::rethrow_exception(thrown);
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), parameter);
            EXPECT_EQ(error.getMessageProperty().rfind("Resource size must be greater than zero.", 0), 0u)
                << error.getMessageProperty();
        }
        catch (const std::exception& other)
        {
            ADD_FAILURE() << "expected ArgumentOutOfRangeException, got: " << other.what();
        }
    }

    constexpr char Texture3DReaderName[] = "Microsoft.Xna.Framework.Content.Texture3DReader";
    constexpr char TextureCubeReaderName[] = "Microsoft.Xna.Framework.Content.TextureCubeReader";
}

// KI-CONTENT-TEXDIM: Refresh's Texture3DReader.Read reads format, width, height, depth and level
// count (IL_0000-0023), then constructs the texture (IL_0035); Texture3D.CreateTexture refuses a
// width, height or depth of zero or less, in that order (IL_0013-004f), with
// ArgumentOutOfRangeException and the ResourcesMustBeGreaterThanZeroSize text, before the
// profile and format checks (IL_0050-00a4). ContentReader.ReadAsset does not wrap it.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderNonPositiveSizeThrowsArgumentOutOfRangeException)
{
    for (const int32_t value : {0, -1})
    {
        SCOPED_TRACE(value);
        ExpectSizeRefusal(ReadTextureHeaderOnly(gd, Texture3DReaderName, SurfaceFormat::Color,
                                                {value, 4, 4, 1}), "width");
        ExpectSizeRefusal(ReadTextureHeaderOnly(gd, Texture3DReaderName, SurfaceFormat::Color,
                                                {4, value, 4, 1}), "height");
        ExpectSizeRefusal(ReadTextureHeaderOnly(gd, Texture3DReaderName, SurfaceFormat::Color,
                                                {4, 4, value, 1}), "depth");
    }
    ExpectSizeRefusal(ReadTextureHeaderOnly(gd, Texture3DReaderName, SurfaceFormat::Color,
                                            {4, 0, 0, 1}), "height");
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderRefusesTheSizeBeforeTheFormat)
{
    // Dxt1 is not a volume format (the format refusal below); a zero size still wins.
    ExpectSizeRefusal(ReadTextureHeaderOnly(gd, Texture3DReaderName, SurfaceFormat::Dxt1,
                                            {4, 4, 0, 1}), "depth");
    EXPECT_THROW(std::rethrow_exception(ReadTextureHeaderOnly(
                     gd, Texture3DReaderName, SurfaceFormat::Dxt1, {4, 4, 4, 1})),
                 System::NotSupportedException);
}

// TextureCubeReader's own decoded-byte-size check (size*size*4) shares the
// identical signed-int64-overflow UB shape as Texture2DReader's -- confirmed during that task's
// root-cause sweep.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderAbsurdlyLargeSizeThrowsContentLoadExceptionNotBadAlloc)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // size -- adversarially huge
    writer.Write(static_cast<int32_t>(1));           // levels
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.TextureCubeReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// KI-CONTENT-REACH3D: Refresh's Texture3DReader.Read reads its five header fields, looks up the device
// and constructs the texture (IL_0025-0035) before reading any level; Texture3D.CreateTexture reads
// the device's profile capabilities and refuses MaxVolumeExtent == 0 -- Reach -- with
// ProfileCapabilities.ThrowNotSupportedException(ProfileFeatureNotSupported) (IL_0050-0074), after
// the size (IL_0013-004f) and before the format (IL_0079-00a4).
TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderOnReachRefusesVolumeTexturesBeforeTheFormatAndTheLevels)
{
    GraphicsDevice reach{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
                         PresentationParameters::CreateWindowedEXT(800, 480)};
    const auto expectNoVolumeSupport = [](const std::exception_ptr& thrown) {
        ASSERT_NE(thrown, nullptr);
        try
        {
            std::rethrow_exception(thrown);
        }
        catch (const System::NotSupportedException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), "XNA Framework Reach profile does not support Texture3D.");
        }
        catch (const std::exception& other)
        {
            ADD_FAILURE() << "expected NotSupportedException, got: " << other.what();
        }
    };
    // No level bytes follow any of these headers: the refusal comes first.
    expectNoVolumeSupport(ReadTextureHeaderOnly(reach, Texture3DReaderName, SurfaceFormat::Color, {4, 4, 4, 1}));
    // Before the format: Dxt1 is no volume format, but the profile has no volume textures at all.
    expectNoVolumeSupport(ReadTextureHeaderOnly(reach, Texture3DReaderName, SurfaceFormat::Dxt1, {4, 4, 4, 1}));
    // Before CNA's own level-count refusal.
    expectNoVolumeSupport(ReadTextureHeaderOnly(reach, Texture3DReaderName, SurfaceFormat::Color, {4, 4, 4, 0}));
    // The size still comes first.
    ExpectSizeRefusal(ReadTextureHeaderOnly(reach, Texture3DReaderName, SurfaceFormat::Color, {4, 0, 4, 1}), "height");
    // HiDef has volume textures: the same header goes on to its levels, where a renderer with
    // volume storage finds none (a renderer without it refuses at construction, as before).
    if (gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
    {
        const std::exception_ptr hiDef =
            ReadTextureHeaderOnly(gd, Texture3DReaderName, SurfaceFormat::Color, {4, 4, 4, 1});
        ASSERT_NE(hiDef, nullptr);
        EXPECT_THROW(std::rethrow_exception(hiDef), System::IO::EndOfStreamException);
    }
}

// KI-CONTENT-TEXDIM: Refresh's TextureCubeReader.Read reads format, size and level count
// (IL_0000-0016), then constructs the cube (IL_0027); TextureCube.ValidateCreationParameters
// refuses a size of zero or less (IL_0000-0013) with ArgumentOutOfRangeException("size") before
// the format (IL_0014-004e).
TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderNonPositiveSizeThrowsArgumentOutOfRangeException)
{
    for (const int32_t value : {0, -1, std::numeric_limits<int32_t>::min()})
    {
        SCOPED_TRACE(value);
        ExpectSizeRefusal(ReadTextureHeaderOnly(gd, TextureCubeReaderName, SurfaceFormat::Color,
                                                {value, 1}), "size");
    }
    // A cube format the profile refuses does not come first.
    ExpectSizeRefusal(ReadTextureHeaderOnly(gd, TextureCubeReaderName, static_cast<SurfaceFormat>(999),
                                            {0, 1}), "size");
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderRejectsCompressedSurfaceFormats)
{
    // XNA 4.0's exact HiDef volume-format table contains all fifteen uncompressed formats above,
    // but no block-compressed format. CNA used to accept DXT1/3/5 and reject Bgr565 instead.
    // HND-TQ-16: the Texture3D constructor's refusal is NotSupportedException.
    for (const SurfaceFormat format :
         {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
    {
        SCOPED_TRACE(static_cast<int>(format));
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(static_cast<int32_t>(4));
        writer.Write(static_cast<int32_t>(4));
        writer.Write(static_cast<int32_t>(1));
        writer.Write(static_cast<int32_t>(1));
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(gd);
        System::IO::MemoryStream body(
            serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture3DReader");
        ASSERT_NE(typeReader, nullptr);
        EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
    }
}

// Refresh Texture3DReader.Read and TextureCubeReader.Read construct with mipMap = count > 1 and loop
// `level < count` (Texture3D IL_002b-0035/0087-008a, TextureCube IL_001e-0027/005f-0062), as
// Texture2DReader does: a count of zero or less creates a one-level texture and reads no level.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, ANonPositiveLevelCountCreatesAOneLevelTextureAndReadsNoLevel)
{
    for (const int32_t count : {0, -1, std::numeric_limits<int32_t>::min()})
    {
        for (const bool cube : {false, true})
        {
            SCOPED_TRACE(std::to_string(count) + (cube ? " cube" : " volume"));
            System::IO::MemoryStream stream;
            System::IO::BinaryWriter writer(&stream, true);
            writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
            writer.Write(static_cast<int32_t>(4));
            if (!cube)
            {
                writer.Write(static_cast<int32_t>(4));
                writer.Write(static_cast<int32_t>(4));
            }
            writer.Write(count);
            writer.Write(static_cast<int32_t>(0x5eed));
            writer.Flush();
            const auto serialized = stream.ToArray();
            ContentManager content;
            content.setGraphicsDevice(gd);
            System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
            ContentReader reader(&content, &body, "test", 5, 'w');
            auto typeReader = ContentTypeReaderManager::CreateReader(
                cube ? "Microsoft.Xna.Framework.Content.TextureCubeReader"
                     : "Microsoft.Xna.Framework.Content.Texture3DReader");
            ASSERT_NE(typeReader, nullptr);
            if ((cube && !CubeStorageSupported()) ||
                (!cube && !gd.SupportsCapability(CNA::GraphicsCapability::Texture3D)))
            {
                // The construction is reached (no level-count refusal before it) and refused.
                EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
                continue;
            }
            std::any result = typeReader->ReadUntyped(reader, std::any{});
            if (cube)
            {
                const auto& texture = std::any_cast<const TextureCube&>(result);
                EXPECT_EQ(texture.getSizeProperty(), 4);
                EXPECT_EQ(texture.getLevelCountProperty(), 1);
            }
            else
            {
                const auto texture = std::any_cast<std::shared_ptr<Texture3D>>(result);
                ASSERT_NE(texture, nullptr);
                EXPECT_EQ(texture->getDepthProperty(), 4);
                EXPECT_EQ(texture->getLevelCountProperty(), 1);
            }
            EXPECT_EQ(reader.ReadInt32(), 0x5eed) << "a level was read";
        }
    }
}

// Refresh Texture3DReader.Read and TextureCubeReader.Read never load their existingInstance argument
// (no ldarg.2): a supplied texture is neither refused for its size nor written, and the result is new.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, BothReadersIgnoreAnExistingInstance)
{
    const auto read = [this](bool cube, std::any existing) {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
        writer.Write(static_cast<int32_t>(1));
        if (!cube)
        {
            writer.Write(static_cast<int32_t>(1));
            writer.Write(static_cast<int32_t>(1));
        }
        writer.Write(static_cast<int32_t>(1));
        for (int level = 0; level < (cube ? 6 : 1); ++level)
        {
            writer.Write(static_cast<int32_t>(4));
            for (int i = 0; i < 4; ++i) writer.Write(static_cast<std::uint8_t>(0x21));
        }
        writer.Flush();
        const auto bytes = stream.ToArray();
        ContentManager content;
        content.setGraphicsDevice(gd);
        System::IO::MemoryStream body(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            cube ? "Microsoft.Xna.Framework.Content.TextureCubeReader"
                 : "Microsoft.Xna.Framework.Content.Texture3DReader");
        return typeReader->ReadUntyped(reader, std::move(existing));
    };
    const Color fill(0x7a, 0x7a, 0x7a, 0x7a);
    if (CubeStorageSupported())
    {
        TextureCube existing(gd, 2, false, SurfaceFormat::Color);
        const std::vector<Color> face(4, fill);
        existing.SetData(CubeMapFace::PositiveX, face.data(), 0, 4);
        const std::any result = read(true, std::any(existing));
        const auto& cube = std::any_cast<const TextureCube&>(result);
        EXPECT_EQ(cube.getSizeProperty(), 1);
        EXPECT_EQ(existing.getSizeProperty(), 2);
        std::vector<std::uint8_t> back(16);
        existing.GetData(CubeMapFace::PositiveX, back.data(), 16);
        EXPECT_EQ(back, std::vector<std::uint8_t>(16, 0x7a)) << "the existing cube was written";
    }
    if (gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
    {
        auto existing = std::make_shared<Texture3D>(gd, 2, 2, 2, false, SurfaceFormat::Color);
        const auto result = std::any_cast<std::shared_ptr<Texture3D>>(read(false, std::any(existing)));
        ASSERT_NE(result, nullptr);
        EXPECT_NE(result.get(), existing.get());
        EXPECT_EQ(result->getWidthProperty(), 1);
        EXPECT_EQ(existing->getWidthProperty(), 2);
    }
}
