// SPDX-License-Identifier: MS-PL

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Playlist.hpp"
#include "Microsoft/Xna/Framework/Media/PlaylistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"

using Microsoft::Xna::Framework::Media::Playlist;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    Playlist* FindPlaylist(Microsoft::Xna::Framework::Media::PlaylistCollection* playlists,
                            const std::string& name)
    {
        for (Playlist* p : *playlists)
        {
            if (p->getNameProperty() == name) return p;
        }
        return nullptr;
    }
}

// plan_media.md MEDIA-68: real implementation backed by PlaylistParser results.
TEST_F(MediaLibraryTestFixture, PlaylistsContainsBothFixturePlaylists)
{
    EXPECT_EQ(library->getPlaylistsProperty()->getCountProperty(), 2);
    EXPECT_NE(FindPlaylist(library->getPlaylistsProperty(), "Favorites"), nullptr);
    EXPECT_NE(FindPlaylist(library->getPlaylistsProperty(), "International"), nullptr);
}

TEST_F(MediaLibraryTestFixture, FavoritesResolvesToThreeRealSongsSkippingTheMissingOne)
{
    Playlist* favorites = FindPlaylist(library->getPlaylistsProperty(), "Favorites");
    ASSERT_NE(favorites, nullptr);
    EXPECT_EQ(favorites->getSongsProperty()->getCountProperty(), 3);
}

TEST_F(MediaLibraryTestFixture, InternationalResolvesTheNonAsciiEntry)
{
    Playlist* international = FindPlaylist(library->getPlaylistsProperty(), "International");
    ASSERT_NE(international, nullptr);
    EXPECT_EQ(international->getSongsProperty()->getCountProperty(), 2);
}

// plan_media.md MEDIA-68: operator==/!= call Equals() directly (see Playlist.cpp's own comment on
// why FNA's null-check shape doesn't apply to C++ reference parameters).
TEST_F(MediaLibraryTestFixture, PlaylistEqualitySetForEqualAndUnequalPlaylists)
{
    Playlist* favorites = FindPlaylist(library->getPlaylistsProperty(), "Favorites");
    Playlist* international = FindPlaylist(library->getPlaylistsProperty(), "International");
    ASSERT_NE(favorites, nullptr);
    ASSERT_NE(international, nullptr);

    EXPECT_TRUE(favorites->Equals(favorites));
    EXPECT_FALSE(favorites->Equals(international));
    EXPECT_TRUE(*favorites == *favorites);
    EXPECT_TRUE(*favorites != *international);
    EXPECT_EQ(favorites->GetHashCode(), favorites->GetHashCode());
}

TEST_F(MediaLibraryTestFixture, PlaylistGetTypeNameIsFullyQualified)
{
    Playlist* favorites = FindPlaylist(library->getPlaylistsProperty(), "Favorites");
    ASSERT_NE(favorites, nullptr);
    EXPECT_EQ(favorites->GetTypeName(), "Microsoft.Xna.Framework.Media.Playlist");
}

// plan_media.md MEDIA-108: Equals(nullptr) -- the "one-null" case. There is no C++ "both-null"
// analog to test: operator==/!= take references (see Playlist.cpp's own comment on why), and a
// reference can never be null, so the only null-involving case reachable at all is via the
// pointer-taking Equals(const Playlist*) overload with a real `this` and a null `other`.
TEST_F(MediaLibraryTestFixture, PlaylistEqualsReturnsFalseForNullOther)
{
    Playlist* favorites = FindPlaylist(library->getPlaylistsProperty(), "Favorites");
    ASSERT_NE(favorites, nullptr);
    EXPECT_FALSE(favorites->Equals(nullptr));
}

// plan_media.md MEDIA-108: Duration -- sum of song durations, all TimeSpan.Zero for
// library-scanned songs until actually played (§4 D9), so a freshly-scanned Playlist's Duration
// is zero too.
TEST_F(MediaLibraryTestFixture, PlaylistDurationIsZeroForUnplayedLibraryScannedSongs)
{
    Playlist* favorites = FindPlaylist(library->getPlaylistsProperty(), "Favorites");
    ASSERT_NE(favorites, nullptr);
    EXPECT_EQ(favorites->getDurationProperty(), System::TimeSpan::Zero);
}

// plan_media.md MEDIA-108: IsDisposed, not exercised anywhere else in this file.
TEST_F(MediaLibraryTestFixture, PlaylistDisposeFlipsIsDisposed)
{
    Playlist* favorites = FindPlaylist(library->getPlaylistsProperty(), "Favorites");
    ASSERT_NE(favorites, nullptr);
    ASSERT_FALSE(favorites->getIsDisposedProperty());

    favorites->Dispose();

    EXPECT_TRUE(favorites->getIsDisposedProperty());
}

// plan_media.md MEDIA-109: PlaylistCollection's own indexer (in-bounds) and Dispose()/IsDisposed --
// only Count was previously checked.
TEST_F(MediaLibraryTestFixture, PlaylistCollectionIndexerReturnsPlaylistsInBounds)
{
    auto* playlists = library->getPlaylistsProperty();
    ASSERT_EQ(playlists->getCountProperty(), 2);
    for (SharpRuntime::intcs i = 0; i < playlists->getCountProperty(); ++i)
    {
        EXPECT_NE((*playlists)[i], nullptr);
    }
}

TEST_F(MediaLibraryTestFixture, PlaylistCollectionDisposeFlipsIsDisposed)
{
    auto* playlists = library->getPlaylistsProperty();
    ASSERT_FALSE(playlists->getIsDisposedProperty());

    playlists->Dispose();

    EXPECT_TRUE(playlists->getIsDisposedProperty());
}
