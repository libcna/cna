// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-020..CNBF-033 (Phase A tests): the CNB container's positive contract and,
// far more importantly, one dedicated negative test per container invariant listed in
// plans/plan_cnb.md §4. Every negative test builds a *valid* file first and then breaks exactly one
// invariant, so "the reader rejected it" can never be confused with "the fixture was never valid".
// Where breaking an invariant would also invalidate a checksum, the affected checksums are
// recomputed, so the test really does exercise the invariant it names rather than the checksum
// check standing in front of it.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
using CNA::Content::Cnb::CnbByteReader;
using CNA::Content::Cnb::CnbByteWriter;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;
using CNA::Content::Cnb::CnbChunkId;
using CNA::Content::Cnb::CnbCompression;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbExternalReference;
using CNA::Content::Cnb::CnbReadLimits;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::Crc32c;
using CNA::Content::Cnb::MakeChunkId;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace Format = CNA::Content::Cnb::Format;

namespace
{
    // Header field offsets, spelled out rather than derived, so that a change to the layout has to
    // break this test explicitly instead of silently following along.
    constexpr std::size_t kOffMagic = 0;
    constexpr std::size_t kOffMajor = 4;
    constexpr std::size_t kOffMinor = 6;
    constexpr std::size_t kOffHeaderFlags = 8;
    constexpr std::size_t kOffAssetType = 12;
    constexpr std::size_t kOffSchema = 16;
    constexpr std::size_t kOffChunkCount = 20;
    constexpr std::size_t kOffFileSize = 24;
    constexpr std::size_t kOffTocOffset = 32;
    constexpr std::size_t kOffTocChecksum = 40;
    constexpr std::size_t kOffHeaderChecksum = 44;
    constexpr std::size_t kOffReserved = 48;

    // Table-of-contents entry field offsets, relative to the entry's own start.
    constexpr std::size_t kEntType = 0;
    constexpr std::size_t kEntFlags = 4;
    constexpr std::size_t kEntOffset = 8;
    constexpr std::size_t kEntStored = 16;
    constexpr std::size_t kEntUnpacked = 24;
    constexpr std::size_t kEntChecksum = 32;
    constexpr std::size_t kEntCompression = 36;
    constexpr std::size_t kEntAlignment = 40;
    constexpr std::size_t kEntReserved = 44;

    const CnbChunkId kAlpha = MakeChunkId('A', 'L', 'F', 'A');
    const CnbChunkId kBeta = MakeChunkId('B', 'E', 'T', 'A');
    const CnbChunkId kGamma = MakeChunkId('G', 'A', 'M', 'A');

    std::vector<std::uint8_t> Bytes(std::initializer_list<int> values)
    {
        std::vector<std::uint8_t> out;
        out.reserve(values.size());
        for (const int v : values) { out.push_back(static_cast<std::uint8_t>(v)); }
        return out;
    }

    /// A mutable byte image of a .cnb file plus the small amount of surgery the negative tests
    /// need. Patch<N> writes a little-endian value; the Fix* helpers put the checksums back so a
    /// test can aim at the invariant it actually cares about.
    struct RawCnb
    {
        std::vector<std::uint8_t> bytes;

        void PatchU16(std::size_t offset, std::uint16_t value)
        {
            bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
            bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
        }
        void PatchU32(std::size_t offset, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                bytes[offset + static_cast<std::size_t>(i)] =
                    static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
            }
        }
        void PatchU64(std::size_t offset, std::uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                bytes[offset + static_cast<std::size_t>(i)] =
                    static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
            }
        }
        [[nodiscard]] std::uint32_t ReadU32(std::size_t offset) const
        {
            std::uint32_t v = 0;
            for (int i = 3; i >= 0; --i)
            {
                v = (v << 8) | bytes[offset + static_cast<std::size_t>(i)];
            }
            return v;
        }
        [[nodiscard]] std::uint64_t ReadU64(std::size_t offset) const
        {
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
            {
                v = (v << 8) | bytes[offset + static_cast<std::size_t>(i)];
            }
            return v;
        }
        [[nodiscard]] std::size_t EntryOffset(std::size_t index) const
        {
            return static_cast<std::size_t>(ReadU64(kOffTocOffset)) +
                   index * Format::TocEntrySize;
        }
        void FixChunkChecksum(std::size_t index)
        {
            const std::size_t entry = EntryOffset(index);
            const auto off = static_cast<std::size_t>(ReadU64(entry + kEntOffset));
            const auto size = static_cast<std::size_t>(ReadU64(entry + kEntStored));
            PatchU32(entry + kEntChecksum,
                     Crc32c(std::span<const std::uint8_t>(bytes).subspan(off, size)));
        }
        void FixTocChecksum()
        {
            const auto tocOffset = static_cast<std::size_t>(ReadU64(kOffTocOffset));
            const std::size_t tocSize =
                static_cast<std::size_t>(ReadU32(kOffChunkCount)) * Format::TocEntrySize;
            PatchU32(kOffTocChecksum,
                     Crc32c(std::span<const std::uint8_t>(bytes).subspan(tocOffset, tocSize)));
        }
        void FixHeaderChecksum()
        {
            PatchU32(kOffHeaderChecksum,
                     Crc32c(std::span<const std::uint8_t>(bytes).first(
                         Format::HeaderChecksumCoverage)));
        }
        void FixStructuralChecksums()
        {
            FixTocChecksum();
            FixHeaderChecksum();
        }
    };

    /// The shared valid fixture: three chunks, one of them mandatory, one of them 64-byte aligned
    /// so alignment padding really exists in the file.
    RawCnb MakeValidFile()
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(kAlpha, Bytes({1, 2, 3, 4, 5}), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(kBeta, Bytes({9, 8, 7}), CnbChunkFlags::None, 64u);
        writer.AddChunk(kGamma, Bytes({0x41, 0x42}), CnbChunkFlags::None, 4u);
        return RawCnb{writer.Build()};
    }

    CnbDocument ParseRaw(const RawCnb& raw)
    {
        return CnbDocument::Parse(raw.bytes, "fixture.cnb");
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-020 -- CRC-32C known answers
// --------------------------------------------------------------------------------------------

TEST(CnbCrc32cTest, MatchesPublishedKnownAnswers)
{
    // The canonical CRC-32C check value, published with the algorithm's parameters.
    const std::string check = "123456789";
    EXPECT_EQ(Crc32c(std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t*>(check.data()), check.size())),
              0xE3069283u);

    // Empty input is the identity: initial register, inverted twice.
    EXPECT_EQ(Crc32c(std::span<const std::uint8_t>{}), 0x00000000u);

    // A single zero byte and a 32-byte zero run, both independently derivable from the
    // reflected-table definition.
    const std::vector<std::uint8_t> oneZero(1, 0u);
    EXPECT_EQ(Crc32c(oneZero), 0x527D5351u);

    const std::vector<std::uint8_t> thirtyTwoZeros(32, 0u);
    EXPECT_EQ(Crc32c(thirtyTwoZeros), 0x8A9136AAu);
}

TEST(CnbCrc32cTest, ContinuationMatchesWholeBufferComputation)
{
    std::vector<std::uint8_t> all;
    for (int i = 0; i < 300; ++i) { all.push_back(static_cast<std::uint8_t>(i * 7 + 3)); }

    const std::uint32_t whole = Crc32c(all);
    const std::uint32_t split = CNA::Content::Cnb::Crc32cContinue(
        CNA::Content::Cnb::Crc32cContinue(CNA::Content::Cnb::Crc32cSeed(),
                                          std::span<const std::uint8_t>(all).first(101)),
        std::span<const std::uint8_t>(all).subspan(101));
    EXPECT_EQ(whole, split);
}

// --------------------------------------------------------------------------------------------
// CNBF-021 -- round trip, chunk lookup, alignment
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, ValidFileRoundTripsThroughTheReader)
{
    const RawCnb raw = MakeValidFile();
    const CnbDocument doc = ParseRaw(raw);

    EXPECT_EQ(doc.ContainerMajor(), Format::ContainerMajor);
    EXPECT_EQ(doc.ContainerMinor(), Format::ContainerMinor);
    EXPECT_EQ(doc.AssetTypeId(), CnbAssetTypeId::Curve);
    EXPECT_EQ(doc.AssetSchemaVersion(), 1u);
    ASSERT_EQ(doc.ChunkCount(), 3u);

    EXPECT_EQ(doc.ChunkAt(0).type, kAlpha);
    EXPECT_TRUE(doc.ChunkAt(0).IsMandatory());
    EXPECT_FALSE(doc.ChunkAt(1).IsMandatory());
    EXPECT_EQ(doc.ChunkAt(1).alignment, 64u);
    EXPECT_EQ(doc.ChunkAt(1).offset % 64u, 0u);
    EXPECT_EQ(doc.ChunkAt(0).compression, CnbCompression::None);
    EXPECT_EQ(doc.ChunkAt(0).storedSize, doc.ChunkAt(0).uncompressedSize);

    const std::span<const std::uint8_t> alpha = doc.ChunkData(0);
    ASSERT_EQ(alpha.size(), 5u);
    EXPECT_EQ(alpha[0], 1u);
    EXPECT_EQ(alpha[4], 5u);

    EXPECT_EQ(doc.RequireSingle(kBeta), 1u);
    EXPECT_EQ(doc.FindAll(kGamma).size(), 1u);
    EXPECT_FALSE(doc.FindSingle(MakeChunkId('N', 'O', 'P', 'E')).has_value());
}

TEST(CnbContainerTest, HeaderIsExactlySixtyFourBytesAndTocFollowsIt)
{
    const RawCnb raw = MakeValidFile();
    EXPECT_EQ(Format::HeaderSize, 64u);
    EXPECT_EQ(Format::TocEntrySize, 48u);
    EXPECT_EQ(raw.ReadU64(kOffTocOffset), 64u);
    EXPECT_EQ(raw.ReadU64(kOffFileSize), raw.bytes.size());
    EXPECT_EQ(raw.bytes[0], 0x43u);
    EXPECT_EQ(raw.bytes[1], 0x4Eu);
    EXPECT_EQ(raw.bytes[2], 0x42u);
    EXPECT_EQ(raw.bytes[3], 0x1Au);
}

TEST(CnbContainerTest, EmptyChunkListIsValid)
{
    const CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    const std::vector<std::uint8_t> bytes = writer.Build();
    EXPECT_EQ(bytes.size(), Format::HeaderSize);

    const CnbDocument doc = CnbDocument::Parse(bytes, "empty.cnb");
    EXPECT_EQ(doc.ChunkCount(), 0u);
    EXPECT_FALSE(doc.Metadata().present);
    EXPECT_TRUE(doc.ExternalReferences().empty());
}

TEST(CnbContainerTest, ZeroLengthChunkIsValid)
{
    CnbWriter writer(CnbAssetTypeId::AnimationClip, 1u);
    writer.AddChunk(kAlpha, {}, CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(kBeta, Bytes({7}), CnbChunkFlags::None, 4u);

    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "zero.cnb");
    ASSERT_EQ(doc.ChunkCount(), 2u);
    EXPECT_EQ(doc.ChunkData(0).size(), 0u);
    EXPECT_EQ(doc.ChunkData(1).size(), 1u);
}

TEST(CnbContainerTest, ChunkAccessorsRejectAnOutOfRangeIndex)
{
    const CnbDocument doc = ParseRaw(MakeValidFile());
    EXPECT_THROW((void)doc.ChunkAt(3), ContentLoadException);
    EXPECT_THROW((void)doc.ChunkData(99), ContentLoadException);
    EXPECT_THROW((void)doc.OpenChunk(3), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-022 -- truncation
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, TruncatedHeaderIsRejected)
{
    for (std::size_t keep : {std::size_t{0}, std::size_t{3}, std::size_t{4}, std::size_t{63}})
    {
        RawCnb raw = MakeValidFile();
        raw.bytes.resize(keep);
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException) << "keeping " << keep << " bytes";
    }
}

TEST(CnbContainerTest, TruncatedTableOfContentsIsRejected)
{
    RawCnb raw = MakeValidFile();
    // Keep the header and part of the first entry only.
    raw.bytes.resize(Format::HeaderSize + 20u);
    raw.PatchU64(kOffFileSize, raw.bytes.size());
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, TruncatedChunkPayloadIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.bytes.resize(raw.bytes.size() - 1u);
    raw.PatchU64(kOffFileSize, raw.bytes.size());
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-023 -- magic, versions, reserved fields
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, BadMagicIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.bytes[kOffMagic + 2] = 'X';
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
    EXPECT_FALSE(CnbDocument::HasMagic(raw.bytes));
}

TEST(CnbContainerTest, UnsupportedContainerMajorVersionIsRejected)
{
    for (const std::uint16_t major : {std::uint16_t{0}, std::uint16_t{2}, std::uint16_t{65535}})
    {
        RawCnb raw = MakeValidFile();
        raw.PatchU16(kOffMajor, major);
        raw.FixHeaderChecksum();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException) << "major " << major;
    }
}

TEST(CnbContainerTest, HigherContainerMinorVersionIsAcceptedBecauseMinorBumpsAreAdditive)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU16(kOffMinor, 7u);
    raw.FixHeaderChecksum();

    const CnbDocument doc = ParseRaw(raw);
    EXPECT_EQ(doc.ContainerMinor(), 7u);
    EXPECT_EQ(doc.ChunkCount(), 3u);
}

TEST(CnbContainerTest, NonZeroHeaderFlagsAreRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(kOffHeaderFlags, 0x00000001u);
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, NonZeroReservedHeaderBytesAreRejected)
{
    for (std::size_t i = 0; i < Format::HeaderReservedSize; ++i)
    {
        RawCnb raw = MakeValidFile();
        raw.bytes[kOffReserved + i] = 0xABu;
        raw.FixHeaderChecksum();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException) << "reserved byte " << i;
    }
}

TEST(CnbContainerTest, AssetTypeZeroAndSchemaVersionZeroAreRejected)
{
    {
        RawCnb raw = MakeValidFile();
        raw.PatchU32(kOffAssetType, 0u);
        raw.FixHeaderChecksum();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
    }
    {
        RawCnb raw = MakeValidFile();
        raw.PatchU32(kOffSchema, 0u);
        raw.FixHeaderChecksum();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
    }
}

TEST(CnbContainerTest, DeclaredFileSizeMustMatchTheRealOne)
{
    for (const std::int64_t delta : {std::int64_t{-1}, std::int64_t{1}, std::int64_t{1 << 20}})
    {
        RawCnb raw = MakeValidFile();
        raw.PatchU64(kOffFileSize,
                     static_cast<std::uint64_t>(static_cast<std::int64_t>(raw.bytes.size()) + delta));
        raw.FixHeaderChecksum();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException) << "delta " << delta;
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-024 -- checksums
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, CorruptHeaderChecksumIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(kOffHeaderChecksum, raw.ReadU32(kOffHeaderChecksum) ^ 1u);
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, CorruptTableOfContentsChecksumIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(kOffTocChecksum, raw.ReadU32(kOffTocChecksum) ^ 0x80u);
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, CorruptChunkPayloadIsCaughtByItsChecksum)
{
    RawCnb raw = MakeValidFile();
    const auto payloadOffset = static_cast<std::size_t>(
        raw.ReadU64(raw.EntryOffset(0) + kEntOffset));
    raw.bytes[payloadOffset + 2] ^= 0xFFu;
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, CorruptChunkChecksumFieldIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(raw.EntryOffset(1) + kEntChecksum, 0xDEADBEEFu);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-025 -- offset arithmetic, bounds, alignment
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, ChunkOffsetPlusSizeOverflowIsRejectedRatherThanWrappingAround)
{
    RawCnb raw = MakeValidFile();
    const std::size_t entry = raw.EntryOffset(0);
    // Two enormous values whose 64-bit sum wraps to a small number: exactly the shape that
    // defeats a naive `offset + size <= fileSize` check.
    raw.PatchU64(entry + kEntOffset, 0xFFFFFFFFFFFFFF00ull);
    raw.PatchU64(entry + kEntStored, 0x0000000000000200ull);
    raw.PatchU64(entry + kEntUnpacked, 0x0000000000000200ull);
    raw.PatchU32(entry + kEntAlignment, 1u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, ChunkEndingPastEndOfFileIsRejected)
{
    RawCnb raw = MakeValidFile();
    const std::size_t entry = raw.EntryOffset(2);
    raw.PatchU64(entry + kEntStored, raw.bytes.size() + 16u);
    raw.PatchU64(entry + kEntUnpacked, raw.bytes.size() + 16u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, TableOfContentsEndingPastEndOfFileIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(kOffChunkCount, 4096u);
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, TableOfContentsOverlappingTheHeaderIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU64(kOffTocOffset, 32u);
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, MisalignedChunkOffsetIsRejected)
{
    RawCnb raw = MakeValidFile();
    const std::size_t entry = raw.EntryOffset(1);
    // Chunk 1 declares 64-byte alignment; claim an offset one byte off.
    raw.PatchU64(entry + kEntOffset, raw.ReadU64(entry + kEntOffset) + 1u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, InvalidChunkAlignmentIsRejected)
{
    for (const std::uint32_t alignment : {0u, 3u, 12u, 8192u})
    {
        RawCnb raw = MakeValidFile();
        raw.PatchU32(raw.EntryOffset(0) + kEntAlignment, alignment);
        raw.FixStructuralChecksums();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException) << "alignment " << alignment;
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-026 -- coverage: overlap, ordering, padding, trailing bytes
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, OverlappingChunksAreRejected)
{
    RawCnb raw = MakeValidFile();
    const std::size_t entry2 = raw.EntryOffset(2);
    const std::uint64_t chunk1Offset = raw.ReadU64(raw.EntryOffset(1) + kEntOffset);
    raw.PatchU64(entry2 + kEntOffset, chunk1Offset);
    raw.PatchU32(entry2 + kEntAlignment, 1u);
    raw.FixChunkChecksum(2);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, NonMonotonicTableOfContentsIsRejected)
{
    // Build a file whose two entries are laid out in ascending order, then swap their table rows
    // (payloads untouched) so the table itself is out of order.
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(kAlpha, Bytes({1, 1, 1, 1}), CnbChunkFlags::None, 4u);
    writer.AddChunk(kBeta, Bytes({2, 2, 2, 2}), CnbChunkFlags::None, 4u);
    RawCnb raw{writer.Build()};

    const std::size_t e0 = raw.EntryOffset(0);
    const std::size_t e1 = raw.EntryOffset(1);
    std::vector<std::uint8_t> tmp(Format::TocEntrySize);
    std::memcpy(tmp.data(), raw.bytes.data() + e0, Format::TocEntrySize);
    std::memcpy(raw.bytes.data() + e0, raw.bytes.data() + e1, Format::TocEntrySize);
    std::memcpy(raw.bytes.data() + e1, tmp.data(), Format::TocEntrySize);
    raw.FixStructuralChecksums();

    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, NonZeroAlignmentPaddingIsRejected)
{
    RawCnb raw = MakeValidFile();
    // Chunk 1 is 64-byte aligned, so there is real padding between chunk 0's end and it.
    const std::uint64_t chunk0End = raw.ReadU64(raw.EntryOffset(0) + kEntOffset) +
                                    raw.ReadU64(raw.EntryOffset(0) + kEntStored);
    const std::uint64_t chunk1Begin = raw.ReadU64(raw.EntryOffset(1) + kEntOffset);
    ASSERT_GT(chunk1Begin, chunk0End) << "fixture must actually contain padding";

    raw.bytes[static_cast<std::size_t>(chunk0End)] = 0x55u;
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, TrailingBytesAfterTheLastChunkAreRejected)
{
    RawCnb raw = MakeValidFile();
    raw.bytes.push_back(0u);
    raw.bytes.push_back(0x99u);
    raw.PatchU64(kOffFileSize, raw.bytes.size());
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, TrailingZeroBytesAreToleratedAsEndOfFilePadding)
{
    // Deliberate, and asserted rather than left implicit: zero-filled bytes after the last chunk
    // are indistinguishable from alignment padding, and tolerating them keeps the door open for a
    // future writer that pads a file out to a page boundary for memory mapping. Anything NON-zero
    // out there is still rejected -- that is the test above.
    RawCnb raw = MakeValidFile();
    raw.bytes.push_back(0u);
    raw.PatchU64(kOffFileSize, raw.bytes.size());
    raw.FixHeaderChecksum();
    const CnbDocument doc = ParseRaw(raw);
    EXPECT_EQ(doc.ChunkCount(), 3u);
}

// --------------------------------------------------------------------------------------------
// CNBF-027 -- limits
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, ChunkCountAboveTheConfiguredLimitIsRejectedBeforeAnyAllocation)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(kOffChunkCount, 0x7FFFFFFFu);
    raw.FixHeaderChecksum();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, FileSizeAboveTheConfiguredLimitIsRejected)
{
    CnbReadLimits tiny;
    tiny.maxFileSize = 16u;
    const RawCnb raw = MakeValidFile();
    EXPECT_THROW((void)CnbDocument::Parse(raw.bytes, "fixture.cnb", tiny), ContentLoadException);
}

TEST(CnbContainerTest, ChunkSizeAboveTheConfiguredLimitIsRejected)
{
    CnbReadLimits tiny;
    tiny.maxChunkSize = 2u;
    const RawCnb raw = MakeValidFile();
    EXPECT_THROW((void)CnbDocument::Parse(raw.bytes, "fixture.cnb", tiny), ContentLoadException);
}

TEST(CnbContainerTest, StringLengthAboveTheConfiguredLimitIsRejected)
{
    CnbByteWriter w;
    w.WriteString(std::string(64, 'x'));
    const std::vector<std::uint8_t> bytes = w.Take();

    CnbReadLimits tiny;
    tiny.maxStringBytes = 8u;
    CnbByteReader reader(bytes, "test", tiny);
    EXPECT_THROW((void)reader.ReadString(), ContentLoadException);
}

TEST(CnbContainerTest, ArrayCountAboveTheConfiguredLimitIsRejectedBeforeReserving)
{
    CnbByteWriter w;
    w.WriteU32(0x7FFFFFFFu);
    const std::vector<std::uint8_t> bytes = w.Take();

    CnbByteReader reader(bytes, "test");
    EXPECT_THROW((void)reader.ReadCount(4u, "things"), ContentLoadException);
}

TEST(CnbContainerTest, ArrayCountThatCannotFitInTheRemainingBytesIsRejected)
{
    CnbByteWriter w;
    w.WriteU32(1000u);
    w.WriteU32(1u); // only one element actually present
    const std::vector<std::uint8_t> bytes = w.Take();

    CnbByteReader reader(bytes, "test");
    EXPECT_THROW((void)reader.ReadCount(4u, "things"), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-028 -- unknown chunks
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, UnknownOptionalChunkIsSkipped)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(MakeChunkId('f', 'u', 't', 'r'), Bytes({9, 9, 9}), CnbChunkFlags::None, 4u);

    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "future.cnb");
    const CnbChunkId known[] = {kAlpha};
    EXPECT_NO_THROW(doc.RequireMandatoryChunksUnderstood(known));
}

TEST(CnbContainerTest, UnknownMandatoryChunkIsRejected)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(MakeChunkId('f', 'u', 't', 'r'), Bytes({9}), CnbChunkFlags::Mandatory, 4u);

    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "future.cnb");
    const CnbChunkId known[] = {kAlpha};
    EXPECT_THROW(doc.RequireMandatoryChunksUnderstood(known), ContentLoadException);
}

TEST(CnbContainerTest, ContainerLevelChunksNeverCountAsUnknownToASchema)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Curve", "Curves/wobble");
    writer.SetExternalReferences({CnbExternalReference{0u, CnbAssetTypeId::Texture2D,
                                                       "Textures/wall"}});
    writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::Mandatory, 4u);

    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "meta.cnb");
    const CnbChunkId known[] = {kAlpha};
    EXPECT_NO_THROW(doc.RequireMandatoryChunksUnderstood(known));

    ASSERT_TRUE(doc.Metadata().present);
    EXPECT_EQ(doc.Metadata().assetTypeName, "Microsoft.Xna.Framework.Curve");
    EXPECT_EQ(doc.Metadata().contentName, "Curves/wobble");
    ASSERT_EQ(doc.ExternalReferences().size(), 1u);
    EXPECT_EQ(doc.ExternalReferences()[0].logicalName, "Textures/wall");
    EXPECT_EQ(doc.ExternalReferences()[0].expectedAssetTypeId, CnbAssetTypeId::Texture2D);
}

TEST(CnbContainerTest, UnknownChunkFlagBitsAreRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(raw.EntryOffset(0) + kEntFlags, 0x00000002u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, UnknownCompressionCodecIsRejected)
{
    for (const std::uint32_t codec : {1u, 2u, 3u, 999u})
    {
        RawCnb raw = MakeValidFile();
        raw.PatchU32(raw.EntryOffset(0) + kEntCompression, codec);
        raw.FixStructuralChecksums();
        EXPECT_THROW((void)ParseRaw(raw), ContentLoadException) << "codec " << codec;
    }
}

TEST(CnbContainerTest, NonZeroReservedTableEntryFieldIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(raw.EntryOffset(1) + kEntReserved, 1u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, NonPrintableChunkIdentifierIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU32(raw.EntryOffset(0) + kEntType, 0x00414C41u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

TEST(CnbContainerTest, UncompressedSizeDisagreeingWithStoredSizeIsRejected)
{
    RawCnb raw = MakeValidFile();
    raw.PatchU64(raw.EntryOffset(0) + kEntUnpacked, 99u);
    raw.FixStructuralChecksums();
    EXPECT_THROW((void)ParseRaw(raw), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-029 -- duplicates
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, RepeatedChunkTypesAreLegalAndKeepFileOrder)
{
    CnbWriter writer(CnbAssetTypeId::Model, 1u);
    writer.AddChunk(kAlpha, Bytes({0}), CnbChunkFlags::None, 4u);
    writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::None, 4u);
    writer.AddChunk(kAlpha, Bytes({2}), CnbChunkFlags::None, 4u);

    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "repeat.cnb");
    const std::vector<std::size_t> all = doc.FindAll(kAlpha);
    ASSERT_EQ(all.size(), 3u);
    for (std::size_t i = 0; i < 3u; ++i)
    {
        EXPECT_EQ(doc.ChunkData(all[i])[0], static_cast<std::uint8_t>(i));
    }
}

TEST(CnbContainerTest, DuplicateSingletonChunkIsRejectedByRequireSingle)
{
    CnbWriter writer(CnbAssetTypeId::Model, 1u);
    writer.AddChunk(kAlpha, Bytes({0}), CnbChunkFlags::None, 4u);
    writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::None, 4u);

    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "dup.cnb");
    EXPECT_THROW((void)doc.RequireSingle(kAlpha), ContentLoadException);
    EXPECT_THROW((void)doc.FindSingle(kAlpha), ContentLoadException);
}

TEST(CnbContainerTest, MissingRequiredChunkIsRejectedByRequireSingle)
{
    const CnbDocument doc = ParseRaw(MakeValidFile());
    EXPECT_THROW((void)doc.RequireSingle(MakeChunkId('N', 'O', 'P', 'E')), ContentLoadException);
}

TEST(CnbContainerTest, DuplicateContainerMetadataChunkIsRejected)
{
    // Two CMET chunks cannot be produced by CnbWriter -- AddChunk refuses a container-defined
    // identifier outright (CNBF-115) -- so the second one is made by writing an ordinary chunk of
    // the same shape and retyping its table-of-contents entry. The point is to reach the READER's
    // singleton rule with a file that arrived from somewhere else.
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.SetMetadata("A", "B");
    writer.AddChunk(MakeChunkId('c', 'm', 'e', 't'), Bytes({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
                    CnbChunkFlags::None, 4u);
    RawCnb raw{writer.Build()};
    raw.PatchU32(raw.EntryOffset(1u) + kEntType,
                 CNA::Content::Cnb::CnbContainerChunk::Metadata.value);
    raw.FixStructuralChecksums();

    // Rejected by Parse() rather than by the accessor: a document decodes its container-level
    // chunks up front (CNBF-H004), so it is immutable once it exists and a structural problem
    // surfaces at the point the file is opened rather than at whichever later call happened to
    // touch it first.
    EXPECT_THROW((void)CnbDocument::Parse(raw.bytes, "twometa.cnb"), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-030 -- UTF-8
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, WellFormedUtf8StringsRoundTrip)
{
    const std::vector<std::string> samples = {
        "", "ascii", "kůň", "日本語", "emoji \xF0\x9F\x8E\xAE", std::string("with\0nul", 8)};
    for (const std::string& sample : samples)
    {
        CnbByteWriter w;
        w.WriteString(sample);
        const std::vector<std::uint8_t> bytes = w.Take();
        CnbByteReader reader(bytes, "test");
        EXPECT_EQ(reader.ReadString(), sample);
        EXPECT_EQ(reader.Remaining(), 0u);
    }
}

TEST(CnbContainerTest, MalformedUtf8StringsAreRejected)
{
    struct Case { const char* name; std::vector<std::uint8_t> raw; };
    const std::vector<Case> cases = {
        {"lone continuation byte", {0x80u}},
        {"truncated two-byte sequence", {0xC3u}},
        {"truncated three-byte sequence", {0xE6u, 0x97u}},
        {"overlong two-byte NUL", {0xC0u, 0x80u}},
        {"overlong three-byte slash", {0xE0u, 0x80u, 0xAFu}},
        {"UTF-16 surrogate half", {0xEDu, 0xA0u, 0x80u}},
        {"code point above U+10FFFF", {0xF5u, 0x80u, 0x80u, 0x80u}},
        {"five-byte form", {0xF8u, 0x88u, 0x80u, 0x80u, 0x80u}},
        {"bad continuation", {0xC3u, 0x28u}},
    };

    for (const Case& c : cases)
    {
        CnbByteWriter w;
        w.WriteU32(static_cast<std::uint32_t>(c.raw.size()));
        w.WriteBytes(c.raw);
        const std::vector<std::uint8_t> bytes = w.Take();

        CnbByteReader reader(bytes, "test");
        EXPECT_THROW((void)reader.ReadString(), ContentLoadException) << c.name;
    }
}

TEST(CnbContainerTest, WriterRefusesToEmitMalformedUtf8)
{
    CnbByteWriter w;
    EXPECT_THROW(w.WriteString(std::string("\xC3", 1)), ContentLoadException);
}

TEST(CnbContainerTest, StringDeclaringMoreBytesThanRemainIsRejected)
{
    CnbByteWriter w;
    w.WriteU32(64u);
    w.WriteU8(0x41u);
    const std::vector<std::uint8_t> bytes = w.Take();

    CnbByteReader reader(bytes, "test");
    EXPECT_THROW((void)reader.ReadString(), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-032 -- external reference safety
// --------------------------------------------------------------------------------------------

namespace
{
    std::vector<std::uint8_t> BuildXrefFile(const std::string& logicalName)
    {
        // Written by hand, because CnbWriter's own path would be free to start validating names
        // one day and this test must keep reaching the reader's check.
        CnbByteWriter payload;
        payload.WriteU32(1u);
        payload.WriteU32(0u);
        payload.WriteU32(CnbAssetTypeId::Texture2D);
        payload.WriteU32(static_cast<std::uint32_t>(logicalName.size()));
        payload.WriteBytes(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(logicalName.data()), logicalName.size()));

        // Emitted as an ordinary chunk and then retyped, because CnbWriter refuses to add a
        // container-defined identifier through AddChunk (CNBF-115) and SetExternalReferences()
        // now applies the very rule this test has to reach the reader's copy of.
        CnbWriter writer(CnbAssetTypeId::Model, 1u);
        writer.AddChunk(MakeChunkId('x', 'r', 'e', 'f'), payload.Take(),
                        CnbChunkFlags::Mandatory, 4u);
        RawCnb raw{writer.Build()};
        raw.PatchU32(raw.EntryOffset(0u) + kEntType,
                     CNA::Content::Cnb::CnbContainerChunk::ExternalReferences.value);
        raw.FixStructuralChecksums();
        return raw.bytes;
    }
}

TEST(CnbContainerTest, ExternalReferenceNamesAreValidatedForPathSafety)
{
    const std::vector<std::string> unsafe = {
        "",
        "/etc/passwd",
        "C:/Windows/system.ini",
        "..",
        "../secrets",
        "Textures/../../secrets",
        "Textures\\wall",
    };
    for (const std::string& name : unsafe)
    {
        // Refused by Parse(), not by the accessor (CNBF-H004): a file that names a path outside
        // the content root is malformed, and the moment it is opened is the right moment to say so.
        const std::vector<std::uint8_t> bytes = BuildXrefFile(name);
        EXPECT_THROW((void)CnbDocument::Parse(bytes, "refs.cnb"), ContentLoadException)
            << "name '" << name << "'";
    }
}

TEST(CnbContainerTest, OrdinaryExternalReferenceNamesAreAccepted)
{
    for (const std::string& name : {"wall", "Textures/wall", "a/b/c/d.png", "Flag.en-US"})
    {
        const std::vector<std::uint8_t> bytes = BuildXrefFile(name);
        const CnbDocument doc = CnbDocument::Parse(bytes, "refs.cnb");
        ASSERT_EQ(doc.ExternalReferences().size(), 1u) << name;
        EXPECT_EQ(doc.ExternalReferences()[0].logicalName, name);
    }
}

TEST(CnbContainerTest, ExternalReferenceIndexIsRangeChecked)
{
    const std::vector<std::uint8_t> bytes = BuildXrefFile("Textures/wall");
    const CnbDocument doc = CnbDocument::Parse(bytes, "refs.cnb");
    EXPECT_NO_THROW((void)doc.ExternalReferenceAt(0u, "a mesh texture"));
    EXPECT_THROW((void)doc.ExternalReferenceAt(1u, "a mesh texture"), ContentLoadException);
    EXPECT_THROW((void)doc.ExternalReferenceAt(0xFFFFFFFFu, "a mesh texture"), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// CNBF-033 -- determinism, asset-type dispatch helpers
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, WritingTheSameInputTwiceProducesIdenticalBytes)
{
    const auto build = []() {
        CnbWriter writer(CnbAssetTypeId::Model, 1u);
        writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Model", "Models/robot");
        writer.SetExternalReferences({
            CnbExternalReference{0u, CnbAssetTypeId::Texture2D, "Textures/skin"},
            CnbExternalReference{0u, CnbAssetTypeId::Texture2D, "Textures/normal"}});
        writer.AddChunk(kAlpha, Bytes({1, 2, 3}), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(kBeta, Bytes({4, 5, 6, 7, 8}), CnbChunkFlags::None, 16u);
        return writer.Build();
    };

    EXPECT_EQ(build(), build());
}

TEST(CnbContainerTest, RequireAssetChecksTypeAndSchemaVersion)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 2u);
    writer.AddChunk(kAlpha, Bytes({0}), CnbChunkFlags::None, 4u);
    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "curve.cnb");

    EXPECT_NO_THROW(doc.RequireAsset(CnbAssetTypeId::Curve, 2u));
    EXPECT_NO_THROW(doc.RequireAsset(CnbAssetTypeId::Curve, 3u));
    EXPECT_THROW(doc.RequireAsset(CnbAssetTypeId::Curve, 1u), ContentLoadException);
    EXPECT_THROW(doc.RequireAsset(CnbAssetTypeId::Model, 2u), ContentLoadException);
}

TEST(CnbContainerTest, CustomAssetTypeIdsAreStableAndLiveInTheCustomRange)
{
    const std::uint32_t level = CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Level");
    EXPECT_EQ(level, CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Level"));
    EXPECT_TRUE(CNA::Content::Cnb::IsCustomAssetTypeId(level));
    EXPECT_NE(level, CNA::Content::Cnb::CnbAssetTypeIdFromName("MyGame.Levels"));
    EXPECT_FALSE(CNA::Content::Cnb::IsCustomAssetTypeId(CnbAssetTypeId::Model));
    EXPECT_THROW((void)CNA::Content::Cnb::CnbAssetTypeIdFromName(""), std::invalid_argument);
}

TEST(CnbContainerTest, ChunkIdentifiersRenderAsTheirFourCharacters)
{
    EXPECT_EQ(CNA::Content::Cnb::ChunkIdToString(kAlpha), "ALFA");
    EXPECT_TRUE(CNA::Content::Cnb::IsWellFormedChunkId(kAlpha));
    EXPECT_FALSE(CNA::Content::Cnb::IsWellFormedChunkId(CnbChunkId{0x00414C41u}));
    EXPECT_EQ(CNA::Content::Cnb::ChunkIdToString(CnbChunkId{0x00414C41u}), "ALA?");
}

TEST(CnbContainerTest, WriterRejectsInvalidChunkIdentifiersAndAlignments)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    EXPECT_THROW(writer.AddChunk(CnbChunkId{0u}, {}, CnbChunkFlags::None, 4u),
                 ContentLoadException);
    EXPECT_THROW(writer.AddChunk(kAlpha, {}, CnbChunkFlags::None, 3u), ContentLoadException);
    EXPECT_THROW(writer.AddChunk(kAlpha, {}, 0x40u, 4u), ContentLoadException);
    EXPECT_THROW(CnbWriter(CnbAssetTypeId::Invalid, 1u), ContentLoadException);
    EXPECT_THROW(CnbWriter(CnbAssetTypeId::Curve, 0u), ContentLoadException);
}

TEST(CnbContainerTest, ParseFileReadsAndValidatesFromDisk)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "cna_cnb_parsefile_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "fixture.cnb";

    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(kAlpha, Bytes({1, 2, 3}), CnbChunkFlags::Mandatory, 4u);
    writer.WriteToFile(path.string());

    const CnbDocument doc = CnbDocument::ParseFile(path.string());
    EXPECT_EQ(doc.ChunkCount(), 1u);
    EXPECT_EQ(doc.AssetTypeId(), CnbAssetTypeId::Curve);

    CnbReadLimits tiny;
    tiny.maxFileSize = 8u;
    EXPECT_THROW((void)CnbDocument::ParseFile(path.string(), tiny), ContentLoadException);
    EXPECT_THROW((void)CnbDocument::ParseFile((dir / "missing.cnb").string()),
                 ContentLoadException);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(CnbContainerTest, PrimitiveEncodingIsLittleEndianAndAbiIndependent)
{
    CnbByteWriter w;
    w.WriteU8(0x12u);
    w.WriteU16(0x3456u);
    w.WriteU32(0x89ABCDEFu);
    w.WriteU64(0x0123456789ABCDEFull);
    w.WriteI32(-2);
    w.WriteF32(1.0f);
    w.WriteF64(-2.0);
    const std::vector<std::uint8_t> bytes = w.Take();

    const std::vector<std::uint8_t> expected = {
        0x12u,
        0x56u, 0x34u,
        0xEFu, 0xCDu, 0xABu, 0x89u,
        0xEFu, 0xCDu, 0xABu, 0x89u, 0x67u, 0x45u, 0x23u, 0x01u,
        0xFEu, 0xFFu, 0xFFu, 0xFFu,
        0x00u, 0x00u, 0x80u, 0x3Fu,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xC0u,
    };
    EXPECT_EQ(bytes, expected);

    CnbByteReader r(bytes, "test");
    EXPECT_EQ(r.ReadU8(), 0x12u);
    EXPECT_EQ(r.ReadU16(), 0x3456u);
    EXPECT_EQ(r.ReadU32(), 0x89ABCDEFu);
    EXPECT_EQ(r.ReadU64(), 0x0123456789ABCDEFull);
    EXPECT_EQ(r.ReadI32(), -2);
    EXPECT_EQ(r.ReadF32(), 1.0f);
    EXPECT_EQ(r.ReadF64(), -2.0);
    EXPECT_NO_THROW(r.RequireExhausted());
}

TEST(CnbContainerTest, EveryPrimitiveReadIsBoundsCheckedRatherThanTruncating)
{
    const std::vector<std::uint8_t> tiny = {1u};
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW((void)r.ReadU16(), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW((void)r.ReadU32(), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW((void)r.ReadU64(), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW((void)r.ReadF32(), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW((void)r.ReadF64(), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW((void)r.ReadBytes(2u), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW(r.Skip(2u), ContentLoadException);
    }
    {
        CnbByteReader r(tiny, "test");
        EXPECT_THROW(r.RequireExhausted(), ContentLoadException);
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-115 -- writer/reader symmetry: everything Build() accepts must be loadable
// --------------------------------------------------------------------------------------------

TEST(CnbContainerTest, TheLogicalNameRuleIsOneFunctionAndCoversEveryRefusal)
{
    using CNA::Content::Cnb::CnbLogicalNameProblem;

    // Acceptable: relative, '/'-separated, no traversal, well-formed UTF-8 (including non-ASCII,
    // which a name is allowed to be -- a content tree is not obliged to be English).
    EXPECT_TRUE(CnbLogicalNameProblem("music/theme").empty());
    EXPECT_TRUE(CnbLogicalNameProblem("a").empty());
    EXPECT_TRUE(CnbLogicalNameProblem("levels/1/..hidden").empty()) << "'..hidden' is not '..'";
    EXPECT_TRUE(CnbLogicalNameProblem("musique/thèm").empty());

    EXPECT_FALSE(CnbLogicalNameProblem("").empty());
    EXPECT_FALSE(CnbLogicalNameProblem("music\\theme").empty());
    EXPECT_FALSE(CnbLogicalNameProblem("/etc/passwd").empty());
    EXPECT_FALSE(CnbLogicalNameProblem("C:/windows").empty());
    EXPECT_FALSE(CnbLogicalNameProblem("../secret").empty());
    EXPECT_FALSE(CnbLogicalNameProblem("music/../../secret").empty());
    EXPECT_FALSE(CnbLogicalNameProblem("trailing/..").empty());
    EXPECT_FALSE(CnbLogicalNameProblem(std::string("bad\xC3", 4)).empty()) << "truncated UTF-8";
}

TEST(CnbContainerTest, WriterRefusesAnExternalReferenceItsOwnReaderWouldReject)
{
    // The asymmetry this closes: the reader has always refused these names, and the writer used to
    // emit them -- so an encoder could produce a file its own decoder rejects. Each case is
    // asserted through Build(), which is the boundary a schema encoder actually crosses.
    for (const char* hostile : {"", "..\\/secret", "/etc/passwd", "D:/x", "../secret",
                                "a/../../b"})
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::None, 4u);
        CnbExternalReference reference;
        reference.logicalName = hostile;
        writer.SetExternalReferences({reference});
        EXPECT_THROW((void)writer.Build(), ContentLoadException)
            << "Build() accepted the reference '" << hostile << "'";
    }
}

TEST(CnbContainerTest, WriterRefusesToAddAContainerDefinedChunkAsASchemaChunk)
{
    // Adding a second CMET or XREF produced a file Build() accepted and Parse() refused, because
    // the reader requires each to be a singleton. Caught at the AddChunk() that is wrong.
    for (const CnbChunkId reserved : {CNA::Content::Cnb::CnbContainerChunk::Metadata,
                                      CNA::Content::Cnb::CnbContainerChunk::ExternalReferences})
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        EXPECT_THROW(writer.AddChunk(reserved, Bytes({0, 0, 0, 0}), CnbChunkFlags::None, 4u),
                     ContentLoadException)
            << "AddChunk accepted the container-defined identifier "
            << CNA::Content::Cnb::ChunkIdToString(reserved);
        EXPECT_EQ(writer.SchemaChunkCount(), 0u);
    }
}

TEST(CnbContainerTest, EveryShapeOfWriterCallsAnEncoderMakesProducesALoadableFile)
{
    // The invariant the two tests above exist to protect, over the SHAPES a schema encoder
    // actually produces: with and without metadata, with and without references, with a mandatory
    // chunk, with an empty chunk, with a non-default alignment. Whatever combination of writer
    // calls succeeds, the bytes it produced parse.
    //
    // This is deliberately no longer named as if it proved Build() -> Parse() symmetry in general
    // (plans/plan_cnb.md CNBF-122): it varies the document's shape and not its SIZE, so it says
    // nothing about the reader's count and size limits. Those are the next test's subject, and
    // until CNBF-122 the writer did not enforce them at all.
    struct Shape
    {
        const char* what;
        bool metadata;
        bool references;
    };
    for (const Shape& shape : {Shape{"bare", false, false}, Shape{"cmet", true, false},
                               Shape{"xref", false, true}, Shape{"both", true, true}})
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        if (shape.metadata) { writer.SetMetadata("Microsoft.Xna.Framework.Curve", "sym/curve"); }
        if (shape.references)
        {
            CnbExternalReference reference;
            reference.logicalName = "textures/atlas";
            reference.expectedAssetTypeId = CnbAssetTypeId::Texture2D;
            writer.SetExternalReferences({reference});
        }
        writer.AddChunk(kAlpha, Bytes({1, 2, 3}), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(kBeta, {}, CnbChunkFlags::None, 64u);
        writer.AddChunk(kGamma, Bytes({7}), CnbChunkFlags::None, 4096u);

        std::vector<std::uint8_t> bytes;
        ASSERT_NO_THROW(bytes = writer.Build()) << shape.what;
        CnbDocument document = CnbDocument::Parse(bytes, "symmetry.cnb");
        const CnbChunkId known[] = {kAlpha, kBeta, kGamma};
        EXPECT_NO_THROW(document.RequireMandatoryChunksUnderstood(known)) << shape.what;
        EXPECT_EQ(document.Metadata().present, shape.metadata) << shape.what;
        EXPECT_EQ(document.ExternalReferences().size(), shape.references ? 1u : 0u) << shape.what;
        EXPECT_EQ(document.FindAll(CNA::Content::Cnb::CnbContainerChunk::Metadata).size(),
                  shape.metadata ? 1u : 0u)
            << shape.what;
    }
}


// --------------------------------------------------------------------------------------------
// CNBF-122 -- the writer enforces the reader's own limits, so Build() -> Parse() holds by SIZE too
// --------------------------------------------------------------------------------------------

namespace
{
    /// Bytes a `.cnb` costs before any chunk contents: the 64-byte header plus a 48-byte
    /// table-of-contents entry per chunk. Spelled out from the format constants so a layout change
    /// breaks this arithmetic rather than silently shifting the boundaries under it.
    [[nodiscard]] std::uint64_t ContainerOverhead(std::uint32_t chunkCount)
    {
        return Format::HeaderSize + static_cast<std::uint64_t>(chunkCount) * Format::TocEntrySize;
    }

    /// Small limits, so every boundary below is reached with a few hundred bytes rather than by
    /// allocating anything of the default limits' size.
    [[nodiscard]] CnbReadLimits TinyLimits()
    {
        CnbReadLimits limits;
        limits.maxFileSize = 512u;
        limits.maxChunkCount = 4u;
        limits.maxChunkSize = 128u;
        limits.maxTotalUncompressedSize = 192u;
        return limits;
    }

    /// One writer holding `count` chunks of `each` bytes, at the default 4-byte alignment.
    [[nodiscard]] CnbWriter WriterWith(const CnbReadLimits& limits, std::size_t count,
                                       std::size_t each)
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.SetLimits(limits);
        for (std::size_t i = 0; i < count; ++i)
        {
            writer.AddChunk(kAlpha, std::vector<std::uint8_t>(each, 0x5Au), CnbChunkFlags::None,
                            4u);
        }
        return writer;
    }
}

TEST(CnbContainerTest, TheWriterDefaultsToTheReadersOwnLimits)
{
    // The property that makes SetLimits() an override rather than a requirement: say nothing, and
    // the writer is already bounded by exactly what DefaultCnbReadLimits() allows.
    const CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    const CNA::Content::Cnb::CnbReadLimits& defaults = CNA::Content::Cnb::DefaultCnbReadLimits();
    EXPECT_EQ(writer.Limits().maxFileSize, defaults.maxFileSize);
    EXPECT_EQ(writer.Limits().maxChunkCount, defaults.maxChunkCount);
    EXPECT_EQ(writer.Limits().maxChunkSize, defaults.maxChunkSize);
    EXPECT_EQ(writer.Limits().maxTotalUncompressedSize, defaults.maxTotalUncompressedSize);
    EXPECT_EQ(writer.Limits().maxChunkAlignment, defaults.maxChunkAlignment);
}

TEST(CnbContainerTest, TheWriterRefusesADocumentWhoseChunksUnpackAboveTheAggregateBudget)
{
    // The asymmetry CNBF-122 closes, and the reason it is not hypothetical: with compression on, a
    // document's SERIALIZED size says nothing about the aggregate logical size CNBF-114 taught the
    // reader to bound. Build() checked neither, so a highly compressible document could be built
    // and then refused by a default Parse().
    const CnbReadLimits limits = TinyLimits(); // aggregate 192

    // Exactly at the limit: three 64-byte chunks unpack to 192. Accepted, and loadable.
    CnbWriter exact = WriterWith(limits, 3u, 64u);
    std::vector<std::uint8_t> bytes;
    ASSERT_NO_THROW(bytes = exact.Build());
    EXPECT_NO_THROW((void)CnbDocument::Parse(bytes, "aggregate-exact.cnb", limits));

    // One byte over: a fourth chunk of one byte. Every chunk is still individually legal, and the
    // file is still tiny -- the aggregate is the only thing that moved.
    CnbWriter over = WriterWith(limits, 3u, 64u);
    over.AddChunk(kBeta, Bytes({0}), CnbChunkFlags::None, 4u);
    EXPECT_THROW((void)over.Build(), ContentLoadException);

    // And the refusal is the AGGREGATE's, not the per-chunk ceiling's: each chunk here is well
    // under maxChunkSize, and raising only the aggregate makes the same document build.
    CnbReadLimits raised = limits;
    raised.maxTotalUncompressedSize = 193u;
    CnbWriter allowed = WriterWith(raised, 3u, 64u);
    allowed.AddChunk(kBeta, Bytes({0}), CnbChunkFlags::None, 4u);
    ASSERT_NO_THROW(bytes = allowed.Build());
    EXPECT_NO_THROW((void)CnbDocument::Parse(bytes, "aggregate-raised.cnb", raised));
}

TEST(CnbContainerTest, TheWriterRefusesAFileLargerThanTheReaderWouldOpen)
{
    // The one limit that cannot be known before the layout exists, so it is checked last -- but
    // still before the output buffer is allocated.
    CnbReadLimits limits = TinyLimits();
    // One chunk: 64-byte header + one 48-byte TOC entry + the payload.
    const std::uint64_t overhead = ContainerOverhead(1u);
    limits.maxFileSize = overhead + 100u;
    limits.maxChunkSize = 4096u;
    limits.maxTotalUncompressedSize = 4096u;

    CnbWriter exact = WriterWith(limits, 1u, 100u);
    std::vector<std::uint8_t> bytes;
    ASSERT_NO_THROW(bytes = exact.Build());
    EXPECT_EQ(bytes.size(), limits.maxFileSize) << "the arithmetic this test rests on is wrong";
    EXPECT_NO_THROW((void)CnbDocument::Parse(bytes, "size-exact.cnb", limits));

    // One byte over the ceiling, with everything else unchanged.
    CnbWriter over = WriterWith(limits, 1u, 101u);
    EXPECT_THROW((void)over.Build(), ContentLoadException);
}

TEST(CnbContainerTest, TheWriterRefusesTooManyChunksAndTooLargeAChunk)
{
    // The remaining two limits a reader applies to a well-formed file. Without these the "every
    // applicable default limit" claim below would be false, which is why they are here rather than
    // left to the reader.
    CnbReadLimits limits = TinyLimits();
    limits.maxChunkCount = 3u;
    limits.maxTotalUncompressedSize = 4096u;

    ASSERT_NO_THROW((void)WriterWith(limits, 3u, 8u).Build());
    EXPECT_THROW((void)WriterWith(limits, 4u, 8u).Build(), ContentLoadException);

    // maxChunkSize is 128 in TinyLimits(): one chunk at the ceiling builds, one byte over does not.
    CnbReadLimits perChunk = TinyLimits();
    perChunk.maxTotalUncompressedSize = 4096u;
    perChunk.maxFileSize = 4096u;
    ASSERT_NO_THROW((void)WriterWith(perChunk, 1u, perChunk.maxChunkSize).Build());
    EXPECT_THROW((void)WriterWith(perChunk, 1u, perChunk.maxChunkSize + 1u).Build(),
                 ContentLoadException);

    // A metadata chunk counts toward the chunk COUNT like any other -- the container-level chunks
    // are not exempt from the reader's arithmetic, so they must not be exempt from the writer's.
    CnbWriter withMetadata = WriterWith(limits, 3u, 8u);
    withMetadata.SetMetadata("Microsoft.Xna.Framework.Curve", "counted");
    EXPECT_THROW((void)withMetadata.Build(), ContentLoadException);
}

TEST(CnbContainerTest, EveryDefaultReaderLimitIsAlsoAWriterLimit)
{
    // The universal claim, made only now that it is true: for a limit set L, whatever Build()
    // accepts, Parse(..., L) loads. Asserted by walking each limit to its exact boundary and one
    // step past it, under injected small limits so nothing large is allocated.
    //
    // All SEVEN of CnbReadLimits' entries are in scope, which was not obvious: maxStringBytes and
    // maxArrayElementCount look like schema-layer limits, and are, but the CONTAINER reads CMET's
    // two names, XREF's count and every XREF name through the same bounded CnbByteReader -- so a
    // writer ignoring them emits a CMET its own Parse() refuses. They are covered by the second
    // half of this test.
    const CnbReadLimits limits = TinyLimits();

    struct Case
    {
        const char* what;
        std::size_t chunks;
        std::size_t each;
        bool buildable;
    };
    // aggregate 192, per chunk 128, at most 4 chunks, at most 512 bytes of file.
    for (const Case& c : {Case{"aggregate exactly", 3u, 64u, true},
                          Case{"aggregate one over", 3u, 65u, false},
                          Case{"per-chunk exactly", 1u, 128u, true},
                          Case{"per-chunk one over", 1u, 129u, false},
                          Case{"count exactly", 4u, 32u, true},
                          Case{"count one over", 5u, 32u, false}})
    {
        CnbWriter writer = WriterWith(limits, c.chunks, c.each);
        if (c.buildable)
        {
            std::vector<std::uint8_t> bytes;
            ASSERT_NO_THROW(bytes = writer.Build()) << c.what;
            EXPECT_NO_THROW((void)CnbDocument::Parse(bytes, "limits.cnb", limits))
                << c.what << ": Build() accepted a file Parse() refuses";
        }
        else
        {
            EXPECT_THROW((void)writer.Build(), ContentLoadException) << c.what;
        }
    }

    // The file-size ceiling separately, because it is the one that depends on the layout: 512
    // bytes of file is 64 + 4*48 = 256 of overhead plus 256 of payload.
    CnbReadLimits sizeOnly = limits;
    sizeOnly.maxTotalUncompressedSize = 4096u;
    sizeOnly.maxChunkSize = 4096u;
    const std::uint64_t payload = sizeOnly.maxFileSize - ContainerOverhead(4u);
    std::vector<std::uint8_t> bytes;
    ASSERT_NO_THROW(bytes = WriterWith(sizeOnly, 4u, static_cast<std::size_t>(payload / 4u))
                                .Build());
    EXPECT_EQ(bytes.size(), sizeOnly.maxFileSize);
    EXPECT_NO_THROW((void)CnbDocument::Parse(bytes, "limits-size.cnb", sizeOnly));
    EXPECT_THROW((void)WriterWith(sizeOnly, 4u, static_cast<std::size_t>(payload / 4u) + 1u).Build(),
                 ContentLoadException);

    // maxStringBytes, on the container's own strings: CMET's two names and every XREF name.
    CnbReadLimits strings;
    strings.maxStringBytes = 16u;
    for (int over = 0; over <= 1; ++over)
    {
        const std::string name(static_cast<std::size_t>(strings.maxStringBytes) +
                                   static_cast<std::size_t>(over),
                               'x');
        const char* what = over == 0 ? "exactly at maxStringBytes" : "one byte over";

        CnbWriter metadata(CnbAssetTypeId::Curve, 1u);
        metadata.SetLimits(strings);
        metadata.SetMetadata(name, "n");
        CnbWriter contentName(CnbAssetTypeId::Curve, 1u);
        contentName.SetLimits(strings);
        contentName.SetMetadata("n", name);
        CnbWriter reference(CnbAssetTypeId::Curve, 1u);
        reference.SetLimits(strings);
        CnbExternalReference ref;
        ref.logicalName = name; // a bare 'x'-run is a valid relative logical name
        reference.SetExternalReferences({ref});

        for (CnbWriter* writer : {&metadata, &contentName, &reference})
        {
            writer->AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::None, 4u);
            if (over == 0)
            {
                std::vector<std::uint8_t> built;
                ASSERT_NO_THROW(built = writer->Build()) << what;
                EXPECT_NO_THROW((void)CnbDocument::Parse(built, "strings.cnb", strings))
                    << what << ": Build() accepted a file Parse() refuses";
            }
            else
            {
                EXPECT_THROW((void)writer->Build(), ContentLoadException) << what;
            }
        }
    }

    // maxArrayElementCount, on the XREF row count. Two references at a ceiling of two build; the
    // same document at a ceiling of one does not.
    for (const std::uint32_t ceiling : {2u, 1u})
    {
        CnbReadLimits arrays;
        arrays.maxArrayElementCount = ceiling;
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.SetLimits(arrays);
        CnbExternalReference a;
        a.logicalName = "art/a";
        CnbExternalReference b;
        b.logicalName = "art/b";
        writer.SetExternalReferences({a, b});
        writer.AddChunk(kAlpha, Bytes({1}), CnbChunkFlags::None, 4u);
        if (ceiling == 2u)
        {
            std::vector<std::uint8_t> built;
            ASSERT_NO_THROW(built = writer.Build());
            EXPECT_NO_THROW((void)CnbDocument::Parse(built, "refs.cnb", arrays));
        }
        else
        {
            EXPECT_THROW((void)writer.Build(), ContentLoadException);
        }
    }
}
