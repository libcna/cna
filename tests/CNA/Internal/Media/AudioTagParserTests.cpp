// SPDX-License-Identifier: MS-PL

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
