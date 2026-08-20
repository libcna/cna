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

// plans/plan_media.md MEDIA-64: real implementation backed by MediaLibraryIndex artist grouping.
TEST_F(MediaLibraryTestFixture, ArtistsContainsEveryFixtureArtist)
{
    EXPECT_GE(library->getArtistsProperty()->getCountProperty(), 4); // corpus grows; presence below is the real contract
    EXPECT_NE(FindArtist(library->getArtistsProperty(), "Artist One"), nullptr);
    EXPECT_NE(FindArtist(library->getArtistsProperty(), "Artist Two"), nullptr);
}

// plans/plan_media.md MEDIA-119: the case-variant-artist-tag regression itself now lives in its own
// dedicated, isolated file -- see ArtistGenreNormalizationRegressionTests.cpp.

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

// plans/plan_media.md MEDIA-101: the in-bounds case, not just the out-of-range case above.
TEST_F(MediaLibraryTestFixture, ArtistCollectionIndexerReturnsArtistsInBounds)
{
    // Derived from the live Count rather than hardcoded: the shared fixture corpus grows as
    // new formats/features get coverage (plans/plan_media.md MEDIA-199/206), and an unrelated
    // indexer test should not break every time it does.
    auto* artists = library->getArtistsProperty();
    const auto count = artists->getCountProperty();
    ASSERT_GT(count, 0);
    for (SharpRuntime::intcs i = 0; i < count; ++i)
    {
        EXPECT_NE((*artists)[i], nullptr) << "index " << i;
    }
    EXPECT_THROW((void)(*artists)[count], System::ArgumentOutOfRangeException);
}

// plans/plan_media.md MEDIA-100: Artist::IsDisposed, not exercised anywhere else in this file.
TEST_F(MediaLibraryTestFixture, ArtistDisposeFlipsIsDisposed)
{
    Artist* artistOne = FindArtist(library->getArtistsProperty(), "Artist One");
    ASSERT_NE(artistOne, nullptr);
    ASSERT_FALSE(artistOne->getIsDisposedProperty());

    artistOne->Dispose();

    EXPECT_TRUE(artistOne->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-101: ArtistCollection's own Dispose()/IsDisposed.
TEST_F(MediaLibraryTestFixture, ArtistCollectionDisposeFlipsIsDisposed)
{
    auto* artists = library->getArtistsProperty();
    ASSERT_FALSE(artists->getIsDisposedProperty());

    artists->Dispose();

    EXPECT_TRUE(artists->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-121 (found by external code review): ArtistCollection's own GetTypeName().
TEST_F(MediaLibraryTestFixture, ArtistCollectionGetTypeNameIsFullyQualified)
{
    EXPECT_EQ(library->getArtistsProperty()->GetTypeName(), "Microsoft.Xna.Framework.Media.ArtistCollection");
}
