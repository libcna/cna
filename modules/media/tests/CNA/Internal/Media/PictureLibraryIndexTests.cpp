// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <filesystem>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <gtest/gtest.h>
#include "CNA/Internal/Media/PictureLibraryIndex.hpp"

using CNA::Internal::Media::IndexedPicture;
using CNA::Internal::Media::PictureAlbumNode;
using CNA::Internal::Media::PictureLibraryIndex;

namespace
{
    constexpr const char* kPicturesRoot = "tests/assets/media/pictures";

    const IndexedPicture* FindByName(const std::vector<IndexedPicture>& pics, const std::string& name)
    {
        auto it = std::find_if(pics.begin(), pics.end(),
                                [&](const IndexedPicture& p) { return p.name == name; });
        return it == pics.end() ? nullptr : &*it;
    }
}

TEST(PictureLibraryIndexTest, ScansAllThreeFixturePictures)
{
    PictureLibraryIndex index(kPicturesRoot);
    EXPECT_EQ(index.GetPictures().size(), 3u);
}

// plans/plan_media.md MEDIA-56/D4: dimensions come from the real, already-existing ImageLoader, not a
// reimplemented decoder.
TEST(PictureLibraryIndexTest, ReadsRealDimensionsViaImageLoader)
{
    PictureLibraryIndex index(kPicturesRoot);
    const auto& pics = index.GetPictures();

    const IndexedPicture* beach = FindByName(pics, "beach");
    ASSERT_NE(beach, nullptr);
    EXPECT_EQ(beach->width, 64);
    EXPECT_EQ(beach->height, 48);

    const IndexedPicture* sunset = FindByName(pics, "sunset");
    ASSERT_NE(sunset, nullptr);
    EXPECT_EQ(sunset->width, 32);
    EXPECT_EQ(sunset->height, 32);

    const IndexedPicture* portrait = FindByName(pics, "portrait");
    ASSERT_NE(portrait, nullptr);
    EXPECT_EQ(portrait->width, 100);
    EXPECT_EQ(portrait->height, 80);
}

// plans/plan_media.md MEDIA-56/MEDIA-67: real parent/child album tree matching the actual directory
// structure -- Vacation/Day 2/sunset.png is 2 levels deep, Family/portrait.png is 1 level deep.
TEST(PictureLibraryIndexTest, BuildsARealParentChildAlbumTree)
{
    PictureLibraryIndex index(kPicturesRoot);
    const auto& albums = index.GetAlbums();
    const std::string& root = index.GetRootAlbumPath();
    ASSERT_FALSE(root.empty());

    auto rootIt = albums.find(root);
    ASSERT_NE(rootIt, albums.end());
    EXPECT_EQ(rootIt->second.childPaths.size(), 2u); // Vacation, Family

    // Find the Vacation node (child of root, named "Vacation").
    const PictureAlbumNode* vacation = nullptr;
    for (const auto& childPath : rootIt->second.childPaths)
    {
        if (albums.at(childPath).name == "Vacation") vacation = &albums.at(childPath);
    }
    ASSERT_NE(vacation, nullptr);
    EXPECT_EQ(vacation->pictureIndices.size(), 1u); // beach.jpg
    EXPECT_EQ(vacation->childPaths.size(), 1u);      // Day 2

    const PictureAlbumNode& day2 = albums.at(vacation->childPaths[0]);
    EXPECT_EQ(day2.name, "Day 2");
    EXPECT_EQ(day2.parentPath, vacation->path);
    EXPECT_EQ(day2.pictureIndices.size(), 1u); // sunset.png
}

TEST(PictureLibraryIndexTest, EmptyOrMissingRootProducesNoResultsWithoutCrashing)
{
    PictureLibraryIndex missing("tests/assets/media/this-directory-does-not-exist");
    EXPECT_TRUE(missing.GetPictures().empty());
}

// REMED-TEST-004(c): PictureLibraryIndex.cpp's ScanDirectory() already has an explicit
// `visited` weakly_canonical-keyed cycle guard ("cycle guard, matching MediaLibraryIndex's own
// approach") -- this coverage gap test is expected to PASS, proving the guard that already exists
// actually works, matching MediaLibraryIndexTest.TerminatesOnASelfReferentialSymlinkCycle for the
// music scanner in this same shard.
TEST(PictureLibraryIndexTest, TerminatesOnASelfReferentialSymlinkCycle)
{
    std::filesystem::path root = "tests/assets/media/.picture_symlink_cycle_test_fixture";
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

    PictureLibraryIndex index(root.string());
    // The real assertion is implicit: this constructor call must return at all (a regression here
    // would hang the whole test binary, not just fail an assertion).
    EXPECT_TRUE(index.GetPictures().empty());

    std::filesystem::remove_all(root, ec);
}

// REMED-TEST-004(c): an unreadable subdirectory (real file-permission denial) must be silently
// skipped via std::filesystem::directory_options::skip_permission_denied, not crash or throw --
// PictureLibraryIndex.cpp already passes that option to its directory_iterator. Matches
// MediaLibraryIndexTest.SkipsAnUnreadableSubdirectoryWithoutCrashing's own pattern for the music
// scanner in this same shard. Skips itself if running as root, since root bypasses Unix permission
// bits entirely and the scenario genuinely can't be exercised there.
TEST(PictureLibraryIndexTest, SkipsAnUnreadableSubdirectoryWithoutCrashing)
{
#ifdef _WIN32
    // REMED-BUILD-013 (discovered while verifying it): see MediaLibraryIndexTests.cpp's own
    // sibling test for the full rationale -- geteuid() doesn't exist on Windows/MinGW, and Unix
    // permission bits aren't the enforcement mechanism there either.
    GTEST_SKIP() << "Unix permission-bits scenario does not apply on Windows";
#else
    if (::geteuid() == 0)
    {
        GTEST_SKIP() << "running as root -- permission bits don't restrict access, can't exercise this";
    }
#endif

    std::filesystem::path root = "tests/assets/media/.picture_permission_denied_test_fixture";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Readable Album", ec);
    std::filesystem::create_directories(root / "Locked Album", ec);
    ASSERT_FALSE(ec);

    std::filesystem::copy_file(
        "tests/assets/media/pictures/Vacation/beach.jpg",
        root / "Readable Album" / "beach.jpg", ec);
    std::filesystem::copy_file(
        "tests/assets/media/pictures/Family/portrait.png",
        root / "Locked Album" / "portrait.png", ec);
    ASSERT_FALSE(ec);

    std::filesystem::path lockedDir = root / "Locked Album";
    std::filesystem::permissions(lockedDir, std::filesystem::perms::none, ec);
    ASSERT_FALSE(ec);

    PictureLibraryIndex index(root.string());

    // Restore permissions before cleanup -- remove_all can't recurse into a directory it can't
    // read/traverse.
    std::filesystem::permissions(
        lockedDir,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec,
        ec);
    std::filesystem::remove_all(root, ec);

    // The readable picture is still found; the locked one is silently skipped, not a thrown
    // exception or a crash.
    ASSERT_EQ(index.GetPictures().size(), 1u);
    EXPECT_EQ(index.GetPictures()[0].name, "beach");
}
