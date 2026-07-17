// SPDX-License-Identifier: MS-PL

#include <algorithm>
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

// plan_media.md MEDIA-56/D4: dimensions come from the real, already-existing ImageLoader, not a
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

// plan_media.md MEDIA-56/MEDIA-67: real parent/child album tree matching the actual directory
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
