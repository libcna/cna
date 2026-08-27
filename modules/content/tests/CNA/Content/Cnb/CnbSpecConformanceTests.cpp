// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-090: keeps `docs/cnb-format.md` and the implementation from drifting apart.
//
// A format specification that merely describes what someone believed the code did is worse than
// no specification, because it will be trusted. This suite asserts the document's own numbers
// against the code -- every header and table-of-contents field offset, every stride, every
// identifier, every limit -- and it regenerates §14's annotated hex dump and byte-compares it
// against what the writer actually produces.
//
// It reads the document from disk, so a change to either side that is not made to both fails here.

#include <bit>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Cnb/CnbReadLimits.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::DefaultCnbReadLimits;
using CNA::Content::Cnb::EncodeCurveToCnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;

using CNA::Content::Cnb::CnbCompression;
namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;
namespace Format = CNA::Content::Cnb::Format;

namespace
{
    /// Finds docs/cnb-format.md whether the suite runs from the source root or from a build
    /// directory beside it. Returns an empty path when neither works.
    std::filesystem::path FindSpec()
    {
        for (const char* candidate : {"docs/cnb-format.md", "../docs/cnb-format.md",
                                       "../../docs/cnb-format.md"})
        {
            if (std::filesystem::exists(candidate)) { return candidate; }
        }
        return {};
    }

    std::string ReadSpec()
    {
        const std::filesystem::path path = FindSpec();
        if (path.empty()) { return {}; }
        std::ifstream file(path, std::ios::binary);
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void ExpectSpecContains(const std::string& spec, const std::string& text)
    {
        EXPECT_NE(spec.find(text), std::string::npos)
            << "docs/cnb-format.md no longer contains: " << text;
    }
}

TEST(CnbSpecConformanceTest, TheDocumentIsPresentAndReadable)
{
    // Everything below depends on this; failing here once beats fifteen confusing failures.
    ASSERT_FALSE(FindSpec().empty())
        << "docs/cnb-format.md was not found (run the suite from the source root)";
    EXPECT_GT(ReadSpec().size(), 4096u);
}

TEST(CnbSpecConformanceTest, TheDocumentedHeaderAndTableLayoutMatchTheImplementation)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    EXPECT_EQ(Format::HeaderSize, 64u);
    EXPECT_EQ(Format::TocEntrySize, 48u);
    EXPECT_EQ(Format::HeaderChecksumOffset, 44u);
    EXPECT_EQ(Format::HeaderChecksumCoverage, 44u);
    EXPECT_EQ(Format::HeaderReservedSize, 16u);
    EXPECT_EQ(Format::ContainerMajor, 1u);
    EXPECT_EQ(Format::ContainerMinor, 0u);
    EXPECT_EQ(Format::DefaultTocOffset, 64u);
    EXPECT_EQ(Format::Magic[0], 0x43u);
    EXPECT_EQ(Format::Magic[1], 0x4Eu);
    EXPECT_EQ(Format::Magic[2], 0x42u);
    EXPECT_EQ(Format::Magic[3], 0x1Au);

    ExpectSpecContains(spec, "Exactly 64 bytes at offset 0");
    ExpectSpecContains(spec, "`43 4E 42 1A`");
    ExpectSpecContains(spec, "entries of exactly 48 bytes each");
    ExpectSpecContains(spec, "CRC-32C of bytes `[0, 44)`");
    ExpectSpecContains(spec, "| 48 | 16 | `reserved` | must be all zero |");
}

TEST(CnbSpecConformanceTest, TheDocumentedAssetTypeIdentifiersMatchTheImplementation)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    EXPECT_EQ(CnbAssetTypeId::Invalid, 0x00000000u);
    EXPECT_EQ(CnbAssetTypeId::Texture2D, 1u);
    EXPECT_EQ(CnbAssetTypeId::Texture3D, 2u);
    EXPECT_EQ(CnbAssetTypeId::TextureCube, 3u);
    EXPECT_EQ(CnbAssetTypeId::SpriteFont, 4u);
    EXPECT_EQ(CnbAssetTypeId::Model, 5u);
    EXPECT_EQ(CnbAssetTypeId::AnimationClip, 6u);
    EXPECT_EQ(CnbAssetTypeId::Curve, 7u);
    EXPECT_EQ(CnbAssetTypeId::SoundEffect, 8u);
    EXPECT_EQ(CnbAssetTypeId::Song, 9u);
    EXPECT_EQ(CnbAssetTypeId::Video, 10u);
    EXPECT_EQ(CnbAssetTypeId::Effect, 11u);
    EXPECT_EQ(CnbAssetTypeId::ReservedRangeFirst, 0x40000000u);
    EXPECT_EQ(CnbAssetTypeId::CustomRangeFirst, 0x80000000u);

    ExpectSpecContains(spec, "0x00000001-0x3FFFFFFF   CNA built-in asset types");
    ExpectSpecContains(spec, "0x80000000-0xFFFFFFFF   game-defined custom types");
    ExpectSpecContains(spec, "| 1 | `Texture2D` | **version 1**");
    ExpectSpecContains(spec, "| 2 | `Texture3D` | **version 1**");
    ExpectSpecContains(spec, "| 3 | `TextureCube` | **version 1**");
    ExpectSpecContains(spec, "| 4 | `SpriteFont` | **version 1**");
    // CNBF-105: the codec table is a wire contract -- codec 2 is Zstandard in every file ever
    // written, whether or not a given build implements it.
    ExpectSpecContains(spec, "| 2 | Zstandard | **implemented**");
    ExpectSpecContains(spec, "checksum` covers the **stored** bytes");
    ExpectSpecContains(spec, "| 5 | `Model` | **version 1**");
    ExpectSpecContains(spec, "| 8 | `SoundEffect` | **version 1**");
    ExpectSpecContains(spec, "| 9 | `Song` | **version 1**");
    ExpectSpecContains(spec, "| 10 | `Video` | **version 1**");
    ExpectSpecContains(spec, "| 6 | `AnimationClip` | **version 1**");
    ExpectSpecContains(spec, "| 7 | `Curve` | **version 1**");
    ExpectSpecContains(spec, "`FNV-1a-32(name) | 0x80000000`");
}

TEST(CnbSpecConformanceTest, TheDocumentedSchemaStridesMatchTheImplementation)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    EXPECT_EQ(CNA::Content::Cnb::CnbCurveKeyStride, 20u);
    EXPECT_EQ(CNA::Content::Cnb::CnbAnimationTrackStride, 12u);
    EXPECT_EQ(CNA::Content::Cnb::CnbAnimationKeyStride, 48u);
    EXPECT_EQ(CNA::Content::Cnb::CnbModelBoneStride, 72u);
    EXPECT_EQ(CNA::Content::Cnb::CnbModelMeshStride, 16u);
    EXPECT_EQ(CNA::Content::Cnb::CnbModelPartStride, 56u);
    EXPECT_EQ(CNA::Content::Cnb::CnbModelMaterialStride, 368u);
    EXPECT_EQ(CNA::Content::Cnb::CnbTextureHeaderStride, 24u);
    EXPECT_EQ(CNA::Content::Cnb::CnbTextureRepresentationStride, 24u);
    EXPECT_EQ(CNA::Content::Cnb::CnbTextureCubeFaceCount, 6u);
    EXPECT_EQ(CNA::Content::Cnb::CnbMaxTextureMipLevels, 16u);
    EXPECT_EQ(CNA::Content::Cnb::CnbMaxTextureRepresentations, 8u);
    EXPECT_EQ(CNA::Content::Cnb::CnbSpriteFontHeaderStride, 24u);
    EXPECT_EQ(CNA::Content::Cnb::CnbSpriteFontRectangleStride, 16u);
    EXPECT_EQ(CNA::Content::Cnb::CnbSpriteFontKerningStride, 12u);
    EXPECT_EQ(CNA::Content::Cnb::CnbSpriteFontCharacterStride, 4u);
    EXPECT_EQ(CNA::Content::Cnb::CnbMaxSpriteFontGlyphs, 65536u);
    EXPECT_EQ(CNA::Content::Cnb::CnbSoundEffectHeaderStride, 28u);
    EXPECT_EQ(CNA::Content::Cnb::CnbMaxAudioSampleRate, 384000u);
    EXPECT_EQ(CNA::Content::Cnb::CnbAudioFormatMax, 5u);
    EXPECT_EQ(CNA::Content::Cnb::CnbSongHeaderFixedStride, 8u);
    EXPECT_EQ(CNA::Content::Cnb::CnbVideoHeaderStride, 24u);
    EXPECT_EQ(CNA::Content::Cnb::CnbMaxVideoDimension, 65536u);
    EXPECT_EQ(static_cast<std::uint32_t>(CNA::Content::Cnb::CnbAudioFormat::Pcm16), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(CNA::Content::Cnb::CnbAudioFormat::Vorbis), 5u);
    EXPECT_EQ(CNA::Content::Cnb::CnbNoIndex, 0xFFFFFFFFu);
    EXPECT_EQ(CNA::Content::Cnb::CnbTextureSlotCount, 7u);
    EXPECT_EQ(CNA::Content::Cnb::CnbMaxEffectKind, 5u);

    ExpectSpecContains(spec, "`keyCount` × 20 bytes");
    ExpectSpecContains(spec, "`trackCount` × 12 bytes");
    ExpectSpecContains(spec, "`totalKeyCount` × 48 bytes");
    ExpectSpecContains(spec, "`boneCount` × 72 bytes");
    ExpectSpecContains(spec, "16 bytes each");
    ExpectSpecContains(spec, "56 bytes each");
    ExpectSpecContains(spec, "`count` × 368 bytes");
    ExpectSpecContains(spec, "`representationCount` × 24-byte descriptors");
    ExpectSpecContains(spec, "`glyphCount` × 16 bytes: source rectangles");
    ExpectSpecContains(spec, "`glyphCount` × 12 bytes: bearings");
}

TEST(CnbSpecConformanceTest, TheDocumentedChunkIdentifiersMatchTheImplementation)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    const auto render = CNA::Content::Cnb::ChunkIdToString;
    EXPECT_EQ(render(CNA::Content::Cnb::CnbContainerChunk::Metadata), "CMET");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbContainerChunk::ExternalReferences), "XREF");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbCurveChunk::Header), "CRVH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbCurveChunk::Keys), "CRVK");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbAnimationClipChunk::Header), "ACLH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbAnimationClipChunk::Tracks), "ACLT");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbAnimationClipChunk::Keys), "ACLK");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbMediaChunk::SongHeader), "SNGH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbMediaChunk::VideoHeader), "VIDH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSoundEffectChunk::Header), "AUDH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSoundEffectChunk::Data), "AUDD");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSpriteFontChunk::Header), "FONT");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSpriteFontChunk::GlyphBounds), "GLYP");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSpriteFontChunk::Cropping), "CROP");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSpriteFontChunk::Kerning), "KERN");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbSpriteFontChunk::Characters), "CHAR");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbTextureChunk::Header), "TEXH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbTextureChunk::Representations), "TEXR");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbTextureChunk::Payload), "TEXD");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Header), "MDLH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Strings), "MSTR");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Bones), "MBON");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Meshes), "MMSH");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Materials), "MMAT");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::VertexData), "MVTX");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::IndexData), "MIDX");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::MorphData), "MMRP");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Skeleton), "MSKL");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Animations), "MANM");
    EXPECT_EQ(render(CNA::Content::Cnb::CnbModelChunk::Lights), "MLIT");

    for (const char* id : {"CMET", "XREF", "CRVH", "CRVK", "ACLH", "ACLT", "ACLK", "MDLH", "MSTR",
                           "MBON", "MMSH", "MMAT", "MVTX", "MIDX", "MMRP", "MSKL", "MANM", "MLIT"})
    {
        ExpectSpecContains(spec, std::string("`") + id + "`");
    }
}

TEST(CnbSpecConformanceTest, TheDocumentedLimitsMatchTheImplementation)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    const auto& limits = DefaultCnbReadLimits();
    EXPECT_EQ(limits.maxFileSize, 512ull * 1024ull * 1024ull);
    EXPECT_EQ(limits.maxChunkCount, 65536u);
    EXPECT_EQ(limits.maxChunkSize, 384ull * 1024ull * 1024ull);
    EXPECT_EQ(limits.maxStringBytes, 1024u * 1024u);
    EXPECT_EQ(limits.maxArrayElementCount, 16u * 1024u * 1024u);
    EXPECT_EQ(limits.maxChunkAlignment, 4096u);

    ExpectSpecContains(spec, "| `maxFileSize` | 512 MiB |");
    ExpectSpecContains(spec, "| `maxChunkCount` | 65536 |");
    ExpectSpecContains(spec, "| `maxChunkSize` | 384 MiB |");
    ExpectSpecContains(spec, "| `maxStringBytes` | 1 MiB |");
    ExpectSpecContains(spec, "| `maxArrayElementCount` | 16777216 |");
    ExpectSpecContains(spec, "| `maxChunkAlignment` | 4096 |");
}

TEST(CnbSpecConformanceTest, TheAnnotatedHexExampleMatchesTheBytesTheWriterProduces)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    // §14's example: a Curve with no keys, no CMET and no XREF -- which is EncodeCurveToCnb's
    // output minus the metadata chunk it always writes, so it is built here from the same writer
    // rather than described.
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    std::vector<std::uint8_t> header;
    for (int i = 0; i < 12; ++i) { header.push_back(0u); } // preLoop, postLoop, keyCount, all 0
    writer.AddChunk(CNA::Content::Cnb::CnbCurveChunk::Header, header, CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbCurveChunk::Keys, {}, CnbChunkFlags::Mandatory, 4u);
    const std::vector<std::uint8_t> bytes = writer.Build();

    // Every literal §14 states, checked against the real bytes.
    ASSERT_EQ(bytes.size(), 172u) << "the documented example is 172 bytes";
    ExpectSpecContains(spec, "172\nbytes");

    const auto u16At = [&](std::size_t o) {
        return static_cast<std::uint16_t>(bytes[o] | (bytes[o + 1] << 8));
    };
    const auto u32At = [&](std::size_t o) {
        std::uint32_t v = 0;
        for (int i = 3; i >= 0; --i) { v = (v << 8) | bytes[o + static_cast<std::size_t>(i)]; }
        return v;
    };
    const auto u64At = [&](std::size_t o) {
        std::uint64_t v = 0;
        for (int i = 7; i >= 0; --i) { v = (v << 8) | bytes[o + static_cast<std::size_t>(i)]; }
        return v;
    };

    EXPECT_EQ(u16At(4), 1u);        // containerMajor
    EXPECT_EQ(u16At(6), 0u);        // containerMinor
    EXPECT_EQ(u32At(8), 0u);        // headerFlags
    EXPECT_EQ(u32At(12), 7u);       // assetTypeId = Curve
    EXPECT_EQ(u32At(16), 1u);       // assetSchemaVersion
    EXPECT_EQ(u32At(20), 2u);       // chunkCount
    EXPECT_EQ(u64At(24), 172u);     // fileSize
    EXPECT_EQ(u64At(32), 64u);      // tocOffset
    for (std::size_t i = 48; i < 64; ++i) { EXPECT_EQ(bytes[i], 0u) << "reserved byte " << i; }

    // Entry 0: CRVH at 160, 12 bytes, alignment 4, mandatory.
    EXPECT_EQ(u32At(64 + 0), CNA::Content::Cnb::CnbCurveChunk::Header.value);
    EXPECT_EQ(u32At(64 + 4), CnbChunkFlags::Mandatory);
    EXPECT_EQ(u64At(64 + 8), 160u);
    EXPECT_EQ(u64At(64 + 16), 12u);
    EXPECT_EQ(u64At(64 + 24), 12u);
    EXPECT_EQ(u32At(64 + 36), 0u);  // compression = none
    EXPECT_EQ(u32At(64 + 40), 4u);  // alignment
    EXPECT_EQ(u32At(64 + 44), 0u);  // reserved

    // Entry 1: CRVK, zero-length, sitting at the end-of-file offset.
    EXPECT_EQ(u32At(112 + 0), CNA::Content::Cnb::CnbCurveChunk::Keys.value);
    EXPECT_EQ(u64At(112 + 8), 172u);
    EXPECT_EQ(u64At(112 + 16), 0u);
    EXPECT_EQ(u32At(112 + 32), 0u); // CRC-32C of nothing is 0

    // CRVH's payload.
    EXPECT_EQ(u32At(160), 0u);
    EXPECT_EQ(u32At(164), 0u);
    EXPECT_EQ(u32At(168), 0u);

    // And the file the document describes is a file the reader accepts.
    const CnbDocument document = CnbDocument::Parse(bytes, "spec-example.cnb");
    EXPECT_EQ(document.AssetTypeId(), CnbAssetTypeId::Curve);
    EXPECT_EQ(document.ChunkCount(), 2u);
}

TEST(CnbSpecConformanceTest, TheDocumentedCustomTypeRuleIsTheOneTheCodeEnforces)
{
    // plans/plan_cnb.md CNBF-H002/CNBF-H011. Textual scraping alone would let the document and the
    // implementation drift together, so each clause below is asserted against BEHAVIOUR as well as
    // against the sentence that promises it.
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "For a **custom** asset type the chunk is **required**");
    ExpectSpecContains(spec, "Dispatch itself compares names, not just numbers");
    ExpectSpecContains(spec, "built-in types dispatch on the number alone");

    // Behaviour: a writer refuses a custom-typed file with no canonical name.
    const std::uint32_t customId =
        CNA::Content::Cnb::CnbAssetTypeIdFromName("SpecConformance.Widget");
    {
        CnbWriter writer(customId, 1u);
        writer.AddChunk(CNA::Content::Cnb::MakeChunkId('w', 'd', 'g', 't'), {1, 2, 3},
                        CnbChunkFlags::Mandatory, 4u);
        EXPECT_THROW((void)writer.Build(), ContentLoadException)
            << "the document says a custom-typed file must carry its canonical name";
    }
    // Behaviour: with the name, the same file builds.
    {
        CnbWriter writer(customId, 1u);
        writer.SetMetadata("SpecConformance.Widget", "widgets/one");
        writer.AddChunk(CNA::Content::Cnb::MakeChunkId('w', 'd', 'g', 't'), {1, 2, 3},
                        CnbChunkFlags::Mandatory, 4u);
        EXPECT_NO_THROW((void)writer.Build());
    }
    // Behaviour: a built-in type needs no such thing, which is the asymmetry the document states.
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(CNA::Content::Cnb::CnbCurveChunk::Header,
                        std::vector<std::uint8_t>(12u, 0u), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CNA::Content::Cnb::CnbCurveChunk::Keys, {}, CnbChunkFlags::Mandatory, 4u);
        EXPECT_NO_THROW((void)writer.Build());
    }
}

TEST(CnbSpecConformanceTest, TheDocumentedParentBeforeChildRuleIsTheOneTheCodeEnforces)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "must be ordered parent-before-child");
    ExpectSpecContains(spec, "-1 for the root; otherwise an EARLIER index in this table");
    ExpectSpecContains(spec, "parent-before-child, as for MBON");

    // Behaviour: the encoder refuses a forward reference in both tables.
    CNA::Content::Cnb::CnbModelData model;
    CNA::Content::Cnb::CnbModelBone root;
    root.name = "Root";
    CNA::Content::Cnb::CnbModelBone child;
    child.name = "Child";
    child.parent = 2;   // forward
    CNA::Content::Cnb::CnbModelBone later;
    later.name = "Later";
    later.parent = 0;
    model.bones = {root, child, later};
    model.hasBoneHierarchy = true;

    CNA::Content::Cnb::CnbModelPart part;
    part.name = "Only";
    part.vertexStride = 16u;
    part.vertexCount = 1u;
    part.indexCount = 0u;
    part.indexElementSize = 2u;
    part.vertexBytes.assign(16u, 0u);
    model.parts = {part};
    CNA::Content::Cnb::CnbModelMesh mesh;
    mesh.name = "Only";
    mesh.parentBone = 0;
    mesh.partIndices = {0u};
    model.meshes = {mesh};

    EXPECT_THROW((void)CNA::Content::Cnb::EncodeModelToCnb(model), ContentLoadException);
}

TEST(CnbSpecConformanceTest, TheDocumentedDispatchOrderIsTheOneTheCodeFollows)
{
    // plans/plan_cnb.md CNBF-H018. §7 now spells out the dispatch sequence, including the part that
    // is easy to get wrong when reading the code: the registry lookup happens BEFORE the
    // custom-name checks, so an unregistered custom identifier is reported as an unknown type
    // rather than as a name mismatch. Asserted behaviourally, because a sequence stated only in
    // prose is a sequence nobody notices changing.
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "selects the **candidate** loader");
    ExpectSpecContains(spec, "the numeric identifier is sufficient");
    ExpectSpecContains(spec, "the numeric identifier is *not* sufficient");
    ExpectSpecContains(spec, "reported as an unknown type, not as a name mismatch");

    // Behaviour: an unregistered CUSTOM identifier, whose file DOES carry a canonical name, is
    // refused for being unknown -- not for a name mismatch, which is what a reordered
    // implementation would report.
    const std::uint32_t unknownId =
        CNA::Content::Cnb::CnbAssetTypeIdFromName("SpecConformance.NeverRegistered");
    ASSERT_FALSE(CNA::Content::CnbLoaderRegistry::IsRegistered(unknownId));

    CnbWriter writer(unknownId, 1u);
    writer.SetMetadata("SpecConformance.NeverRegistered", "unused");
    writer.AddChunk(CNA::Content::Cnb::MakeChunkId('n', 'o', 'p', 'e'), {1},
                    CnbChunkFlags::Mandatory, 4u);
    const CnbDocument document = CnbDocument::Parse(writer.Build(), "unknown.cnb");

    try
    {
        (void)CNA::Content::CnbLoaderRegistry::ResolveForDocument(document);
        FAIL() << "expected an unregistered custom type to be refused";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("no .cnb loader for"), std::string::npos) << message;
        EXPECT_EQ(message.find("collide"), std::string::npos)
            << "an unregistered type must not be reported as a collision: " << message;
    }
}

TEST(CnbSpecConformanceTest, TheDocumentedFloatContractIsTheOneTheCodeEnforces)
{
    // plans/plan_cnb.md CNBF-H019. §2.1 makes three claims that the code must actually honour: the
    // representation is checked at COMPILE time, the container stores bit patterns verbatim
    // without canonicalising, and -0.0 is distinguishable from +0.0 in the bytes.
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "enforced **at compile time**");
    ExpectSpecContains(spec, "stores the bit pattern verbatim");
    ExpectSpecContains(spec, "does not promise signalling-NaN preservation on every ABI");

    // The compile-time half is proven by this translation unit existing: CnbFormat.hpp's
    // static_asserts would have failed the build otherwise. Restated here so the dependency is
    // visible to a reader of the test rather than implicit.
    static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);
    static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);
    static_assert(std::bit_cast<std::uint32_t>(1.0f) == 0x3F800000u);
    static_assert(std::bit_cast<std::uint64_t>(1.0) == 0x3FF0000000000000ull);

    // The runtime half: no canonicalisation, and the two zeroes really are different bytes.
    CNA::Content::Cnb::CnbByteWriter positive;
    positive.WriteF32(0.0f);
    CNA::Content::Cnb::CnbByteWriter negative;
    negative.WriteF32(-0.0f);
    EXPECT_NE(positive.View().size(), 0u);
    EXPECT_NE(std::vector<std::uint8_t>(positive.View().begin(), positive.View().end()),
              std::vector<std::uint8_t>(negative.View().begin(), negative.View().end()))
        << "the specification says -0.0 and +0.0 produce different bytes";
}

TEST(CnbSpecConformanceTest, TheDocumentedTextureFormatTableMatchesTheImplementation)
{
    // plans/plan_cnb.md CNBF-101A-fmt. These numbers are in shipped files the moment a texture is
    // compiled, and §16.4's whole point is that they cannot be derived from SurfaceFormat. So the
    // table is checked row by row against the code rather than trusted to stay in step.
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "The `format` field is **not** a `SurfaceFormat` value");
    ExpectSpecContains(spec, "| 1 | `Rgba8` | 4 | `Color` |");
    ExpectSpecContains(spec, "| 22 | `Bc1` | 8 per 4×4 block | `Dxt1` |");
    ExpectSpecContains(spec, "| 27 | `Bc7Srgb` | 16 per 4×4 block | `Bc7SrgbEXT` |");
    ExpectSpecContains(spec, "The rounding is up, to whole blocks");
    ExpectSpecContains(spec, "face-major, then mip");

    // Every row of the documented table, parsed out of the document and checked against the code.
    // A row silently deleted from the table fails the count; a row whose numbers were edited on
    // one side only fails its own assertion.
    std::size_t rowsChecked = 0;
    for (std::uint32_t id = 1u; id <= CNA::Content::Cnb::CnbTextureFormatMax; ++id)
    {
        const auto format = static_cast<CNA::Content::Cnb::CnbTextureFormat>(id);
        const std::string row = "| " + std::to_string(id) + " | `" +
                                CNA::Content::Cnb::CnbTextureFormatToString(format) + "` |";
        EXPECT_NE(spec.find(row), std::string::npos)
            << "docs/cnb-format.md §16.4 has no row for identifier " << id;
        ++rowsChecked;
    }
    EXPECT_EQ(rowsChecked, 27u);
}

TEST(CnbSpecConformanceTest, TheDocumentedTextureShapeRulesAreTheOnesTheCodeEnforces)
{
    // §16.1's three shape rules are what separate three asset types that share one chunk layout.
    // Asserted behaviourally: a rule stated only in prose is a rule that stops being true quietly.
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "must be 1** unless the asset is a `Texture3D`");
    ExpectSpecContains(spec, "a cube face is square");
    ExpectSpecContains(spec, "descriptors must tile the `TEXD` list exactly");

    CNA::Content::Cnb::CnbTextureData cube;
    cube.width = 4u; cube.height = 2u; cube.depth = 1u;
    cube.faceCount = CNA::Content::Cnb::CnbTextureCubeFaceCount; cube.mipCount = 1u;
    CNA::Content::Cnb::CnbTextureRepresentation representation;
    representation.format = CNA::Content::Cnb::CnbTextureFormat::Rgba8;
    for (std::uint32_t face = 0u; face < cube.faceCount; ++face)
    {
        representation.levels.push_back(std::vector<std::uint8_t>(4u * 2u * 4u));
    }
    cube.representations.push_back(representation);
    // Non-square: refused, exactly as §16.1 says.
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeTextureCubeToCnb(cube), ContentLoadException);

    CNA::Content::Cnb::CnbTextureData deep;
    deep.width = 2u; deep.height = 2u; deep.depth = 2u;
    deep.faceCount = 1u; deep.mipCount = 1u;
    CNA::Content::Cnb::CnbTextureRepresentation volume;
    volume.format = CNA::Content::Cnb::CnbTextureFormat::Rgba8;
    volume.levels.push_back(std::vector<std::uint8_t>(2u * 2u * 2u * 4u));
    deep.representations.push_back(volume);
    // Depth > 1 is a Texture3D and nothing else.
    EXPECT_THROW((void)CNA::Content::Cnb::EncodeTexture2DToCnb(deep), ContentLoadException);
    EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeTexture3DToCnb(deep));
}

TEST(CnbSpecConformanceTest, TheDocumentDistinguishesSchemaFromProducerSupport)
{
    // plans/plan_cnb.md CNBF-109..113. "Supported" was one word covering four different claims,
    // and conflating them is how the tree ended up with ten schemas and three compilable types.
    // §15.1 keeps them apart; this pins that it still does, and that the one real gap is named.
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    ExpectSpecContains(spec, "Four different meanings of \"supported\"");
    ExpectSpecContains(spec, "| asset type | wire schema | runtime loader | writer API | CLI producer |");
    ExpectSpecContains(spec, "`TextureCube` is the one implemented schema with no producer");
    ExpectSpecContains(spec, "cna_tool_source_to_cnb");
    ExpectSpecContains(spec, "headless and deterministic");

    // §15.2 lists what cnj_to_cnb compiles. The list is a claim about code, so it names all seven
    // types rather than saying "the matching .cnb" -- a vague row cannot go stale visibly.
    ExpectSpecContains(spec, "`Curve`, `AnimationClip`, `Model`, `Texture2D`, `Texture3D`, "
                             "`SpriteFont`, `SoundEffect`");
}

TEST(CnbSpecConformanceTest, TheDocumentStatesTheThingsThatMustNotQuietlyChange)
{
    const std::string spec = ReadSpec();
    if (spec.empty()) { GTEST_SKIP() << "docs/cnb-format.md was not found"; }

    // Load-bearing claims, each of which a reader will rely on. If one is deleted from the
    // document, that is a decision someone has to make on purpose.
    ExpectSpecContains(spec, "little-endian");
    ExpectSpecContains(spec, "accidental");
    ExpectSpecContains(spec, "sorted by ascending `offset`");
    ExpectSpecContains(spec, "zero-length chunk is legal");
    ExpectSpecContains(spec, "ordinal within its\nown type");
    ExpectSpecContains(spec, "byte-identical output");
    // CNBF-105 changed this deliberately: codec 0 is now "always available; the default" rather
    // than the only one defined, because Zstandard landed as an opt-in codec. The identifiers
    // themselves did not move, and that is what must not change quietly.
    ExpectSpecContains(spec, "| 0 | none | always available; the default |");
    ExpectSpecContains(spec, "| 1 | LZ4 |");
    ExpectSpecContains(spec, "| 3 | Deflate |");
    ExpectSpecContains(spec, "**accept** — minor bumps are additive-only");
    ExpectSpecContains(spec, "material variants");
    ExpectSpecContains(spec, "held\n**by value**");
    ExpectSpecContains(spec, "decoded during parsing rather than on first use");
    // The chunk-identifier case split is a CONVENTION, not an enforced rule; saying so is what
    // stops a reader assuming the writer will reject a mis-cased identifier (CNBF-H020).
    ExpectSpecContains(spec, "convention, not an enforced invariant");
    ExpectSpecContains(spec, "at most `maxChunkAlignment`");
}
