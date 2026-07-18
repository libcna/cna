// SPDX-License-Identifier: MS-PL

#include <fstream>

#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include <gtest/gtest.h>
#include "CNA/Internal/Media/AudioTagParser.hpp"

using CNA::Internal::Media::AudioTagParser;
using CNA::Internal::Media::AudioTags;

namespace
{
    constexpr const char* kSunriseOgg = "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
    constexpr const char* kTwilightMp3 = "tests/assets/media/music/Artist One/Album Beta/01 - Twilight.mp3";
    constexpr const char* kDaybreakMp3 = "tests/assets/media/music/Artist Two/Album Delta/02 - Daybreak.mp3";
    constexpr const char* kEtoileOgg = "tests/assets/media/music/Artist Two/Album Delta/03 - \xc3\x89toile.ogg";
    constexpr const char* kNocturneWav = "tests/assets/media/music/Artist Two/Album Gamma/01 - Nocturne.wav";
}

// plan_media.md MEDIA-47/48: real Ogg Vorbis-comment field extraction.
TEST(AudioTagParserTest, ReadsRealVorbisCommentsFromOgg)
{
    AudioTags tags = AudioTagParser::ReadTags(kSunriseOgg);
    EXPECT_TRUE(tags.fromRealTags);
    EXPECT_EQ(tags.title, "Sunrise");
    EXPECT_EQ(tags.artist, "Artist One");
    EXPECT_EQ(tags.album, "Album Alpha");
    EXPECT_EQ(tags.genre, "Rock");
    EXPECT_EQ(tags.trackNumber, 1);
}

TEST(AudioTagParserTest, ReadsNonAsciiVorbisCommentTitleCorrectly)
{
    AudioTags tags = AudioTagParser::ReadTags(kEtoileOgg);
    EXPECT_TRUE(tags.fromRealTags);
    EXPECT_EQ(tags.title, "\xc3\x89toile"); // "Étoile" in UTF-8
    EXPECT_EQ(tags.artist, "Artist Two");
    EXPECT_EQ(tags.trackNumber, 3);
}

// plan_media.md MEDIA-49/50: real ID3v2.4 text-frame extraction (Twilight.mp3 is ID3v2.4,
// confirmed via raw header bytes during fixture authoring).
TEST(AudioTagParserTest, ReadsRealId3v24TagsFromMp3)
{
    AudioTags tags = AudioTagParser::ReadTags(kTwilightMp3);
    EXPECT_TRUE(tags.fromRealTags);
    EXPECT_EQ(tags.title, "Twilight");
    EXPECT_EQ(tags.artist, "ARTIST ONE");
    EXPECT_EQ(tags.album, "Album Beta");
    EXPECT_EQ(tags.genre, "Electronic");
    EXPECT_EQ(tags.trackNumber, 1);
}

// plan_media.md MEDIA-49/50: real ID3v2.3 text-frame extraction (Daybreak.mp3 is ID3v2.3).
TEST(AudioTagParserTest, ReadsRealId3v23TagsFromMp3)
{
    AudioTags tags = AudioTagParser::ReadTags(kDaybreakMp3);
    EXPECT_TRUE(tags.fromRealTags);
    EXPECT_EQ(tags.title, "Daybreak");
    EXPECT_EQ(tags.artist, "Artist Two");
    EXPECT_EQ(tags.album, "Album Delta");
    EXPECT_EQ(tags.genre, "Rock");
    EXPECT_EQ(tags.trackNumber, 2);
}

// plan_media.md MEDIA-51: filename/folder fallback heuristic for untagged files.
TEST(AudioTagParserTest, FallsBackToFilenameFolderHeuristicForUntaggedWav)
{
    AudioTags tags = AudioTagParser::ReadTags(kNocturneWav);
    EXPECT_FALSE(tags.fromRealTags);
    EXPECT_EQ(tags.title, "01 - Nocturne");
    EXPECT_EQ(tags.album, "Album Gamma");
    EXPECT_EQ(tags.artist, "Artist Two");
    EXPECT_TRUE(tags.genre.empty());
}

TEST(AudioTagParserTest, VorbisParserFailsGracefullyOnMalformedInput)
{
    std::vector<uint8_t> garbage = {'O', 'g', 'g', 'S', 0, 1, 2, 3, 4, 5};
    AudioTags tags;
    EXPECT_FALSE(AudioTagParser::TryReadVorbisComments(garbage, tags));
}

TEST(AudioTagParserTest, VorbisParserFailsGracefullyOnEmptyInput)
{
    std::vector<uint8_t> empty;
    AudioTags tags;
    EXPECT_FALSE(AudioTagParser::TryReadVorbisComments(empty, tags));
}

TEST(AudioTagParserTest, Id3v2ParserFailsGracefullyOnMalformedInput)
{
    std::vector<uint8_t> garbage = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 10, 'T', 'I'};
    AudioTags tags;
    EXPECT_FALSE(AudioTagParser::TryReadId3v2(garbage, tags));
}

TEST(AudioTagParserTest, Id3v2ParserFailsGracefullyOnNonId3Input)
{
    std::vector<uint8_t> notId3 = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0};
    AudioTags tags;
    EXPECT_FALSE(AudioTagParser::TryReadId3v2(notId3, tags));
}

namespace
{
    // Hand-builds a minimal, real ID3v2.4 tag containing a single TIT2 (title) frame whose text
    // payload uses the given encoding byte, matching AudioTagParser.cpp's own documented matrix
    // (plan_media.md D11): 0x00=Latin-1, 0x01=UTF-16+BOM, 0x02=UTF-16BE, 0x03=UTF-8. All frame/tag
    // sizes are synchsafe (7 significant bits per byte), matching real ID3v2.4 (Twilight.mp3's own
    // real fixture format).
    std::vector<uint8_t> BuildId3v24WithTitleFrame(uint8_t encoding, const std::vector<uint8_t>& textPayload)
    {
        std::vector<uint8_t> frameData;
        frameData.push_back(encoding);
        frameData.insert(frameData.end(), textPayload.begin(), textPayload.end());

        std::vector<uint8_t> frame;
        frame.insert(frame.end(), {'T', 'I', 'T', '2'});
        uint32_t frameSize = static_cast<uint32_t>(frameData.size());
        frame.push_back(static_cast<uint8_t>((frameSize >> 21) & 0x7F));
        frame.push_back(static_cast<uint8_t>((frameSize >> 14) & 0x7F));
        frame.push_back(static_cast<uint8_t>((frameSize >> 7) & 0x7F));
        frame.push_back(static_cast<uint8_t>(frameSize & 0x7F));
        frame.push_back(0); // flags byte 1
        frame.push_back(0); // flags byte 2
        frame.insert(frame.end(), frameData.begin(), frameData.end());

        uint32_t tagSize = static_cast<uint32_t>(frame.size());
        std::vector<uint8_t> tag;
        tag.insert(tag.end(), {'I', 'D', '3', 4, 0, 0}); // "ID3" + major version 4 + revision + flags
        tag.push_back(static_cast<uint8_t>((tagSize >> 21) & 0x7F));
        tag.push_back(static_cast<uint8_t>((tagSize >> 14) & 0x7F));
        tag.push_back(static_cast<uint8_t>((tagSize >> 7) & 0x7F));
        tag.push_back(static_cast<uint8_t>(tagSize & 0x7F));
        tag.insert(tag.end(), frame.begin(), frame.end());
        return tag;
    }
}

// plan_media.md MEDIA-111: the full text-encoding-byte matrix (Latin-1/UTF-16+BOM/UTF-16BE/UTF-8)
// -- the two real MP3 fixtures only ever exercise whichever single encoding their own real tagger
// happened to write, not all four documented in AudioTagParser.cpp's own DecodeId3TextFrame.
TEST(AudioTagParserTest, Id3v2TextFrameDecodesLatin1Encoding)
{
    // 0x00 = Latin-1: 'C', 'a', 0xE9 ("Café" minus the trailing 'f' for brevity), NUL terminator.
    AudioTags tags;
    auto tag = BuildId3v24WithTitleFrame(0x00, {'C', 'a', 0xE9, 0x00});
    ASSERT_TRUE(AudioTagParser::TryReadId3v2(tag, tags));
    EXPECT_EQ(tags.title, "Ca\xC3\xA9"); // "Caé" in UTF-8
}

TEST(AudioTagParserTest, Id3v2TextFrameDecodesUtf16WithLittleEndianBom)
{
    // 0x01 = UTF-16 + BOM: FF FE (little-endian BOM) then "Aé" as UTF-16LE code units, NUL term.
    AudioTags tags;
    auto tag = BuildId3v24WithTitleFrame(
        0x01, {0xFF, 0xFE, 'A', 0x00, 0xE9, 0x00, 0x00, 0x00});
    ASSERT_TRUE(AudioTagParser::TryReadId3v2(tag, tags));
    EXPECT_EQ(tags.title, "A\xC3\xA9"); // "Aé" in UTF-8
}

TEST(AudioTagParserTest, Id3v2TextFrameDecodesUtf16WithBigEndianBom)
{
    // 0x01 = UTF-16 + BOM: FE FF (big-endian BOM) then "Aé" as UTF-16BE code units, NUL term.
    AudioTags tags;
    auto tag = BuildId3v24WithTitleFrame(
        0x01, {0xFE, 0xFF, 0x00, 'A', 0x00, 0xE9, 0x00, 0x00});
    ASSERT_TRUE(AudioTagParser::TryReadId3v2(tag, tags));
    EXPECT_EQ(tags.title, "A\xC3\xA9");
}

TEST(AudioTagParserTest, Id3v2TextFrameDecodesUtf16BigEndianWithoutBom)
{
    // 0x02 = UTF-16BE (ID3v2.4 only), no BOM: "Aé" as UTF-16BE code units, NUL terminator.
    AudioTags tags;
    auto tag = BuildId3v24WithTitleFrame(0x02, {0x00, 'A', 0x00, 0xE9, 0x00, 0x00});
    ASSERT_TRUE(AudioTagParser::TryReadId3v2(tag, tags));
    EXPECT_EQ(tags.title, "A\xC3\xA9");
}

TEST(AudioTagParserTest, Id3v2TextFrameDecodesUtf8Encoding)
{
    // 0x03 = UTF-8 (ID3v2.4 only): "Aé" already as real UTF-8 bytes, NUL terminator.
    AudioTags tags;
    auto tag = BuildId3v24WithTitleFrame(0x03, {'A', 0xC3, 0xA9, 0x00});
    ASSERT_TRUE(AudioTagParser::TryReadId3v2(tag, tags));
    EXPECT_EQ(tags.title, "A\xC3\xA9");
}

// plan_media.md MEDIA-200/202: FLAC and Opus store the SAME Vorbis-comment list as Ogg Vorbis but
// locate it completely differently -- FLAC in a native METADATA_BLOCK (type 4) after the "fLaC"
// marker, Opus behind an "OpusTags" magic instead of "\x03vorbis". The pre-existing Vorbis reader
// searches specifically for \x03vorbis, so it finds nothing in either; verified empirically before
// implementing, not assumed. Ground truth was set with ffmpeg and cross-checked via ffprobe.
TEST(AudioTagParserTest, ReadsRealFlacVorbisCommentBlock)
{
    auto tags = CNA::Internal::Media::AudioTagParser::ReadTags(
        "tests/assets/media/music/Artist Three/Album Flac/01 - Flac Song.flac");
    EXPECT_TRUE(tags.fromRealTags) << "FLAC tags must come from the real metadata block, not the filename fallback";
    EXPECT_EQ(tags.title, "Flac Song");
    EXPECT_EQ(tags.artist, "Artist Three");
    EXPECT_EQ(tags.album, "Album Flac");
    EXPECT_EQ(tags.genre, "Jazz");
    EXPECT_EQ(tags.trackNumber, 7);
}

TEST(AudioTagParserTest, ReadsRealOpusTagsHeader)
{
    auto tags = CNA::Internal::Media::AudioTagParser::ReadTags(
        "tests/assets/media/music/Artist Four/Album Opus/01 - Opus Song.opus");
    EXPECT_TRUE(tags.fromRealTags) << "Opus tags must come from the real OpusTags header";
    EXPECT_EQ(tags.title, "Opus Song");
    EXPECT_EQ(tags.artist, "Artist Four");
    EXPECT_EQ(tags.album, "Album Opus");
    EXPECT_EQ(tags.genre, "Ambient");
    EXPECT_EQ(tags.trackNumber, 9);
}

// Hostile-input hardening, matching the standard MEDIA-39/40 set for the video decoder: a
// truncated FLAC metadata block must be rejected, never read past the buffer.
TEST(AudioTagParserTest, TruncatedFlacIsRejectedWithoutReadingPastTheBuffer)
{
    std::ifstream src("tests/assets/media/music/Artist Three/Album Flac/01 - Flac Song.flac",
                       std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(src)),
                                std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 32u);

    bytes.resize(20); // keep "fLaC" + a block header claiming far more data than remains
    CNA::Internal::Media::AudioTags out;
    EXPECT_NO_THROW({
        bool ok = CNA::Internal::Media::AudioTagParser::TryReadFlacComments(bytes, out);
        EXPECT_FALSE(ok);
    });
}

// plan_media.md MEDIA-206: embedded ID3v2 APIC cover art. This closes the long-standing R2/
// MEDIA-123 follow-up. The fixture's album folder deliberately contains NO cover file, so the art
// can only come from inside the MP3 itself.
TEST(AudioTagParserTest, ExtractsEmbeddedApicArtFromMp3)
{
    std::vector<uint8_t> image;
    ASSERT_TRUE(CNA::Internal::Media::AudioTagParser::ExtractEmbeddedArt(
        "tests/assets/media/music/Artist Five/Album Embedded/01 - Embedded Art Song.mp3", image));
    ASSERT_FALSE(image.empty());

    // Must be a real JPEG (SOI marker), i.e. the raw image bytes -- not the surrounding frame.
    ASSERT_GE(image.size(), 2u);
    EXPECT_EQ(image[0], 0xFF);
    EXPECT_EQ(image[1], 0xD8) << "APIC payload is not a JPEG SOI -- frame parsing is misaligned";

    // And it must decode to the real picture, proving the payload boundaries are exactly right.
    const auto decoded =
        CNA::Internal::Graphics::ImageLoader::LoadFromMemory(image.data(), image.size());
    EXPECT_EQ(decoded.width, 400);
    EXPECT_EQ(decoded.height, 300);
}

TEST(AudioTagParserTest, ReturnsFalseForAFileWithNoEmbeddedArt)
{
    std::vector<uint8_t> image;
    EXPECT_FALSE(CNA::Internal::Media::AudioTagParser::ExtractEmbeddedArt(
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg", image));
    EXPECT_TRUE(image.empty());
}

// plan_media.md MEDIA-182: ID3v2 POPM (Popularimeter). Fixture is hand-built byte-by-byte because
// ffmpeg cannot write POPM -- same technique this project already uses for hand-constructed .xnb
// containers. The rating byte is 196, and 196*10/255 rounds to 8 on XNA's 0-10 scale.
TEST(AudioTagParserTest, ReadsId3v2PopmRatingAndConvertsToXnaScale)
{
    auto tags = CNA::Internal::Media::AudioTagParser::ReadTags(
        "tests/assets/media/music/Artist Six/Album Rated/04 - Rated Song.mp3");
    EXPECT_TRUE(tags.hasRating);
    EXPECT_EQ(tags.rating, 8) << "POPM byte 196 should map to 8 on the 0-10 scale";
    // The hand-built tag's other frames must also parse, proving the frame walk stayed aligned
    // across the POPM frame rather than desynchronising on it.
    EXPECT_EQ(tags.title, "Rated Song");
    EXPECT_EQ(tags.genre, "Soul");
    EXPECT_EQ(tags.trackNumber, 4);
}

// plan_media.md MEDIA-183: the Vorbis RATING comment, treated as a 0-100 scale (80 -> 8).
TEST(AudioTagParserTest, ReadsVorbisRatingCommentAndConvertsToXnaScale)
{
    auto tags = CNA::Internal::Media::AudioTagParser::ReadTags(
        "tests/assets/media/music/Artist Seven/Album Vorbis Rated/02 - Vorbis Rated Song.flac");
    EXPECT_TRUE(tags.hasRating);
    EXPECT_EQ(tags.rating, 8) << "RATING=80 on the 0-100 scale should map to 8";
    EXPECT_EQ(tags.artist, "Artist Seven");
}

// Both formats reserve 0 for "unrated", so an unrated file must report hasRating == false rather
// than a real rating that happens to be zero -- that distinction IS what XNA's IsRated means.
TEST(AudioTagParserTest, UnratedFileReportsNoRatingRatherThanARatingOfZero)
{
    auto tags = CNA::Internal::Media::AudioTagParser::ReadTags(
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg");
    EXPECT_FALSE(tags.hasRating);
    EXPECT_EQ(tags.rating, 0);
}
