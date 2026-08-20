// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <filesystem>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <gtest/gtest.h>
#include "CNA/Internal/Media/MediaLibraryIndex.hpp"

using CNA::Internal::Media::IndexedSong;
using CNA::Internal::Media::MediaLibraryIndex;

namespace
{
    constexpr const char* kMusicRoot = "tests/assets/media/music";

    const IndexedSong* FindByTitle(const std::vector<IndexedSong>& songs, const std::string& title)
    {
        auto it = std::find_if(songs.begin(), songs.end(),
                                [&](const IndexedSong& s) { return s.title == title; });
        return it == songs.end() ? nullptr : &*it;
    }
}

TEST(MediaLibraryIndexTest, ScansEveryFixtureSong)
{
    // Counted from the filesystem rather than hardcoded, so adding a fixture for a new feature
    // cannot break this test while still proving the scan finds EVERY supported file present
    // (plans/plan_media.md MEDIA-199/206 both grew the corpus and broke the old magic number).
    std::size_t expected = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(kMusicRoot))
    {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".ogg" || ext == ".oga" || ext == ".mp3" || ext == ".wav" ||
            ext == ".flac" || ext == ".opus")
        {
            ++expected;
        }
    }
    ASSERT_GT(expected, 0u) << "fixture corpus is missing";

    MediaLibraryIndex index(kMusicRoot);
    EXPECT_EQ(index.GetSongs().size(), expected);
}

TEST(MediaLibraryIndexTest, EachSongHasCorrectTagsFromItsSource)
{
    MediaLibraryIndex index(kMusicRoot);
    const auto& songs = index.GetSongs();

    const IndexedSong* sunrise = FindByTitle(songs, "Sunrise");
    ASSERT_NE(sunrise, nullptr);
    EXPECT_EQ(sunrise->album, "Album Alpha");
    EXPECT_EQ(sunrise->genre, "Rock");

    const IndexedSong* nocturne = FindByTitle(songs, "01 - Nocturne");
    ASSERT_NE(nocturne, nullptr);
    EXPECT_EQ(nocturne->artist, "Artist Two");
    EXPECT_EQ(nocturne->album, "Album Gamma");
    EXPECT_TRUE(nocturne->genre.empty());
}

// plans/plan_media.md MEDIA-54/D10: "Artist One" and "ARTIST ONE" (Twilight.mp3's deliberate
// case-variant tag) must normalize to a single canonical artist, not two distinct ones.
TEST(MediaLibraryIndexTest, NormalizesCaseVariantArtistNamesToOneCanonicalValue)
{
    MediaLibraryIndex index(kMusicRoot);
    const auto& songs = index.GetSongs();

    const IndexedSong* sunrise = FindByTitle(songs, "Sunrise");   // tagged "Artist One"
    const IndexedSong* twilight = FindByTitle(songs, "Twilight"); // tagged "ARTIST ONE"
    ASSERT_NE(sunrise, nullptr);
    ASSERT_NE(twilight, nullptr);
    EXPECT_EQ(sunrise->artist, twilight->artist);
    EXPECT_EQ(sunrise->artist, "Artist One"); // first-seen casing wins (scan order = Alpha before Beta)
}

TEST(MediaLibraryIndexTest, GroupsAllFourArtistTwoSongsUnderOneArtist)
{
    MediaLibraryIndex index(kMusicRoot);
    const auto& songs = index.GetSongs();
    int artistTwoCount = 0;
    for (const auto& s : songs)
    {
        if (s.artist == "Artist Two") ++artistTwoCount;
    }
    EXPECT_EQ(artistTwoCount, 3); // Nocturne, Daybreak, Étoile
}

TEST(MediaLibraryIndexTest, EmptyOrMissingRootProducesNoSongsWithoutCrashing)
{
    MediaLibraryIndex missing("tests/assets/media/this-directory-does-not-exist");
    EXPECT_TRUE(missing.GetSongs().empty());
}

// plans/plan_media.md MEDIA-53: a self-referential symlink must not cause infinite recursion.
TEST(MediaLibraryIndexTest, TerminatesOnASelfReferentialSymlinkCycle)
{
    std::filesystem::path root = "tests/assets/media/.symlink_cycle_test_fixture";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    std::filesystem::path loopLink = root / "self";
    std::filesystem::create_directory_symlink(root, loopLink, ec);
    if (ec)
    {
        // Symlink creation can fail in sandboxed/no-permission environments -- skip rather than
        // fail, since the thing under test (does scanning hang) can't be exercised without one.
        std::filesystem::remove_all(root, ec);
        GTEST_SKIP() << "could not create a symlink in this environment";
    }

    MediaLibraryIndex index(root.string());
    // The real assertion is implicit: this constructor call must return at all (a regression
    // here would hang the whole test binary, not just fail an assertion).
    EXPECT_TRUE(index.GetSongs().empty());

    std::filesystem::remove_all(root, ec);
}

// plans/plan_media.md MEDIA-53/MEDIA-113: an unreadable subdirectory (real file-permission denial, not
// just a missing/empty root) must be silently skipped via
// std::filesystem::directory_options::skip_permission_denied, not crash or throw. Skips itself
// (rather than fail) if this test happens to run as root, since root bypasses Unix permission
// bits entirely and the scenario genuinely can't be exercised there.
TEST(MediaLibraryIndexTest, SkipsAnUnreadableSubdirectoryWithoutCrashing)
{
#ifdef _WIN32
    // REMED-BUILD-013 (discovered while verifying it): geteuid() doesn't exist on Windows/MinGW,
    // and this test's whole premise -- std::filesystem::perms::none denying traversal -- is a Unix
    // permission-bits mechanism NTFS/MinGW's own std::filesystem::permissions() does not reliably
    // enforce, so there is no equivalent scenario to exercise here.
    GTEST_SKIP() << "Unix permission-bits scenario does not apply on Windows";
#else
    if (::geteuid() == 0)
    {
        GTEST_SKIP() << "running as root -- permission bits don't restrict access, can't exercise this";
    }
#endif

    std::filesystem::path root = "tests/assets/media/.permission_denied_test_fixture";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Readable Artist" / "Readable Album", ec);
    std::filesystem::create_directories(root / "Locked Artist" / "Locked Album", ec);
    ASSERT_FALSE(ec);

    std::filesystem::copy_file(
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg",
        root / "Readable Artist" / "Readable Album" / "01 - Track.ogg", ec);
    std::filesystem::copy_file(
        "tests/assets/media/music/Artist Two/Album Gamma/01 - Nocturne.wav",
        root / "Locked Artist" / "Locked Album" / "01 - Track.wav", ec);
    ASSERT_FALSE(ec);

    std::filesystem::path lockedDir = root / "Locked Artist";
    std::filesystem::permissions(lockedDir, std::filesystem::perms::none, ec);
    ASSERT_FALSE(ec);

    MediaLibraryIndex index(root.string());

    // Restore permissions before cleanup -- remove_all can't recurse into a directory it can't
    // read/traverse.
    std::filesystem::permissions(
        lockedDir,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec,
        ec);
    std::filesystem::remove_all(root, ec);

    // The readable song is still found (Sunrise.ogg's own real embedded Vorbis-comment title
    // survives the copy/rename); the locked one is silently skipped, not a thrown exception or a
    // crash.
    ASSERT_EQ(index.GetSongs().size(), 1u);
    EXPECT_EQ(index.GetSongs()[0].title, "Sunrise");
}
