// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-105: opt-in per-chunk compression.
//
// The measurement that justified this is in docs/cnb-compression-measurements.md. What is tested
// here is the part a benchmark cannot check: that turning it on changes nothing a reader can
// observe, that leaving it off changes nothing at all, and that a hostile compressed chunk is
// refused before it can allocate anything.

#include <cstdint>
#include <gtest/gtest.h>
#include <algorithm>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;
using CNA::Content::Cnb::CnbCompression;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::MakeChunkId;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace
{
    /// Asked at RUNTIME, not through a macro. `CNA_CNB_HAVE_ZSTD` is private to the content
    /// library, and deliberately so: a consumer must not have to know how CNA was configured to
    /// find out what it can do. IsCnbCompressionSupported() is the public contract, and using it
    /// here is what proves that contract is usable from outside the library.
    bool HasZstd()
    {
        return CNA::Content::Cnb::IsCnbCompressionSupported(CnbCompression::ReservedZstd);
    }

    /// A payload with the redundancy real geometry has -- a repeating float-ish pattern -- so
    /// compression genuinely shrinks it. Random bytes would not compress and would silently turn
    /// every "was it compressed?" assertion into a tautology.
    std::vector<std::uint8_t> CompressiblePayload(std::size_t bytes = 64u * 1024u)
    {
        std::vector<std::uint8_t> data(bytes);
        for (std::size_t i = 0; i < bytes; ++i)
        {
            data[i] = static_cast<std::uint8_t>((i / 7u) % 23u);
        }
        return data;
    }

    std::vector<std::uint8_t> BuildDocument(CnbCompression codec,
                                            const std::vector<std::uint8_t>& payload)
    {
        CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Curve, 1u);
        writer.SetMetadata("Microsoft.Xna.Framework.Curve", "compressed");
        if (codec != CnbCompression::None) { writer.SetCompression(codec, 3); }
        writer.AddChunk(MakeChunkId('C', 'R', 'V', 'H'), {1, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0},
                        CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(MakeChunkId('C', 'R', 'V', 'K'), payload, CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }
}

TEST(CnbChunkCompressionTest, CompressionIsOffByDefaultAndChangesNoByte)
{
    // The default has to be provably inert: every golden vector in the project depends on it, and
    // this states the property directly rather than leaving it implied by those.
    const std::vector<std::uint8_t> payload = CompressiblePayload(1024u);
    const std::vector<std::uint8_t> a = BuildDocument(CnbCompression::None, payload);

    CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Curve, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Curve", "compressed");
    writer.SetCompression(CnbCompression::None); // explicitly the default
    writer.AddChunk(MakeChunkId('C', 'R', 'V', 'H'), {1, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0},
                    CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(MakeChunkId('C', 'R', 'V', 'K'), payload, CnbChunkFlags::Mandatory, 4u);
    EXPECT_EQ(a, writer.Build());

    const CnbDocument document = CnbDocument::Parse(a, "plain.cnb");
    for (std::size_t i = 0; i < document.ChunkCount(); ++i)
    {
        EXPECT_EQ(document.ChunkAt(i).compression, CnbCompression::None);
        EXPECT_EQ(document.ChunkAt(i).storedSize, document.ChunkAt(i).uncompressedSize);
    }
}

TEST(CnbChunkCompressionTest, ACompressedDocumentIsSmallerAndReadsBackIdentically)
{
    if (!HasZstd()) { GTEST_SKIP() << "this build has no Zstandard codec"; }
    const std::vector<std::uint8_t> payload = CompressiblePayload();

    const std::vector<std::uint8_t> plain = BuildDocument(CnbCompression::None, payload);
    const std::vector<std::uint8_t> packed =
        BuildDocument(CnbCompression::ReservedZstd, payload);
    EXPECT_LT(packed.size(), plain.size())
        << "a compressible payload must actually shrink the file";

    const CnbDocument document = CnbDocument::Parse(packed, "packed.cnb");
    const std::size_t keys = document.RequireSingle(MakeChunkId('C', 'R', 'V', 'K'));
    EXPECT_EQ(document.ChunkAt(keys).compression, CnbCompression::ReservedZstd);
    EXPECT_LT(document.ChunkAt(keys).storedSize, document.ChunkAt(keys).uncompressedSize);
    EXPECT_EQ(document.ChunkAt(keys).uncompressedSize, payload.size());

    // The point of expanding at parse time: ChunkData() means the same thing either way, so no
    // caller anywhere had to learn about the codec.
    const std::span<const std::uint8_t> data = document.ChunkData(keys);
    ASSERT_EQ(data.size(), payload.size());
    EXPECT_TRUE(std::equal(data.begin(), data.end(), payload.begin()));
}

TEST(CnbChunkCompressionTest, AChunkThatWouldGrowIsStoredInstead)
{
    if (!HasZstd()) { GTEST_SKIP() << "this build has no Zstandard codec"; }
    // Incompressible input: compressing it costs bytes AND decompression time, so the writer must
    // decline per chunk rather than applying the codec because it was asked to.
    std::vector<std::uint8_t> noise(4096u);
    std::uint32_t state = 0x12345678u;
    for (std::uint8_t& byte : noise)
    {
        state = state * 1664525u + 1013904223u; // a fixed LCG: deterministic, not random
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    const CnbDocument document =
        CnbDocument::Parse(BuildDocument(CnbCompression::ReservedZstd, noise), "noise.cnb");
    const std::size_t keys = document.RequireSingle(MakeChunkId('C', 'R', 'V', 'K'));
    EXPECT_EQ(document.ChunkAt(keys).compression, CnbCompression::None)
        << "a chunk that did not shrink must be stored, not compressed";
    EXPECT_EQ(document.ChunkAt(keys).storedSize, noise.size());
}

TEST(CnbChunkCompressionTest, ContainerChunksAreNeverCompressed)
{
    if (!HasZstd()) { GTEST_SKIP() << "this build has no Zstandard codec"; }
    // An inspector must be able to read a file's identity without the codec being available, so
    // CMET and XREF stay stored however the writer is configured.
    const CnbDocument document = CnbDocument::Parse(
        BuildDocument(CnbCompression::ReservedZstd, CompressiblePayload()), "packed.cnb");
    const std::size_t metadata =
        document.RequireSingle(CNA::Content::Cnb::CnbContainerChunk::Metadata);
    EXPECT_EQ(document.ChunkAt(metadata).compression, CnbCompression::None);
    EXPECT_EQ(document.Metadata().assetTypeName, "Microsoft.Xna.Framework.Curve");
}

TEST(CnbChunkCompressionTest, ACompressedCurveStillDecodesThroughItsOwnCodec)
{
    if (!HasZstd()) { GTEST_SKIP() << "this build has no Zstandard codec"; }
    // End to end through a real schema, because "the bytes came back" and "the asset loaded" are
    // different claims.
    Microsoft::Xna::Framework::Curve curve;
    for (int i = 0; i < 200; ++i)
    {
        curve.getKeysProperty().Add(Microsoft::Xna::Framework::CurveKey(
            static_cast<float>(i), static_cast<float>(i % 7)));
    }
    const std::vector<std::uint8_t> plain =
        CNA::Content::Cnb::EncodeCurveToCnb(curve, "packed/curve");

    // Re-emit the same chunks through a compressing writer.
    const CnbDocument source = CnbDocument::Parse(plain, "plain.cnb");
    CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Curve, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Curve", "packed/curve");
    writer.SetCompression(CnbCompression::ReservedZstd, 3);
    for (std::size_t i = 0; i < source.ChunkCount(); ++i)
    {
        const auto& entry = source.ChunkAt(i);
        if (entry.type == CNA::Content::Cnb::CnbContainerChunk::Metadata) { continue; }
        const auto data = source.ChunkData(i);
        writer.AddChunk(entry.type, std::vector<std::uint8_t>(data.begin(), data.end()),
                        entry.flags, entry.alignment);
    }
    const std::vector<std::uint8_t> packed = writer.Build();
    EXPECT_LT(packed.size(), plain.size());

    const Microsoft::Xna::Framework::Curve decoded =
        CNA::Content::Cnb::DecodeCurveFromCnb(CnbDocument::Parse(packed, "packed.cnb"));
    ASSERT_EQ(decoded.getKeysProperty().getCountProperty(), 200);
    EXPECT_FLOAT_EQ(decoded.getKeysProperty()[199].getPositionProperty(), 199.0f);
}

TEST(CnbChunkCompressionTest, AnAbsurdUnpackedSizeIsRefusedBeforeAnythingIsAllocated)
{
    if (!HasZstd()) { GTEST_SKIP() << "this build has no Zstandard codec"; }
    // The zip-bomb case: a handful of stored bytes claiming to expand to gigabytes. The ceiling
    // has to be checked against the DECLARED size, before the allocation -- checking after the
    // decode would be checking after the damage.
    const std::vector<std::uint8_t> tiny =
        CNA::Content::Cnb::CompressCnbChunk(std::vector<std::uint8_t>(64u, 7u),
                                             CnbCompression::ReservedZstd, 3);
    try
    {
        (void)CNA::Content::Cnb::DecompressCnbChunk(tiny, CnbCompression::ReservedZstd,
                                                     8ull * 1024ull * 1024ull * 1024ull,
                                                     384ull * 1024ull * 1024ull, "hostile chunk");
        FAIL() << "an unpacked size above the limit must be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("before allocating"), std::string::npos) << e.what();
    }
}

TEST(CnbChunkCompressionTest, AStreamThatExpandsToTheWrongLengthIsCorrupt)
{
    if (!HasZstd()) { GTEST_SKIP() << "this build has no Zstandard codec"; }
    // Accepting a short expansion would leave the tail of the buffer as zeroes that later code
    // reads as data -- silently wrong rather than refused.
    const std::vector<std::uint8_t> packed =
        CNA::Content::Cnb::CompressCnbChunk(std::vector<std::uint8_t>(1000u, 3u),
                                             CnbCompression::ReservedZstd, 3);
    EXPECT_THROW((void)CNA::Content::Cnb::DecompressCnbChunk(packed,
                                                              CnbCompression::ReservedZstd, 999u,
                                                              1024u * 1024u, "wrong size"),
                 ContentLoadException);
    EXPECT_NO_THROW((void)CNA::Content::Cnb::DecompressCnbChunk(
        packed, CnbCompression::ReservedZstd, 1000u, 1024u * 1024u, "right size"));
}

TEST(CnbChunkCompressionTest, AnUnimplementedCodecIsRefusedByName)
{
    // LZ4 and Deflate keep their reserved identifiers and no implementation. A file using one must
    // say which codec it wanted, so the answer is "build CNA with it", not "the file is broken".
    EXPECT_FALSE(CNA::Content::Cnb::IsCnbCompressionSupported(CnbCompression::ReservedLz4));
    EXPECT_FALSE(CNA::Content::Cnb::IsCnbCompressionSupported(CnbCompression::ReservedDeflate));
    EXPECT_TRUE(CNA::Content::Cnb::IsCnbCompressionSupported(CnbCompression::None));
    // Zstandard's availability is a build configuration, so this asserts the query is coherent
    // with itself rather than against a macro the test cannot see.
    const bool zstd = CNA::Content::Cnb::IsCnbCompressionSupported(CnbCompression::ReservedZstd);
    EXPECT_EQ(zstd, HasZstd());

    CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Curve, 1u);
    EXPECT_THROW(writer.SetCompression(CnbCompression::ReservedLz4), std::invalid_argument);

    const std::vector<std::uint8_t> payload = CompressiblePayload(256u);
    EXPECT_THROW((void)CNA::Content::Cnb::CompressCnbChunk(payload, CnbCompression::ReservedLz4),
                 ContentLoadException);
}
