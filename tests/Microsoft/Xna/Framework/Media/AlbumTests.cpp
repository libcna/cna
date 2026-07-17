// SPDX-License-Identifier: MS-PL

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/Stream.hpp"

using Microsoft::Xna::Framework::Media::Album;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Album* FindAlbum(Microsoft::Xna::Framework::Media::AlbumCollection* albums, const std::string& name)
    {
        for (Album* a : *albums)
        {
            if (a->getNameProperty() == name) return a;
        }
        return nullptr;
    }
}

// plan_media.md MEDIA-65: real implementation, incl. HasArt reflecting the real cover.jpg fixture.
TEST_F(MediaLibraryTestFixture, AlbumsContainsAllFourAlbums)
{
    EXPECT_EQ(library->getAlbumsProperty()->getCountProperty(), 4); // Alpha, Beta, Gamma, Delta
}

TEST_F(MediaLibraryTestFixture, AlbumAlphaHasArtViaRealCoverFile)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    EXPECT_TRUE(alpha->getHasArtProperty());

    System::IO::Stream* art = alpha->GetAlbumArt();
    ASSERT_NE(art, nullptr);
    EXPECT_GT(art->getLengthProperty(), 0);
    delete art;
}

TEST_F(MediaLibraryTestFixture, AlbumBetaHasNoArtAndThrowsOnAccess)
{
    Album* beta = FindAlbum(library->getAlbumsProperty(), "Album Beta");
    ASSERT_NE(beta, nullptr);
    EXPECT_FALSE(beta->getHasArtProperty());
    EXPECT_THROW(beta->GetAlbumArt(), System::InvalidOperationException);
}

TEST_F(MediaLibraryTestFixture, AlbumArtistAndGenreAreCorrect)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(alpha->getArtistProperty(), nullptr);
    EXPECT_EQ(alpha->getArtistProperty()->getNameProperty(), "Artist One");
    ASSERT_NE(alpha->getGenreProperty(), nullptr);
    EXPECT_EQ(alpha->getGenreProperty()->getNameProperty(), "Rock");
}

TEST_F(MediaLibraryTestFixture, AlbumSongsMatchesExpectedCount)
{
    Album* delta = FindAlbum(library->getAlbumsProperty(), "Album Delta");
    ASSERT_NE(delta, nullptr);
    EXPECT_EQ(delta->getSongsProperty()->getCountProperty(), 2); // Daybreak + Étoile
}

// plan_media.md MEDIA-65: equality is by (Name, Artist) since album names can collide across
// artists -- there is no actual name collision in this fixture, so this test constructs the
// scenario indirectly by confirming two DIFFERENT albums (different name, different artist) are
// correctly unequal, and an album equals itself.
TEST_F(MediaLibraryTestFixture, AlbumEqualitySetForEqualAndUnequalAlbums)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    Album* gamma = FindAlbum(library->getAlbumsProperty(), "Album Gamma");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(gamma, nullptr);

    EXPECT_TRUE(alpha->Equals(alpha));
    EXPECT_FALSE(alpha->Equals(gamma));
    EXPECT_TRUE(*alpha == *alpha);
    EXPECT_TRUE(*alpha != *gamma);
}

TEST_F(MediaLibraryTestFixture, AlbumGetTypeNameIsFullyQualified)
{
    Album* alpha = FindAlbum(library->getAlbumsProperty(), "Album Alpha");
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->GetTypeName(), "Microsoft.Xna.Framework.Media.Album");
}
