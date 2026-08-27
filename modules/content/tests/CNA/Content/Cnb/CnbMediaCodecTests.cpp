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
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
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
