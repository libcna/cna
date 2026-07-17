// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include "CNA/Internal/Media/PlaylistParser.hpp"

using CNA::Internal::Media::ParsedPlaylist;
using CNA::Internal::Media::PlaylistParser;

TEST(PlaylistParserTest, ParsesFavoritesM3UAndSkipsTheMissingEntry)
{
    ParsedPlaylist playlist = PlaylistParser::Parse("tests/assets/media/music/Favorites.m3u");
    EXPECT_EQ(playlist.name, "Favorites");
    // 4 entries in the file, 1 deliberately points at a nonexistent file -- must be skipped, not fatal.
    EXPECT_EQ(playlist.songPaths.size(), 3u);
    for (const auto& path : playlist.songPaths)
    {
        EXPECT_TRUE(std::filesystem::exists(path)) << path;
    }
}

// plan_media.md MEDIA-58: .m3u8 UTF-8 handling via the non-ASCII "Étoile" filename/title.
TEST(PlaylistParserTest, ParsesInternationalM3U8WithNonAsciiEntry)
{
    ParsedPlaylist playlist = PlaylistParser::Parse("tests/assets/media/music/International.m3u8");
    EXPECT_EQ(playlist.name, "International");
    ASSERT_EQ(playlist.songPaths.size(), 2u);

    bool foundEtoile = false;
    for (const auto& path : playlist.songPaths)
    {
        if (path.find("\xc3\x89toile") != std::string::npos) // "Étoile" in UTF-8
        {
            foundEtoile = true;
        }
    }
    EXPECT_TRUE(foundEtoile);
}

TEST(PlaylistParserTest, MissingPlaylistFileProducesEmptyResultWithoutCrashing)
{
    ParsedPlaylist playlist = PlaylistParser::Parse("tests/assets/media/music/does-not-exist.m3u");
    EXPECT_TRUE(playlist.songPaths.empty());
}

TEST(PlaylistParserTest, ScanDirectoryFindsBothFixturePlaylists)
{
    std::vector<ParsedPlaylist> playlists = PlaylistParser::ScanDirectory("tests/assets/media/music");
    ASSERT_EQ(playlists.size(), 2u);

    bool hasFavorites = false, hasInternational = false;
    for (const auto& p : playlists)
    {
        if (p.name == "Favorites") hasFavorites = true;
        if (p.name == "International") hasInternational = true;
    }
    EXPECT_TRUE(hasFavorites);
    EXPECT_TRUE(hasInternational);
}
