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

// plan_media.md MEDIA-66: real implementation, dimensions via the existing ImageLoader.
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
