// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-101A/B/C: the Texture2D, TextureCube and Texture3D schemas.
//
// The three asset types share one chunk layout, so the tests are written to prove that sharing is
// safe rather than to repeat themselves: each type is exercised for the constraint only it has
// (cube faces must be square and six, only a 3D texture may be deeper than one texel), and the
// layout rules they share are tested once.

#include <cstdint>
#include <gtest/gtest.h>
#include <numeric>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbTextureData;
using CNA::Content::Cnb::CnbTextureFormat;
using CNA::Content::Cnb::CnbTextureRepresentation;
using CNA::Content::Cnb::DecodeTexture2DFromCnb;
using CNA::Content::Cnb::DecodeTexture3DFromCnb;
using CNA::Content::Cnb::DecodeTextureCubeFromCnb;
using CNA::Content::Cnb::EncodeTexture2DToCnb;
using CNA::Content::Cnb::EncodeTexture3DToCnb;
using CNA::Content::Cnb::EncodeTextureCubeToCnb;
using CNA::Content::Cnb::MakeRgba8Texture2DData;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace
{
    /// Fills a level with a value pattern derived from its own index, so a decoder that returns
    /// the right NUMBER of levels but pairs them up wrongly still fails.
    std::vector<std::uint8_t> PatternLevel(std::uint64_t byteCount, std::uint8_t seed)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount));
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<std::uint8_t>((i * 7u + seed * 31u) & 0xFFu);
        }
        return bytes;
    }

    CnbTextureData MakeMipped2D(std::uint32_t width, std::uint32_t height, std::uint32_t mips)
    {
        CnbTextureData data;
        data.width = width;
        data.height = height;
        data.depth = 1u;
        data.faceCount = 1u;
        data.mipCount = mips;
        CnbTextureRepresentation representation;
        representation.format = CnbTextureFormat::Rgba8;
        for (std::uint32_t mip = 0u; mip < mips; ++mip)
        {
            std::uint32_t w = 0u;
            std::uint32_t h = 0u;
            std::uint32_t d = 0u;
            CNA::Content::Cnb::CnbTextureLevelDimensions(data, mip, w, h, d);
            representation.levels.push_back(PatternLevel(
                CNA::Content::Cnb::CnbTextureLevelByteSize(CnbTextureFormat::Rgba8, w, h, d),
                static_cast<std::uint8_t>(mip + 1u)));
        }
        data.representations.push_back(std::move(representation));
        return data;
    }

    CnbTextureData MakeCube(std::uint32_t size)
    {
        CnbTextureData data;
        data.width = size;
        data.height = size;
        data.depth = 1u;
        data.faceCount = CNA::Content::Cnb::CnbTextureCubeFaceCount;
        data.mipCount = 1u;
        CnbTextureRepresentation representation;
        representation.format = CnbTextureFormat::Rgba8;
        for (std::uint32_t face = 0u; face < data.faceCount; ++face)
        {
            representation.levels.push_back(PatternLevel(
                static_cast<std::uint64_t>(size) * size * 4u, static_cast<std::uint8_t>(face + 1u)));
        }
        data.representations.push_back(std::move(representation));
        return data;
    }

    CnbTextureData MakeVolume(std::uint32_t w, std::uint32_t h, std::uint32_t d)
    {
        CnbTextureData data;
        data.width = w;
        data.height = h;
        data.depth = d;
        data.faceCount = 1u;
        data.mipCount = 1u;
        CnbTextureRepresentation representation;
        representation.format = CnbTextureFormat::Rgba8;
        representation.levels.push_back(
            PatternLevel(static_cast<std::uint64_t>(w) * h * d * 4u, 9u));
        data.representations.push_back(std::move(representation));
        return data;
    }

    void ExpectSameTexture(const CnbTextureData& expected, const CnbTextureData& actual)
    {
        EXPECT_EQ(expected.width, actual.width);
        EXPECT_EQ(expected.height, actual.height);
        EXPECT_EQ(expected.depth, actual.depth);
        EXPECT_EQ(expected.faceCount, actual.faceCount);
        EXPECT_EQ(expected.mipCount, actual.mipCount);
        ASSERT_EQ(expected.representations.size(), actual.representations.size());
        for (std::size_t r = 0; r < expected.representations.size(); ++r)
        {
            EXPECT_EQ(expected.representations[r].format, actual.representations[r].format);
            ASSERT_EQ(expected.representations[r].levels.size(),
                      actual.representations[r].levels.size());
            for (std::size_t l = 0; l < expected.representations[r].levels.size(); ++l)
            {
                EXPECT_EQ(expected.representations[r].levels[l],
                          actual.representations[r].levels[l])
                    << "representation " << r << " level " << l;
            }
        }
    }
}

TEST(CnbTextureFormatTest, EveryIdentifierMapsBothWaysAndTheMappingIsAnInvolution)
{
    // The two directions are generated from one table, so the property worth asserting is that
    // round-tripping any identifier through SurfaceFormat and back is the identity. A table row
    // in the wrong slot, or a duplicated SurfaceFormat, breaks exactly this.
    for (std::uint32_t raw = 1u; raw <= CNA::Content::Cnb::CnbTextureFormatMax; ++raw)
    {
        const auto format = static_cast<CnbTextureFormat>(raw);
        ASSERT_TRUE(CNA::Content::Cnb::IsKnownCnbTextureFormat(raw)) << raw;
        const auto surface = CNA::Content::Cnb::CnbTextureFormatToSurfaceFormat(format);
        EXPECT_EQ(format, CNA::Content::Cnb::SurfaceFormatToCnbTextureFormat(surface))
            << "identifier " << raw << " does not survive a round trip through SurfaceFormat";
        EXPECT_GT(CNA::Content::Cnb::CnbTextureFormatUnitBytes(format), 0u) << raw;
        EXPECT_NE(CNA::Content::Cnb::CnbTextureFormatToString(format).find("unknown"),
                  0u) << raw;
    }
}

TEST(CnbTextureFormatTest, TheFrozenIdentifierValuesAreTheOnesTheDocumentPromises)
{
    // These numbers are in shipped files the moment the first texture is compiled. Spelled out
    // rather than derived, because deriving them from the enum is exactly the mistake this
    // enumeration exists to prevent.
    EXPECT_EQ(static_cast<std::uint32_t>(CnbTextureFormat::Unknown), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(CnbTextureFormat::Rgba8), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(CnbTextureFormat::Bgra8), 2u);
    EXPECT_EQ(static_cast<std::uint32_t>(CnbTextureFormat::Rgba8Srgb), 3u);
    EXPECT_EQ(static_cast<std::uint32_t>(CnbTextureFormat::Bc1), 22u);
    EXPECT_EQ(static_cast<std::uint32_t>(CnbTextureFormat::Bc7Srgb), 27u);
    EXPECT_EQ(CNA::Content::Cnb::CnbTextureFormatMax, 27u);
    EXPECT_FALSE(CNA::Content::Cnb::IsKnownCnbTextureFormat(0u));
    EXPECT_FALSE(CNA::Content::Cnb::IsKnownCnbTextureFormat(28u));
}

TEST(CnbTextureFormatTest, ABlockCompressedLevelRoundsUpToWholeBlocks)
{
    using CNA::Content::Cnb::CnbTextureLevelByteSize;
    // A 1x1 BC7 level is one whole 16-byte block, not a fraction of one. Getting this wrong makes
    // the smallest mip of every compressed texture the wrong size, which is the tail of the chain
    // nobody looks at.
    EXPECT_EQ(CnbTextureLevelByteSize(CnbTextureFormat::Bc7, 1u, 1u, 1u), 16u);
    EXPECT_EQ(CnbTextureLevelByteSize(CnbTextureFormat::Bc7, 4u, 4u, 1u), 16u);
    EXPECT_EQ(CnbTextureLevelByteSize(CnbTextureFormat::Bc7, 5u, 4u, 1u), 32u);
    EXPECT_EQ(CnbTextureLevelByteSize(CnbTextureFormat::Bc1, 8u, 8u, 1u), 32u);
    EXPECT_EQ(CnbTextureLevelByteSize(CnbTextureFormat::Rgba8, 3u, 5u, 1u), 60u);
    EXPECT_EQ(CnbTextureLevelByteSize(CnbTextureFormat::Rgba8, 4u, 4u, 4u), 256u);
    EXPECT_THROW(CnbTextureLevelByteSize(CnbTextureFormat::Rgba8, 0u, 1u, 1u), ContentLoadException);
    EXPECT_THROW(CnbTextureLevelByteSize(CnbTextureFormat::Unknown, 1u, 1u, 1u),
                 ContentLoadException);
}

TEST(CnbTextureCodecTest, ASingleLevelRgba8TextureRoundTrips)
{
    std::vector<std::uint8_t> pixels(2u * 3u * 4u);
    std::iota(pixels.begin(), pixels.end(), static_cast<std::uint8_t>(0));
    const CnbTextureData source = MakeRgba8Texture2DData(2u, 3u, pixels);

    const std::vector<std::uint8_t> bytes = EncodeTexture2DToCnb(source, "Textures/flat");
    const CnbDocument document = CnbDocument::Parse(bytes, "flat.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::Texture2D);
    EXPECT_EQ(document.AssetSchemaVersion(), CNA::Content::Cnb::CnbTextureSchemaVersion);
    EXPECT_EQ(document.Metadata().assetTypeName, "Microsoft.Xna.Framework.Graphics.Texture2D");
    EXPECT_EQ(document.Metadata().contentName, "Textures/flat");

    ExpectSameTexture(source, DecodeTexture2DFromCnb(document));
}

TEST(CnbTextureCodecTest, AFullMipChainRoundTripsWithEveryLevelDistinct)
{
    const CnbTextureData source = MakeMipped2D(8u, 4u, 4u);
    // 8x4 -> 4x2 -> 2x1 -> 1x1: the height reaches 1 before the width does, which is the case a
    // naive "halve both until either is 1" loop gets wrong.
    ASSERT_EQ(source.representations[0].levels[0].size(), 8u * 4u * 4u);
    ASSERT_EQ(source.representations[0].levels[1].size(), 4u * 2u * 4u);
    ASSERT_EQ(source.representations[0].levels[2].size(), 2u * 1u * 4u);
    ASSERT_EQ(source.representations[0].levels[3].size(), 1u * 1u * 4u);

    const std::vector<std::uint8_t> bytes = EncodeTexture2DToCnb(source);
    ExpectSameTexture(source, DecodeTexture2DFromCnb(CnbDocument::Parse(bytes, "mips.cnb")));
}

TEST(CnbTextureCodecTest, ACubeMapRoundTripsWithItsSixFacesInOrder)
{
    const CnbTextureData source = MakeCube(4u);
    const std::vector<std::uint8_t> bytes = EncodeTextureCubeToCnb(source, "Sky/day");
    const CnbDocument document = CnbDocument::Parse(bytes, "day.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::TextureCube);
    const CnbTextureData decoded = DecodeTextureCubeFromCnb(document);
    ExpectSameTexture(source, decoded);
    // Each face was seeded differently, so this also proves the faces did not get reordered.
    for (std::size_t face = 1; face < decoded.representations[0].levels.size(); ++face)
    {
        EXPECT_NE(decoded.representations[0].levels[0], decoded.representations[0].levels[face]);
    }
}

TEST(CnbTextureCodecTest, AVolumeTextureRoundTripsAndItsDepthCountsTowardsItsSize)
{
    const CnbTextureData source = MakeVolume(2u, 2u, 3u);
    ASSERT_EQ(source.representations[0].levels[0].size(), 2u * 2u * 3u * 4u);
    const std::vector<std::uint8_t> bytes = EncodeTexture3DToCnb(source, "Volumes/fog");
    const CnbDocument document = CnbDocument::Parse(bytes, "fog.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::Texture3D);
    ExpectSameTexture(source, DecodeTexture3DFromCnb(document));
}

TEST(CnbTextureCodecTest, AVolumeMipChainHalvesDepthToo)
{
    CnbTextureData source = MakeVolume(4u, 4u, 4u);
    source.mipCount = 3u;
    source.representations[0].levels.clear();
    for (std::uint32_t mip = 0u; mip < 3u; ++mip)
    {
        std::uint32_t w = 0u;
        std::uint32_t h = 0u;
        std::uint32_t d = 0u;
        CNA::Content::Cnb::CnbTextureLevelDimensions(source, mip, w, h, d);
        source.representations[0].levels.push_back(PatternLevel(
            static_cast<std::uint64_t>(w) * h * d * 4u, static_cast<std::uint8_t>(mip + 1u)));
    }
    ASSERT_EQ(source.representations[0].levels[1].size(), 2u * 2u * 2u * 4u);
    ASSERT_EQ(source.representations[0].levels[2].size(), 1u * 1u * 1u * 4u);

    const std::vector<std::uint8_t> bytes = EncodeTexture3DToCnb(source);
    ExpectSameTexture(source, DecodeTexture3DFromCnb(CnbDocument::Parse(bytes, "vol.cnb")));
}

TEST(CnbTextureCodecTest, TheEncoderRefusesADescriptionItsOwnDecoderWouldReject)
{
    // Each of these is a shape rule; the encoder enforces every one so that no writer in the
    // project can produce a file the reader then refuses.
    CnbTextureData zeroDimension = MakeRgba8Texture2DData(1u, 1u, {0, 0, 0, 0});
    zeroDimension.width = 0u;
    EXPECT_THROW((void)EncodeTexture2DToCnb(zeroDimension), ContentLoadException);

    CnbTextureData noRepresentations = MakeRgba8Texture2DData(1u, 1u, {0, 0, 0, 0});
    noRepresentations.representations.clear();
    EXPECT_THROW((void)EncodeTexture2DToCnb(noRepresentations), ContentLoadException);

    CnbTextureData wrongLevelSize = MakeRgba8Texture2DData(2u, 2u, std::vector<std::uint8_t>(16u));
    wrongLevelSize.representations[0].levels[0].push_back(0u);
    EXPECT_THROW((void)EncodeTexture2DToCnb(wrongLevelSize), ContentLoadException);

    CnbTextureData tooManyMips = MakeMipped2D(2u, 2u, 2u);
    tooManyMips.mipCount = CNA::Content::Cnb::CnbMaxTextureMipLevels + 1u;
    EXPECT_THROW((void)EncodeTexture2DToCnb(tooManyMips), ContentLoadException);

    // A reserved-but-unwritten format must be refused loudly rather than written as a file no
    // reader in this build can turn into a texture.
    CnbTextureData bc7 = MakeRgba8Texture2DData(4u, 4u, std::vector<std::uint8_t>(64u));
    bc7.representations[0].format = CnbTextureFormat::Bc7;
    EXPECT_THROW((void)EncodeTexture2DToCnb(bc7), ContentLoadException);
}

TEST(CnbTextureCodecTest, EachAssetTypeRefusesTheOtherTwoShapes)
{
    // A cube's six faces, a volume's depth and a 2D texture's single flat level are the only
    // things separating three types that share a chunk layout; each rule is enforced on both
    // sides of the codec.
    EXPECT_THROW((void)EncodeTextureCubeToCnb(MakeMipped2D(4u, 4u, 1u)), ContentLoadException);

    CnbTextureData oblongCube = MakeCube(4u);
    oblongCube.height = 2u;
    EXPECT_THROW((void)EncodeTextureCubeToCnb(oblongCube), ContentLoadException);

    EXPECT_THROW((void)EncodeTexture2DToCnb(MakeVolume(2u, 2u, 2u)), ContentLoadException);
    EXPECT_THROW((void)EncodeTextureCubeToCnb(MakeVolume(2u, 2u, 2u)), ContentLoadException);

    // And a file of the wrong asset type is refused by the decoder, not silently reinterpreted.
    const std::vector<std::uint8_t> cube = EncodeTextureCubeToCnb(MakeCube(2u));
    EXPECT_THROW((void)DecodeTexture2DFromCnb(CnbDocument::Parse(cube, "cube.cnb")),
                 ContentLoadException);
    const std::vector<std::uint8_t> flat = EncodeTexture2DToCnb(MakeMipped2D(2u, 2u, 1u));
    EXPECT_THROW((void)DecodeTextureCubeFromCnb(CnbDocument::Parse(flat, "flat.cnb")),
                 ContentLoadException);
    EXPECT_THROW((void)DecodeTexture3DFromCnb(CnbDocument::Parse(flat, "flat.cnb")),
                 ContentLoadException);
}

TEST(CnbTextureCodecTest, TheRepresentationTableMustAccountForEveryPayloadChunk)
{
    // A TEXD chunk owned by no representation would be silently ignored, which is how a file
    // smuggles data past a reader. Built by hand, because no encoder in the project can produce
    // it.
    const CnbTextureData source = MakeRgba8Texture2DData(1u, 1u, {1, 2, 3, 4});
    const std::vector<std::uint8_t> good = EncodeTexture2DToCnb(source);
    ASSERT_NO_THROW((void)DecodeTexture2DFromCnb(CnbDocument::Parse(good, "good.cnb")));

    CNA::Content::Cnb::CnbByteWriter header;
    header.WriteU32(1u); header.WriteU32(1u); header.WriteU32(1u);
    header.WriteU32(1u); header.WriteU32(1u); header.WriteU32(1u);
    CNA::Content::Cnb::CnbByteWriter descriptors;
    descriptors.WriteU32(static_cast<std::uint32_t>(CnbTextureFormat::Rgba8));
    descriptors.WriteU32(0u);
    descriptors.WriteU32(1u);
    descriptors.WriteU32(0u);
    descriptors.WriteU64(4u);

    CNA::Content::Cnb::CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Texture2D, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Texture2D", "");
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Header, header.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Representations, descriptors.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Payload, {1, 2, 3, 4},
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 16u);
    writer.AddChunk(CNA::Content::Cnb::CnbTextureChunk::Payload, {9, 9, 9, 9},
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 16u);

    try
    {
        (void)DecodeTexture2DFromCnb(CnbDocument::Parse(writer.Build(), "extra.cnb"));
        FAIL() << "a payload owned by no representation must be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("silently ignored"), std::string::npos) << e.what();
    }
}

TEST(CnbTextureCodecTest, RepresentationSelectionPrefersTheEarliestSupportedFormat)
{
    CnbTextureData data = MakeRgba8Texture2DData(1u, 1u, {1, 2, 3, 4});
    // Two more representations, added directly rather than through the encoder, because schema 1
    // deliberately has no writer for a second format yet. Selection is implemented now so that a
    // file with several is not a future format change.
    CnbTextureRepresentation bc7;
    bc7.format = CnbTextureFormat::Bc7;
    bc7.levels.push_back(std::vector<std::uint8_t>(16u));
    CnbTextureRepresentation bgra;
    bgra.format = CnbTextureFormat::Bgra8;
    bgra.levels.push_back(std::vector<std::uint8_t>(4u));
    data.representations.insert(data.representations.begin(), std::move(bc7));
    data.representations.push_back(std::move(bgra));

    const auto anything = [](CnbTextureFormat) { return true; };
    EXPECT_EQ(CNA::Content::Cnb::SelectCnbTextureRepresentation(data, anything), 0u);

    const auto noBlockFormats = [](CnbTextureFormat f)
    { return !CNA::Content::Cnb::IsBlockCompressedCnbTextureFormat(f); };
    EXPECT_EQ(CNA::Content::Cnb::SelectCnbTextureRepresentation(data, noBlockFormats), 1u);

    const auto nothing = [](CnbTextureFormat) { return false; };
    EXPECT_EQ(CNA::Content::Cnb::SelectCnbTextureRepresentation(data, nothing),
              data.representations.size());
}

// ---------------------------------------------------------------------------
// Mip chain generation.
//
// A texture with one level aliases on anything seen at a grazing angle, and the
// container has always been able to carry a chain -- nothing generated one. The
// interesting properties are that the chain is complete and correctly sized,
// that a non-square and a non-power-of-two texture both terminate, and that the
// colour space is respected: averaging sRGB-encoded texels as if they were light
// darkens every level, which is visible as a surface that dims as it recedes.
// ---------------------------------------------------------------------------
namespace
{
    CnbTextureData SolidRgba8(std::uint32_t width, std::uint32_t height, std::uint8_t value)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u, value);
        return CNA::Content::Cnb::MakeRgba8Texture2DData(width, height, std::move(pixels));
    }
}

TEST(CnbTextureCodecTest, GeneratedMipChainIsCompleteAndCorrectlySized)
{
    CnbTextureData data = SolidRgba8(8u, 8u, 128u);
    CNA::Content::Cnb::GenerateRgba8MipChain(data);

    EXPECT_EQ(data.mipCount, 4u);   // 8, 4, 2, 1
    ASSERT_EQ(data.representations.size(), 1u);
    ASSERT_EQ(data.representations.front().levels.size(), 4u);
    for (std::uint32_t level = 0u; level < data.mipCount; ++level)
    {
        std::uint32_t w = 0u, h = 0u, d = 0u;
        CNA::Content::Cnb::CnbTextureLevelDimensions(data, level, w, h, d);
        EXPECT_EQ(data.representations.front().levels[level].size(),
                  static_cast<std::size_t>(w) * h * 4u)
            << "level " << level;
    }
}

TEST(CnbTextureCodecTest, GeneratedMipChainHandlesNonSquareAndNonPowerOfTwo)
{
    // 1 x N is the case that reads off the end if the already-collapsed dimension
    // is not clamped, and 6 x 3 is the case where halving is not exact.
    for (const auto& size : {std::pair<std::uint32_t, std::uint32_t>{1u, 16u},
                             std::pair<std::uint32_t, std::uint32_t>{16u, 1u},
                             std::pair<std::uint32_t, std::uint32_t>{6u, 3u},
                             std::pair<std::uint32_t, std::uint32_t>{1u, 1u}})
    {
        CnbTextureData data = SolidRgba8(size.first, size.second, 200u);
        CNA::Content::Cnb::GenerateRgba8MipChain(data);
        std::uint32_t w = 0u, h = 0u, d = 0u;
        CNA::Content::Cnb::CnbTextureLevelDimensions(data, data.mipCount - 1u, w, h, d);
        EXPECT_EQ(w, 1u);
        EXPECT_EQ(h, 1u);
        // A flat texture stays flat all the way down, whatever the shape.
        for (const std::uint8_t byte : data.representations.front().levels.back())
        {
            EXPECT_EQ(byte, 200u);
        }
    }
}

TEST(CnbTextureCodecTest, MipChainAveragesLinearlyByDefault)
{
    // Two black texels and two white ones: the linear average of the stored
    // values is 127 or 128, whatever the transfer function of the source.
    std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0u);
    for (std::size_t i = 0u; i < 4u; ++i) pixels[i] = 255u;            // texel (0,0)
    for (std::size_t i = 4u; i < 8u; ++i) pixels[i] = 255u;            // texel (1,0)
    CnbTextureData data = CNA::Content::Cnb::MakeRgba8Texture2DData(2u, 2u, std::move(pixels));
    CNA::Content::Cnb::GenerateRgba8MipChain(data,
                                             CNA::Content::Cnb::CnbMipColorSpace::Linear);
    ASSERT_EQ(data.mipCount, 2u);
    const std::vector<std::uint8_t>& level1 = data.representations.front().levels[1];
    ASSERT_EQ(level1.size(), 4u);
    EXPECT_NEAR(static_cast<double>(level1[0]), 128.0, 1.0);
}

TEST(CnbTextureCodecTest, MipChainInSrgbAveragesLightRatherThanCode)
{
    // Half black, half white, averaged as light: 0.5 in linear is 188 in sRGB,
    // not 128. Getting this wrong is the classic mip-darkening artefact, and it
    // is why the colour space is an argument rather than an assumption.
    std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0u);
    for (std::size_t i = 0u; i < 8u; ++i) pixels[i] = 255u;
    CnbTextureData data = CNA::Content::Cnb::MakeRgba8Texture2DData(2u, 2u, std::move(pixels));
    CNA::Content::Cnb::GenerateRgba8MipChain(data, CNA::Content::Cnb::CnbMipColorSpace::Srgb);
    const std::vector<std::uint8_t>& level1 = data.representations.front().levels[1];
    EXPECT_NEAR(static_cast<double>(level1[0]), 188.0, 2.0);
    // Alpha is coverage, never a colour, so it is averaged linearly even here.
    EXPECT_NEAR(static_cast<double>(level1[3]), 128.0, 1.0);
}

TEST(CnbTextureCodecTest, GeneratedMipChainSurvivesAnEncodeDecodeRoundTrip)
{
    CnbTextureData data = SolidRgba8(16u, 8u, 77u);
    CNA::Content::Cnb::GenerateRgba8MipChain(data);
    const std::vector<std::uint8_t> encoded = EncodeTexture2DToCnb(data, "mipped");
    const CnbTextureData decoded = DecodeTexture2DFromCnb(CnbDocument::Parse(encoded, "mipped.cnb"));

    EXPECT_EQ(decoded.mipCount, data.mipCount);
    ASSERT_EQ(decoded.representations.size(), 1u);
    EXPECT_EQ(decoded.representations.front().levels, data.representations.front().levels);
}

TEST(CnbTextureCodecTest, GenerateMipChainRefusesATextureThatAlreadyHasOne)
{
    CnbTextureData data = SolidRgba8(4u, 4u, 10u);
    CNA::Content::Cnb::GenerateRgba8MipChain(data);
    EXPECT_THROW(CNA::Content::Cnb::GenerateRgba8MipChain(data),
                 Microsoft::Xna::Framework::Content::ContentLoadException);
}
