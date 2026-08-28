// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-103B: the Song and Video schemas.
//
// These two are deliberately NOT built like SoundEffect. A sound effect owns its samples; a song
// or a video can be hundreds of megabytes and wants streaming, seeking and buffering, so the .cnb
// carries metadata and an XREF to the media file rather than the media itself. The tests are
// mostly about that decision holding: the reference is mandatory, there is exactly one of it, and
// it shows up in the dependency table a build script reads.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbSongData;
using CNA::Content::Cnb::CnbVideoData;
using CNA::Content::Cnb::DecodeSongFromCnb;
using CNA::Content::Cnb::DecodeVideoFromCnb;
using CNA::Content::Cnb::EncodeSongToCnb;
using CNA::Content::Cnb::EncodeVideoToCnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    CnbSongData MakeSong()
    {
        CnbSongData data;
        data.streamReference = "Music/theme.ogg";
        data.name = "Main Theme";
        data.durationMs = 185000u;
        return data;
    }

    CnbVideoData MakeVideo()
    {
        CnbVideoData data;
        data.streamReference = "Movies/intro.mp4";
        data.durationMs = 42000u;
        data.width = 1920u;
        data.height = 1080u;
        data.framesPerSecond = 29.97f;
        data.soundtrackType = 2u; // MusicAndDialog
        return data;
    }

    class ScratchRoot
    {
    public:
        ScratchRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_media_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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
}

TEST(CnbMediaCodecTest, ASongRoundTripsItsMetadataAndStreamReference)
{
    const CnbSongData source = MakeSong();
    const std::vector<std::uint8_t> bytes = EncodeSongToCnb(source, "Music/theme");
    const CnbDocument document = CnbDocument::Parse(bytes, "theme.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::Song);
    EXPECT_EQ(document.Metadata().assetTypeName, "Microsoft.Xna.Framework.Media.Song");

    const CnbSongData decoded = DecodeSongFromCnb(document);
    EXPECT_EQ(decoded.streamReference, "Music/theme.ogg");
    EXPECT_EQ(decoded.name, "Main Theme");
    EXPECT_EQ(decoded.durationMs, 185000u);

    // The whole point of putting the reference in XREF rather than in the schema chunk: it is
    // visible in the container's own dependency table, which is what cnb_info --refs prints and
    // what a build script uses to know the .ogg has to ship too.
    ASSERT_EQ(document.ExternalReferences().size(), 1u);
    EXPECT_EQ(document.ExternalReferences()[0].logicalName, "Music/theme.ogg");
}

TEST(CnbMediaCodecTest, AVideoRoundTripsItsMetadataAndStreamReference)
{
    const CnbVideoData source = MakeVideo();
    const std::vector<std::uint8_t> bytes = EncodeVideoToCnb(source, "Movies/intro");
    const CnbDocument document = CnbDocument::Parse(bytes, "intro.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::Video);

    const CnbVideoData decoded = DecodeVideoFromCnb(document);
    EXPECT_EQ(decoded.streamReference, "Movies/intro.mp4");
    EXPECT_EQ(decoded.durationMs, 42000u);
    EXPECT_EQ(decoded.width, 1920u);
    EXPECT_EQ(decoded.height, 1080u);
    EXPECT_FLOAT_EQ(decoded.framesPerSecond, 29.97f);
    EXPECT_EQ(decoded.soundtrackType, 2u);
}

TEST(CnbMediaCodecTest, AMediaAssetWithoutAStreamReferenceIsRefused)
{
    // The reference is the one thing this schema cannot omit: with no media file the asset
    // describes nothing. A .cnb that merely LOOKS complete is the failure to avoid.
    CnbSongData song = MakeSong();
    song.streamReference.clear();
    try
    {
        (void)EncodeSongToCnb(song);
        FAIL() << "a Song with no media reference must be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("not the media itself"), std::string::npos)
            << e.what();
    }

    CnbVideoData video = MakeVideo();
    video.streamReference.clear();
    EXPECT_THROW((void)EncodeVideoToCnb(video), ContentLoadException);
}

TEST(CnbMediaCodecTest, AnEscapingOrAbsoluteStreamReferenceIsRefused)
{
    // The container already forbids `..` and absolute names in XREF; asserted here because this
    // schema is the first whose reference comes straight from a compiler's command line, which is
    // exactly where a traversal would enter.
    CnbSongData escaping = MakeSong();
    escaping.streamReference = "../../etc/passwd";
    EXPECT_THROW((void)EncodeSongToCnb(escaping), ContentLoadException);

    CnbSongData absolute = MakeSong();
    absolute.streamReference = "/etc/passwd";
    EXPECT_THROW((void)EncodeSongToCnb(absolute), ContentLoadException);

    CnbSongData backslash = MakeSong();
    backslash.streamReference = "Music\\\\theme.ogg";
    EXPECT_THROW((void)EncodeSongToCnb(backslash), ContentLoadException);
}

TEST(CnbMediaCodecTest, ImpossibleVideoMetadataIsRefusedOnBothSides)
{
    CnbVideoData zeroWidth = MakeVideo();
    zeroWidth.width = 0u;
    EXPECT_THROW((void)EncodeVideoToCnb(zeroWidth), ContentLoadException);

    CnbVideoData huge = MakeVideo();
    huge.height = CNA::Content::Cnb::CnbMaxVideoDimension + 1u;
    EXPECT_THROW((void)EncodeVideoToCnb(huge), ContentLoadException);

    CnbVideoData badRate = MakeVideo();
    badRate.framesPerSecond = 0.0f;
    EXPECT_THROW((void)EncodeVideoToCnb(badRate), ContentLoadException);

    // NaN is a perfectly well-formed f32 that the container stores verbatim (§2.1), so the schema
    // is the only layer that can refuse it. It would otherwise divide badly inside a player.
    CnbVideoData nanRate = MakeVideo();
    nanRate.framesPerSecond = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW((void)EncodeVideoToCnb(nanRate), ContentLoadException);

    CnbVideoData infRate = MakeVideo();
    infRate.framesPerSecond = std::numeric_limits<float>::infinity();
    EXPECT_THROW((void)EncodeVideoToCnb(infRate), ContentLoadException);

    CnbVideoData badSoundtrack = MakeVideo();
    badSoundtrack.soundtrackType = 3u;
    EXPECT_THROW((void)EncodeVideoToCnb(badSoundtrack), ContentLoadException);
}

TEST(CnbMediaCodecTest, ASongCnbLoadsThroughContentManagerAndResolvesItsMediaFile)
{
    ScratchRoot root;
    std::filesystem::create_directories(root.path() / "Music");
    { std::ofstream media(root.path() / "Music" / "theme.ogg", std::ios::binary); media << "x"; }
    const std::vector<std::uint8_t> bytes = EncodeSongToCnb(MakeSong(), "theme");
    std::ofstream out(root.path() / "theme.cnb", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();

    ContentManager cm(nullptr, root.path().string());
    auto song = cm.Load<Microsoft::Xna::Framework::Media::Song>("theme");
    EXPECT_EQ(song.getNameProperty(), "Main Theme");
    EXPECT_NEAR(song.getDurationProperty().getTotalMillisecondsProperty(), 185000.0, 1.0);
}

TEST(CnbMediaCodecTest, AMissingMediaFileIsReportedAgainstTheReferenceThatNamedIt)
{
    // The .cnb is valid; what is missing is the thing it points at. The diagnostic has to name
    // the reference, because "asset not found" against the .cnb itself would send someone
    // looking in the wrong place entirely.
    ScratchRoot root;
    const std::vector<std::uint8_t> bytes = EncodeSongToCnb(MakeSong(), "theme");
    std::ofstream out(root.path() / "theme.cnb", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();

    ContentManager cm(nullptr, root.path().string());
    try
    {
        (void)cm.Load<Microsoft::Xna::Framework::Media::Song>("theme");
        FAIL() << "a missing media file must be reported";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("Music/theme.ogg"), std::string::npos) << e.what();
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-119 -- what the runtime constructors can actually hold, and what the XREF row must say
// --------------------------------------------------------------------------------------------

namespace
{
    /// Rebuilds a media `.cnb` with the header's first `u32` -- the duration -- overwritten, and
    /// the two structural checksums repaired. That field is a `u32` on the wire, so a hostile or
    /// old file can carry a value the encoder now refuses; this is how the DECODE-side bound gets
    /// a file to refuse.
    std::vector<std::uint8_t> WithPatchedDuration(std::vector<std::uint8_t> bytes,
                                                  CNA::Content::Cnb::CnbChunkId headerChunk,
                                                  std::uint32_t durationMs)
    {
        const CnbDocument document = CnbDocument::Parse(bytes, "patch.cnb");
        const std::size_t index = document.RequireSingle(headerChunk);
        const auto offset = static_cast<std::size_t>(document.ChunkAt(index).offset);
        for (int i = 0; i < 4; ++i)
        {
            bytes[offset + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((durationMs >> (8 * i)) & 0xFFu);
        }

        // Chunk checksum, then the table-of-contents checksum, then the header's -- each of the
        // last two covers the one before it.
        const auto tocOffset = static_cast<std::size_t>(32u);
        std::uint64_t tocAt = 0u;
        for (int i = 7; i >= 0; --i) { tocAt = (tocAt << 8) | bytes[tocOffset + static_cast<std::size_t>(i)]; }
        const std::size_t entry =
            static_cast<std::size_t>(tocAt) + index * CNA::Content::Cnb::Format::TocEntrySize;
        const auto size = static_cast<std::size_t>(document.ChunkAt(index).storedSize);
        const std::uint32_t chunkChecksum =
            CNA::Content::Cnb::Crc32c(std::span<const std::uint8_t>(bytes).subspan(offset, size));
        for (int i = 0; i < 4; ++i)
        {
            bytes[entry + 32u + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((chunkChecksum >> (8 * i)) & 0xFFu);
        }

        std::uint32_t chunkCount = 0u;
        for (int i = 3; i >= 0; --i) { chunkCount = (chunkCount << 8) | bytes[20u + static_cast<std::size_t>(i)]; }
        const std::size_t tocSize = chunkCount * CNA::Content::Cnb::Format::TocEntrySize;
        const std::uint32_t tocChecksum = CNA::Content::Cnb::Crc32c(
            std::span<const std::uint8_t>(bytes).subspan(static_cast<std::size_t>(tocAt), tocSize));
        for (int i = 0; i < 4; ++i)
        {
            bytes[40u + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((tocChecksum >> (8 * i)) & 0xFFu);
        }
        const std::uint32_t headerChecksum = CNA::Content::Cnb::Crc32c(
            std::span<const std::uint8_t>(bytes).first(
                CNA::Content::Cnb::Format::HeaderChecksumCoverage));
        for (int i = 0; i < 4; ++i)
        {
            bytes[44u + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((headerChecksum >> (8 * i)) & 0xFFu);
        }
        return bytes;
    }
}

TEST(CnbMediaCodecTest, ADurationAboveInt32MaxIsRefusedAtBothBoundaries)
{
    constexpr std::uint32_t kMax = 0x7FFFFFFFu;

    // Encode side. Song's and Video's constructors both take an intcs, so a u32 above INT32_MAX
    // arrives NEGATIVE -- and every Duration/PlayPosition comparison downstream then reads
    // backwards. A 24.8-day ceiling refuses nothing real.
    CnbSongData song = MakeSong();
    song.durationMs = kMax + 1u;
    EXPECT_THROW((void)EncodeSongToCnb(song, "long"), ContentLoadException);
    song.durationMs = 0xFFFFFFFFu;
    EXPECT_THROW((void)EncodeSongToCnb(song, "long"), ContentLoadException);
    song.durationMs = kMax;
    EXPECT_NO_THROW((void)EncodeSongToCnb(song, "long")) << "exactly INT32_MAX must be accepted";

    CnbVideoData video = MakeVideo();
    video.durationMs = kMax + 1u;
    EXPECT_THROW((void)EncodeVideoToCnb(video, "long"), ContentLoadException);
    video.durationMs = kMax;
    EXPECT_NO_THROW((void)EncodeVideoToCnb(video, "long"));

    // Decode side. The field is a u32 on the wire, so a file written elsewhere -- or by a CNA
    // predating this bound -- can carry one, and the reader must refuse it rather than hand it to
    // a constructor that cannot hold it.
    const std::vector<std::uint8_t> hostileSong = WithPatchedDuration(
        EncodeSongToCnb(MakeSong(), "theme"), CNA::Content::Cnb::CnbMediaChunk::SongHeader,
        0xFFFFFFFFu);
    EXPECT_THROW((void)DecodeSongFromCnb(CnbDocument::Parse(hostileSong, "song.cnb")),
                 ContentLoadException);

    const std::vector<std::uint8_t> hostileVideo = WithPatchedDuration(
        EncodeVideoToCnb(MakeVideo(), "intro"), CNA::Content::Cnb::CnbMediaChunk::VideoHeader,
        kMax + 1u);
    EXPECT_THROW((void)DecodeVideoFromCnb(CnbDocument::Parse(hostileVideo, "video.cnb")),
                 ContentLoadException);

    // The patch helper itself must produce a loadable file, or the two refusals above would prove
    // nothing about the duration.
    const std::vector<std::uint8_t> fine = WithPatchedDuration(
        EncodeSongToCnb(MakeSong(), "theme"), CNA::Content::Cnb::CnbMediaChunk::SongHeader, kMax);
    const CnbSongData decoded = DecodeSongFromCnb(CnbDocument::Parse(fine, "song.cnb"));
    EXPECT_EQ(decoded.durationMs, kMax);
}

TEST(CnbMediaCodecTest, TheMediaReferenceRowMustHaveTheShapeTheSchemaSpecifies)
{
    // docs/cnb-format.md §19.1 specifies flags == 0 and expectedAssetTypeId == 0 for the media
    // reference, and both were written and neither was read back. flags is reserved, so a set bit
    // means a schema this build does not know; and the target is a file to stream -- an .ogg, an
    // .mp4 -- not a CNA asset, so naming an expected CNA type is a dependency this schema cannot
    // honour.
    using CNA::Content::Cnb::CnbExternalReference;
    using CNA::Content::Cnb::CnbWriter;

    const auto buildSong = [](std::uint32_t flags, std::uint32_t expectedAssetTypeId)
    {
        CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::Song, 1u);
        writer.SetMetadata("Microsoft.Xna.Framework.Media.Song", "theme");
        CnbExternalReference reference;
        reference.flags = flags;
        reference.expectedAssetTypeId = expectedAssetTypeId;
        reference.logicalName = "Music/theme.ogg";
        writer.SetExternalReferences({reference});
        CNA::Content::Cnb::CnbByteWriter header;
        header.WriteU32(1000u);
        header.WriteU32(0u);
        header.WriteString("Main Theme");
        writer.AddChunk(CNA::Content::Cnb::CnbMediaChunk::SongHeader, header.Take(),
                        CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    };

    // The well-formed shape still decodes, so the refusals below are about the edits.
    EXPECT_NO_THROW((void)DecodeSongFromCnb(
        CnbDocument::Parse(buildSong(0u, CNA::Content::Cnb::CnbAssetTypeId::Invalid), "ok.cnb")));

    // A non-zero expectedAssetTypeId. (A non-zero `flags` cannot reach here: CnbWriter refuses to
    // encode one, and the container reader refuses to decode one -- which is the right layering,
    // and is asserted rather than assumed.)
    try
    {
        (void)DecodeSongFromCnb(CnbDocument::Parse(
            buildSong(0u, CNA::Content::Cnb::CnbAssetTypeId::SoundEffect), "typed.cnb"));
        FAIL() << "a media reference expecting a CNA asset type must be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("expected type must be 0"), std::string::npos)
            << e.what();
    }
    EXPECT_THROW((void)buildSong(1u, CNA::Content::Cnb::CnbAssetTypeId::Invalid),
                 ContentLoadException)
        << "reserved XREF flags must already be refused by the writer";
}
