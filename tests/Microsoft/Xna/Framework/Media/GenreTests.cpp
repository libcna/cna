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

// plan_media.md MEDIA-63: real implementation backed by MediaLibraryIndex genre grouping.
TEST_F(MediaLibraryTestFixture, GenresContainsRockAndElectronic)
{
    EXPECT_EQ(library->getGenresProperty()->getCountProperty(), 2);
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

// plan_media.md MEDIA-57 (GenreCollectionTests folded in): indexer/count/dispose/iteration.
TEST_F(MediaLibraryTestFixture, GenreCollectionIndexerAndIterationWork)
{
    auto* genres = library->getGenresProperty();
    ASSERT_EQ(genres->getCountProperty(), 2);
    EXPECT_NE((*genres)[0], nullptr);
    EXPECT_NE((*genres)[1], nullptr);
    EXPECT_THROW((void)(*genres)[2], System::ArgumentOutOfRangeException);

    int count = 0;
    for (Genre* g : *genres) { EXPECT_NE(g, nullptr); ++count; }
    EXPECT_EQ(count, 2);
}
