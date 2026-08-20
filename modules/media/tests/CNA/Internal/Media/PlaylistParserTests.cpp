// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

// plans/plan_media.md MEDIA-58: .m3u8 UTF-8 handling via the non-ASCII "Étoile" filename/title.
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

namespace
{
    // Isolated scratch directory for the REMED-CONTENT-002 containment tests below, so they don't
    // depend on (or risk mutating) the checked-in tests/assets/media/music/ fixtures.
    class ScratchPlaylistDir
    {
    public:
        ScratchPlaylistDir()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_playlist_containment_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_ / "songs");
        }
        ~ScratchPlaylistDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchPlaylistDir(const ScratchPlaylistDir&) = delete;
        ScratchPlaylistDir& operator=(const ScratchPlaylistDir&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };
}

// REMED-CONTENT-002: standard M3U allows absolute entries; CNA deliberately rejects them (see
// PlaylistParser's own class docs) -- a hostile playlist must not be able to make the engine open
// an arbitrary file elsewhere on disk.
TEST(PlaylistParserTest, AbsoluteEntryIsSkippedNotResolved)
{
    ScratchPlaylistDir root;
    std::ofstream(root.path() / "songs" / "real.mp3") << "data";

    std::ofstream playlist(root.path() / "test.m3u");
    playlist << "songs/real.mp3\n";
    playlist << "/etc/passwd\n";
    playlist.close();

    ParsedPlaylist parsed = PlaylistParser::Parse((root.path() / "test.m3u").string());

    ASSERT_EQ(parsed.songPaths.size(), 1u);
    EXPECT_NE(parsed.songPaths[0].find("real.mp3"), std::string::npos);
}

// REMED-CONTENT-002: a "..''-escaping relative entry must be rejected the same as an absolute one.
TEST(PlaylistParserTest, EscapingEntryIsSkippedNotResolved)
{
    ScratchPlaylistDir root;
    std::ofstream(root.path() / "songs" / "real.mp3") << "data";
    // A real file outside root/ that an escaping entry could otherwise reach.
    std::ofstream(std::filesystem::temp_directory_path() / "cna_playlist_containment_outside_marker.txt") << "data";

    std::ofstream playlist(root.path() / "test.m3u");
    playlist << "songs/real.mp3\n";
    playlist << "../cna_playlist_containment_outside_marker.txt\n";
    playlist.close();

    ParsedPlaylist parsed = PlaylistParser::Parse((root.path() / "test.m3u").string());

    ASSERT_EQ(parsed.songPaths.size(), 1u);
    EXPECT_NE(parsed.songPaths[0].find("real.mp3"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(std::filesystem::temp_directory_path() / "cna_playlist_containment_outside_marker.txt", ec);
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
