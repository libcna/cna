// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <fstream>

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Picture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "System/IO/Stream.hpp"

using Microsoft::Xna::Framework::Media::Picture;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Picture* FindPicture(Microsoft::Xna::Framework::Media::PictureCollection* pics, const std::string& name)
    {
        for (Picture* p : *pics)
        {
            if (p->getNameProperty() == name) return p;
        }
        return nullptr;
    }
}

// plans/plan_media.md MEDIA-66: real implementation, dimensions via the existing ImageLoader.
TEST_F(MediaLibraryTestFixture, PicturesContainsAllThreeFixtureImages)
{
    EXPECT_EQ(library->getPicturesProperty()->getCountProperty(), 3);
}

TEST_F(MediaLibraryTestFixture, PictureDimensionsMatchFixtureFiles)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);
    EXPECT_EQ(beach->getWidthProperty(), 64);
    EXPECT_EQ(beach->getHeightProperty(), 48);

    Picture* portrait = FindPicture(library->getPicturesProperty(), "portrait");
    ASSERT_NE(portrait, nullptr);
    EXPECT_EQ(portrait->getWidthProperty(), 100);
    EXPECT_EQ(portrait->getHeightProperty(), 80);
}

TEST_F(MediaLibraryTestFixture, GetImageContentRoundTripsByteForByte)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);

    std::ifstream src("tests/assets/media/pictures/Vacation/beach.jpg", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    // uint8_t, not char -- comparing a signed vector<char> against vector<uint8_t> via std::equal
    // promotes both to int, so any byte >= 0x80 (common throughout real binary JPEG data) would
    // never compare equal (e.g. char(-1) vs uint8_t(255) promote to int(-1) vs int(255)).
    std::vector<uint8_t> expected((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());

    System::IO::Stream* stream = beach->GetImage();
    ASSERT_NE(stream, nullptr);
    std::vector<uint8_t> actual(static_cast<std::size_t>(stream->getLengthProperty()));
    if (!actual.empty())
    {
        stream->Read(actual.data(), 0, static_cast<SharpRuntime::intcs>(actual.size()));
    }
    delete stream;

    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin()));
}

TEST_F(MediaLibraryTestFixture, PictureEqualitySetByResolvedPath)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    Picture* portrait = FindPicture(library->getPicturesProperty(), "portrait");
    ASSERT_NE(beach, nullptr);
    ASSERT_NE(portrait, nullptr);

    EXPECT_TRUE(beach->Equals(beach));
    EXPECT_FALSE(beach->Equals(portrait));
    EXPECT_TRUE(*beach == *beach);
    EXPECT_TRUE(*beach != *portrait);
}

// plans/plan_media.md MEDIA-234: cover the System.Object override separately from Equals(Picture).
TEST_F(MediaLibraryTestFixture, PictureObjectEqualsChecksRuntimeTypeAndValue)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    Picture* portrait = FindPicture(library->getPicturesProperty(), "portrait");
    ASSERT_NE(beach, nullptr);
    ASSERT_NE(portrait, nullptr);

    const System::Object* equalPicture = beach;
    const System::Object* unequalPicture = portrait;
    const System::Object* wrongType = library.get();
    const System::Object* nullObject = nullptr;
    EXPECT_TRUE(beach->Equals(equalPicture));
    EXPECT_FALSE(beach->Equals(unequalPicture));
    EXPECT_FALSE(beach->Equals(wrongType));
    EXPECT_FALSE(beach->Equals(nullObject));
}

TEST_F(MediaLibraryTestFixture, PictureAlbumPropertyPointsToContainingAlbum)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);
    ASSERT_NE(beach->getAlbumProperty(), nullptr);
    EXPECT_EQ(beach->getAlbumProperty()->getNameProperty(), "Vacation");
}

TEST_F(MediaLibraryTestFixture, PictureGetTypeNameIsFullyQualified)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);
    EXPECT_EQ(beach->GetTypeName(), "Microsoft.Xna.Framework.Media.Picture");
}

// plans/plan_media.md MEDIA-104: Date -- sourced from the real file's last-write-time
// (PictureLibraryIndex), not exercised by any other test above.
TEST_F(MediaLibraryTestFixture, PictureDateIsARealNonDefaultTimestamp)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);
    EXPECT_NE(beach->getDateProperty(), std::chrono::system_clock::time_point{});
}

// plans/plan_media.md MEDIA-104: GetThumbnail() -- only GetImage() was covered above.
// beach.jpg is 64x48 -- already within ThumbnailGenerator::MaxEdge (128), so no downscale is
// needed and GetThumbnail() deliberately serves the ORIGINAL bytes rather than pointlessly
// re-encoding an in-spec image (plans/plan_media.md MEDIA-210). Oversized sources genuinely are
// downscaled -- see AlbumTests' GetThumbnailReturnsAGenuinelySmallerImageThanGetAlbumArt, which
// uses a 200x200 cover. This test therefore asserts the small-image contract, NOT that
// GetThumbnail is a synonym for GetImage.
TEST_F(MediaLibraryTestFixture, PictureGetThumbnailRoundTripsByteForByteForAnAlreadySmallImage)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);

    std::ifstream src("tests/assets/media/pictures/Vacation/beach.jpg", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<uint8_t> expected((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());

    System::IO::Stream* stream = beach->GetThumbnail();
    ASSERT_NE(stream, nullptr);
    std::vector<uint8_t> actual(static_cast<std::size_t>(stream->getLengthProperty()));
    if (!actual.empty())
    {
        stream->Read(actual.data(), 0, static_cast<SharpRuntime::intcs>(actual.size()));
    }
    delete stream;

    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin()));
}

// plans/plan_media.md MEDIA-104: IsDisposed, not exercised anywhere else in this file.
TEST_F(MediaLibraryTestFixture, PictureDisposeFlipsIsDisposed)
{
    Picture* beach = FindPicture(library->getPicturesProperty(), "beach");
    ASSERT_NE(beach, nullptr);
    ASSERT_FALSE(beach->getIsDisposedProperty());

    beach->Dispose();

    EXPECT_TRUE(beach->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-105: PictureCollection's own indexer (in-bounds) and Dispose()/IsDisposed --
// only Count was previously checked.
TEST_F(MediaLibraryTestFixture, PictureCollectionIndexerReturnsPicturesInBounds)
{
    auto* pics = library->getPicturesProperty();
    ASSERT_EQ(pics->getCountProperty(), 3);
    for (SharpRuntime::intcs i = 0; i < pics->getCountProperty(); ++i)
    {
        EXPECT_NE((*pics)[i], nullptr);
    }
}

TEST_F(MediaLibraryTestFixture, PictureCollectionDisposeFlipsIsDisposed)
{
    auto* pics = library->getPicturesProperty();
    ASSERT_FALSE(pics->getIsDisposedProperty());

    pics->Dispose();

    EXPECT_TRUE(pics->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-121 (found by external code review): PictureCollection's own GetTypeName().
TEST_F(MediaLibraryTestFixture, PictureCollectionGetTypeNameIsFullyQualified)
{
    EXPECT_EQ(library->getPicturesProperty()->GetTypeName(), "Microsoft.Xna.Framework.Media.PictureCollection");
}
