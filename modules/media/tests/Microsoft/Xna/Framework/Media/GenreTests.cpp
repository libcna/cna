// SPDX-License-Identifier: MS-PL

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Media::Genre;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Genre* FindGenre(Microsoft::Xna::Framework::Media::GenreCollection* genres, const std::string& name)
    {
        for (Genre* g : *genres)
        {
            if (g->getNameProperty() == name) return g;
        }
        return nullptr;
    }
}

// plans/plan_media.md MEDIA-63: real implementation backed by MediaLibraryIndex genre grouping.
TEST_F(MediaLibraryTestFixture, GenresContainsEveryFixtureGenre)
{
    EXPECT_GE(library->getGenresProperty()->getCountProperty(), 4); // corpus grows; presence below is the real contract
    EXPECT_NE(FindGenre(library->getGenresProperty(), "Rock"), nullptr);
    EXPECT_NE(FindGenre(library->getGenresProperty(), "Electronic"), nullptr);
}

TEST_F(MediaLibraryTestFixture, RockGenreHasThreeSongs)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(rock->getSongsProperty()->getCountProperty(), 3); // Sunrise, Daybreak, Étoile
    EXPECT_FALSE(rock->getIsDisposedProperty());
}

TEST_F(MediaLibraryTestFixture, ElectronicGenreHasOneSong)
{
    Genre* electronic = FindGenre(library->getGenresProperty(), "Electronic");
    ASSERT_NE(electronic, nullptr);
    EXPECT_EQ(electronic->getSongsProperty()->getCountProperty(), 1); // Twilight
}

TEST_F(MediaLibraryTestFixture, GenreEqualitySetForEqualAndUnequalGenres)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    Genre* electronic = FindGenre(library->getGenresProperty(), "Electronic");
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(electronic, nullptr);

    EXPECT_TRUE(rock->Equals(rock));
    EXPECT_FALSE(rock->Equals(electronic));
    EXPECT_TRUE(*rock == *rock);
    EXPECT_TRUE(*rock != *electronic);
    EXPECT_EQ(rock->GetHashCode(), rock->GetHashCode());
}

// plans/plan_media.md MEDIA-234: cover the System.Object override separately from Equals(Genre).
TEST_F(MediaLibraryTestFixture, GenreObjectEqualsChecksRuntimeTypeAndValue)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    Genre* electronic = FindGenre(library->getGenresProperty(), "Electronic");
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(electronic, nullptr);

    const System::Object* equalGenre = rock;
    const System::Object* unequalGenre = electronic;
    const System::Object* wrongType = library.get();
    const System::Object* nullObject = nullptr;
    EXPECT_TRUE(rock->Equals(equalGenre));
    EXPECT_FALSE(rock->Equals(unequalGenre));
    EXPECT_FALSE(rock->Equals(wrongType));
    EXPECT_FALSE(rock->Equals(nullObject));
}

TEST_F(MediaLibraryTestFixture, GenreToStringReturnsName)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(rock->ToString(), "Rock");
}

TEST_F(MediaLibraryTestFixture, GenreGetTypeNameIsFullyQualified)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(rock->GetTypeName(), "Microsoft.Xna.Framework.Media.Genre");
}

// plans/plan_media.md MEDIA-57 (GenreCollectionTests folded in): indexer/count/dispose/iteration.
TEST_F(MediaLibraryTestFixture, GenreCollectionIndexerAndIterationWork)
{
    auto* genres = library->getGenresProperty();
    const auto genreCount = genres->getCountProperty();
    ASSERT_GT(genreCount, 0);

    // Derived from the live count rather than hardcoded, so growing the fixture corpus (e.g. the
    // FLAC/Opus files added for plans/plan_media.md MEDIA-199) exercises the same contract instead of
    // breaking an unrelated indexer test.
    for (SharpRuntime::intcs i = 0; i < genreCount; ++i)
    {
        EXPECT_NE((*genres)[i], nullptr) << "index " << i;
    }
    EXPECT_THROW((void)(*genres)[genreCount], System::ArgumentOutOfRangeException);

    SharpRuntime::intcs count = 0;
    for (Genre* g : *genres) { EXPECT_NE(g, nullptr); ++count; }
    EXPECT_EQ(count, genreCount) << "iteration must visit exactly Count items";
}

// plans/plan_media.md MEDIA-121 (found by external code review): GenreCollection's own GetTypeName(),
// not exercised anywhere else in this file (only Genre's own was tested).
TEST_F(MediaLibraryTestFixture, GenreCollectionGetTypeNameIsFullyQualified)
{
    EXPECT_EQ(library->getGenresProperty()->GetTypeName(), "Microsoft.Xna.Framework.Media.GenreCollection");
}

// plans/plan_media.md MEDIA-98: Albums property, not exercised by any other test above.
TEST_F(MediaLibraryTestFixture, RockGenreAlbumsPropertyContainsRockAlbums)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    ASSERT_NE(rock, nullptr);
    ASSERT_NE(rock->getAlbumsProperty(), nullptr);
    EXPECT_GT(rock->getAlbumsProperty()->getCountProperty(), 0);
}

// plans/plan_media.md MEDIA-98: Genre::Dispose() flips IsDisposed; a non-owning view, so the
// underlying Albums/Songs collections (owned by MediaLibrary) remain independently valid.
TEST_F(MediaLibraryTestFixture, GenreDisposeFlipsIsDisposed)
{
    Genre* rock = FindGenre(library->getGenresProperty(), "Rock");
    ASSERT_NE(rock, nullptr);
    ASSERT_FALSE(rock->getIsDisposedProperty());

    rock->Dispose();

    EXPECT_TRUE(rock->getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-99: GenreCollection's own Dispose()/IsDisposed, distinct from any one
// contained Genre's Dispose()/IsDisposed.
TEST_F(MediaLibraryTestFixture, GenreCollectionDisposeFlipsIsDisposed)
{
    auto* genres = library->getGenresProperty();
    ASSERT_FALSE(genres->getIsDisposedProperty());

    genres->Dispose();

    EXPECT_TRUE(genres->getIsDisposedProperty());
}
