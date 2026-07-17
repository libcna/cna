// SPDX-License-Identifier: MS-PL

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Media::Artist;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Artist* FindArtist(Microsoft::Xna::Framework::Media::ArtistCollection* artists, const std::string& name)
    {
        for (Artist* a : *artists)
        {
            if (a->getNameProperty() == name) return a;
        }
        return nullptr;
    }
}

// plan_media.md MEDIA-64: real implementation backed by MediaLibraryIndex artist grouping.
TEST_F(MediaLibraryTestFixture, ArtistsContainsBothArtists)
{
    EXPECT_EQ(library->getArtistsProperty()->getCountProperty(), 2);
    EXPECT_NE(FindArtist(library->getArtistsProperty(), "Artist One"), nullptr);
    EXPECT_NE(FindArtist(library->getArtistsProperty(), "Artist Two"), nullptr);
}

// plan_media.md MEDIA-54/D10: "ARTIST ONE" (Twilight.mp3's case-variant tag) must NOT create a
// second, distinct Artist entry -- confirms the normalization applied in MediaLibraryIndex flows
// through end-to-end into the real public API.
TEST_F(MediaLibraryTestFixture, CaseVariantArtistTagDidNotCreateADuplicateArtist)
{
    EXPECT_EQ(FindArtist(library->getArtistsProperty(), "ARTIST ONE"), nullptr);
    Artist* artistOne = FindArtist(library->getArtistsProperty(), "Artist One");
    ASSERT_NE(artistOne, nullptr);
    EXPECT_EQ(artistOne->getSongsProperty()->getCountProperty(), 2); // Sunrise + Twilight
}

TEST_F(MediaLibraryTestFixture, ArtistOneHasTwoAlbums)
{
    Artist* artistOne = FindArtist(library->getArtistsProperty(), "Artist One");
    ASSERT_NE(artistOne, nullptr);
    EXPECT_EQ(artistOne->getAlbumsProperty()->getCountProperty(), 2); // Alpha, Beta
}

TEST_F(MediaLibraryTestFixture, ArtistTwoHasThreeSongsAndTwoAlbums)
{
    Artist* artistTwo = FindArtist(library->getArtistsProperty(), "Artist Two");
    ASSERT_NE(artistTwo, nullptr);
    EXPECT_EQ(artistTwo->getSongsProperty()->getCountProperty(), 3); // Nocturne, Daybreak, Étoile
    EXPECT_EQ(artistTwo->getAlbumsProperty()->getCountProperty(), 2); // Gamma, Delta
}

TEST_F(MediaLibraryTestFixture, ArtistEqualitySetForEqualAndUnequalArtists)
{
    Artist* artistOne = FindArtist(library->getArtistsProperty(), "Artist One");
    Artist* artistTwo = FindArtist(library->getArtistsProperty(), "Artist Two");
    ASSERT_NE(artistOne, nullptr);
    ASSERT_NE(artistTwo, nullptr);

    EXPECT_TRUE(artistOne->Equals(artistOne));
    EXPECT_FALSE(artistOne->Equals(artistTwo));
    EXPECT_TRUE(*artistOne == *artistOne);
    EXPECT_TRUE(*artistOne != *artistTwo);
}

TEST_F(MediaLibraryTestFixture, ArtistGetTypeNameIsFullyQualified)
{
    Artist* artistOne = FindArtist(library->getArtistsProperty(), "Artist One");
    ASSERT_NE(artistOne, nullptr);
    EXPECT_EQ(artistOne->GetTypeName(), "Microsoft.Xna.Framework.Media.Artist");
}

TEST_F(MediaLibraryTestFixture, ArtistCollectionIndexerThrowsOutOfRange)
{
    auto* artists = library->getArtistsProperty();
    EXPECT_THROW((void)(*artists)[-1], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)(*artists)[static_cast<SharpRuntime::intcs>(artists->getCountProperty())],
                 System::ArgumentOutOfRangeException);
}
