// SPDX-License-Identifier: MS-PL

// CNA-DEMOS-ISSUE-098: XNA's media library types refuse every ordinary member with
// ObjectDisposedException once disposed (each calls ThrowIfDisposed first); IsDisposed stays
// readable, Dispose stays repeatable, and disposing one object never disposes another.

#include <filesystem>
#include <random>
#include <functional>
#include <string>
#include <vector>

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Picture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Playlist.hpp"
#include "Microsoft/Xna/Framework/Media/PlaylistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

namespace
{
    constexpr const char* kFixture =
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";

    std::size_t CountRefusals(const std::vector<std::function<void()>>& operations)
    {
        std::size_t refused = 0;
        for (const auto& operation : operations)
        {
            try
            {
                operation();
            }
            catch (const System::ObjectDisposedException&)
            {
                ++refused;
            }
            catch (const std::exception&)
            {
                // Any other outcome is a failure to refuse.
            }
        }
        return refused;
    }

    template <typename TCollection>
    void ExpectDisposedCollectionRefusesAccess(TCollection& collection)
    {
        collection.Dispose();
        collection.Dispose();
        EXPECT_TRUE(collection.getIsDisposedProperty());
        EXPECT_THROW((void)collection.getCountProperty(), System::ObjectDisposedException);
        EXPECT_THROW((void)collection[0], System::ObjectDisposedException);
        EXPECT_THROW((void)collection[-1], System::ObjectDisposedException);
        EXPECT_THROW((void)collection.begin(), System::ObjectDisposedException);
        EXPECT_THROW((void)collection.end(), System::ObjectDisposedException);
        const TCollection& view = collection;
        EXPECT_THROW((void)view.begin(), System::ObjectDisposedException);
        EXPECT_THROW((void)view.end(), System::ObjectDisposedException);
    }
}

TEST(MediaDisposalTest, ADisposedSongRefusesEveryOrdinaryMember)
{
    Song song(kFixture, "Sunrise");
    Song renamed(kFixture, "Renamed");
    Song namesake(kFixture, "Sunrise");
    song.Dispose();
    song.Dispose();
    ASSERT_TRUE(song.getIsDisposedProperty());
    ASSERT_FALSE(renamed.getIsDisposedProperty());

    const std::size_t refused = CountRefusals({
        [&] { (void)song.getNameProperty(); },
        [&] { (void)song.getAlbumProperty(); },
        [&] { (void)song.getArtistProperty(); },
        [&] { (void)song.getGenreProperty(); },
        [&] { (void)song.getDurationProperty(); },
        [&] { (void)song.getIsProtectedProperty(); },
        [&] { (void)song.getIsRatedProperty(); },
        [&] { (void)song.getPlayCountProperty(); },
        [&] { (void)song.getRatingProperty(); },
        [&] { (void)song.getTrackNumberProperty(); },
        [&] { (void)song.Equals(&renamed); },
        [&] { (void)song.Equals(static_cast<const System::Object*>(&renamed)); },
        [&] { (void)(song == renamed); },
        [&] { (void)(song != namesake); },
        [&] { (void)song.GetHashCode(); },
        [&] { (void)song.ToString(); },
    });
    EXPECT_EQ(refused, 16u);
    EXPECT_EQ(renamed.getNameProperty(), "Renamed");
}

TEST(MediaDisposalTest, EqualityFollowsObjectEqualsAroundADisposedSong)
{
    Song song(kFixture, "Sunrise");
    Song peer(kFixture, "Sunrise");
    ASSERT_TRUE(peer.Equals(&song));
    song.Dispose();

    // The same object is equal without being asked; a disposed argument released its handle.
    EXPECT_TRUE(song == song);
    EXPECT_FALSE(song != song);
    EXPECT_FALSE(peer.Equals(&song));
    EXPECT_FALSE(peer == song);
    EXPECT_TRUE(peer.Equals(&peer));
    EXPECT_EQ(song.getHandle(), peer.getHandle());
}

TEST_F(MediaLibraryTestFixture, DisposedMusicEntitiesRefuseTheirMembers)
{
    AlbumCollection* albums = library->getAlbumsProperty();
    ASSERT_GE(albums->getCountProperty(), 2);
    Album* album = nullptr;
    for (Album* candidate : *albums)
    {
        if (!candidate->getHasArtProperty()) album = candidate;
    }
    ASSERT_NE(album, nullptr);
    Album* otherAlbum = (*albums)[0] == album ? (*albums)[1] : (*albums)[0];
    Artist* artist = (*library->getArtistsProperty())[0];
    Artist* otherArtist = (*library->getArtistsProperty())[1];
    Genre* genre = (*library->getGenresProperty())[0];
    Genre* otherGenre = (*library->getGenresProperty())[1];
    Playlist* playlist = (*library->getPlaylistsProperty())[0];
    Playlist* otherPlaylist = (*library->getPlaylistsProperty())[1];

    album->Dispose();
    album->Dispose();
    artist->Dispose();
    genre->Dispose();
    playlist->Dispose();
    playlist->Dispose();

    // The art methods refuse the disposed album before its missing-art InvalidOperationException.
    EXPECT_EQ(CountRefusals({
        [&] { (void)album->getArtistProperty(); },
        [&] { (void)album->getDurationProperty(); },
        [&] { (void)album->getGenreProperty(); },
        [&] { (void)album->getHasArtProperty(); },
        [&] { (void)album->getNameProperty(); },
        [&] { (void)album->getSongsProperty(); },
        [&] { delete album->GetAlbumArt(); },
        [&] { delete album->GetThumbnail(); },
        [&] { (void)album->Equals(otherAlbum); },
        [&] { (void)album->Equals(static_cast<const System::Object*>(otherAlbum)); },
        [&] { (void)(*album == *otherAlbum); },
        [&] { (void)album->GetHashCode(); },
        [&] { (void)album->ToString(); },
    }), 13u);
    EXPECT_EQ(CountRefusals({
        [&] { (void)artist->getNameProperty(); },
        [&] { (void)artist->getSongsProperty(); },
        [&] { (void)artist->getAlbumsProperty(); },
        [&] { (void)artist->Equals(otherArtist); },
        [&] { (void)artist->Equals(static_cast<const System::Object*>(otherArtist)); },
        [&] { (void)(*artist == *otherArtist); },
        [&] { (void)artist->GetHashCode(); },
        [&] { (void)artist->ToString(); },
    }), 8u);
    EXPECT_EQ(CountRefusals({
        [&] { (void)genre->getNameProperty(); },
        [&] { (void)genre->getSongsProperty(); },
        [&] { (void)genre->getAlbumsProperty(); },
        [&] { (void)genre->Equals(otherGenre); },
        [&] { (void)genre->Equals(static_cast<const System::Object*>(otherGenre)); },
        [&] { (void)(*genre != *otherGenre); },
        [&] { (void)genre->GetHashCode(); },
        [&] { (void)genre->ToString(); },
    }), 8u);
    EXPECT_EQ(CountRefusals({
        [&] { (void)playlist->getNameProperty(); },
        [&] { (void)playlist->getSongsProperty(); },
        [&] { (void)playlist->getDurationProperty(); },
        [&] { (void)playlist->Equals(otherPlaylist); },
        [&] { (void)playlist->Equals(static_cast<const System::Object*>(otherPlaylist)); },
        [&] { (void)(*playlist == *otherPlaylist); },
        [&] { (void)playlist->GetHashCode(); },
        [&] { (void)playlist->ToString(); },
    }), 8u);

    // Siblings stay live, and a disposed argument equals nothing but itself.
    EXPECT_FALSE(otherAlbum->getIsDisposedProperty());
    EXPECT_FALSE(otherArtist->Equals(artist));
    EXPECT_TRUE(*artist == *artist);
    EXPECT_FALSE(otherGenre->getNameProperty().empty());
    EXPECT_FALSE(otherPlaylist->Equals(playlist));
}

TEST_F(MediaLibraryTestFixture, AnAlbumStillComparesAndHashesAfterItsArtistIsDisposed)
{
    Album* album = (*library->getAlbumsProperty())[0];
    ASSERT_NE(album->getArtistProperty(), nullptr);
    const int hash = album->GetHashCode();
    album->getArtistProperty()->Dispose();

    EXPECT_TRUE(album->Equals(album));
    EXPECT_EQ(album->GetHashCode(), hash);
}

TEST_F(MediaLibraryTestFixture, DisposedPicturesAndPictureAlbumsRefuseTheirMembers)
{
    PictureCollection* pictures = library->getPicturesProperty();
    ASSERT_GE(pictures->getCountProperty(), 2);
    Picture* picture = (*pictures)[0];
    Picture* otherPicture = (*pictures)[1];
    PictureAlbum* pictureAlbum = picture->getAlbumProperty();
    ASSERT_NE(pictureAlbum, nullptr);
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    ASSERT_NE(root, pictureAlbum);

    picture->Dispose();
    picture->Dispose();
    pictureAlbum->Dispose();
    pictureAlbum->Dispose();

    EXPECT_EQ(CountRefusals({
        [&] { (void)picture->getNameProperty(); },
        [&] { (void)picture->getAlbumProperty(); },
        [&] { (void)picture->getWidthProperty(); },
        [&] { (void)picture->getHeightProperty(); },
        [&] { (void)picture->getDateProperty(); },
        [&] { delete picture->GetImage(); },
        [&] { delete picture->GetThumbnail(); },
        [&] { (void)picture->Equals(otherPicture); },
        [&] { (void)picture->Equals(static_cast<const System::Object*>(otherPicture)); },
        [&] { (void)(*picture == *otherPicture); },
        [&] { (void)picture->GetHashCode(); },
        [&] { (void)picture->ToString(); },
    }), 12u);
    EXPECT_EQ(CountRefusals({
        [&] { (void)pictureAlbum->getNameProperty(); },
        [&] { (void)pictureAlbum->getAlbumsProperty(); },
        [&] { (void)pictureAlbum->getPicturesProperty(); },
        [&] { (void)pictureAlbum->getParentProperty(); },
        [&] { (void)pictureAlbum->Equals(root); },
        [&] { (void)pictureAlbum->Equals(static_cast<const System::Object*>(root)); },
        [&] { (void)(*pictureAlbum == *root); },
        [&] { (void)pictureAlbum->GetHashCode(); },
        [&] { (void)pictureAlbum->ToString(); },
    }), 9u);
    EXPECT_FALSE(otherPicture->getIsDisposedProperty());
    EXPECT_FALSE(root->getIsDisposedProperty());
}

TEST_F(MediaLibraryTestFixture, DisposedCollectionsRefuseCountIndexerAndEnumeration)
{
    Song* song = (*library->getSongsProperty())[0];
    Album* album = (*library->getAlbumsProperty())[0];

    ExpectDisposedCollectionRefusesAccess(*library->getSongsProperty());
    ExpectDisposedCollectionRefusesAccess(*library->getAlbumsProperty());
    ExpectDisposedCollectionRefusesAccess(*library->getArtistsProperty());
    ExpectDisposedCollectionRefusesAccess(*library->getGenresProperty());
    ExpectDisposedCollectionRefusesAccess(*library->getPlaylistsProperty());
    ExpectDisposedCollectionRefusesAccess(*library->getPicturesProperty());
    ExpectDisposedCollectionRefusesAccess(*library->getRootPictureAlbumProperty()->getAlbumsProperty());

    // The collections never owned their entries.
    EXPECT_FALSE(song->getNameProperty().empty());
    EXPECT_FALSE(album->getNameProperty().empty());
}

TEST_F(MediaLibraryTestFixture, ADisposedLibraryRefusesItsMembersButNotItsViews)
{
    // An unguarded SavePicture writes, so this library saves below a private temporary root
    // rather than the checked-in fixture pictures.
    std::random_device entropy;
    const std::filesystem::path pictureRoot =
        std::filesystem::temp_directory_path() /
        ("cna-media-disposal-" + std::to_string(entropy()) + "-" + std::to_string(entropy()));
    struct RemoveOnExit
    {
        std::filesystem::path path;
        ~RemoveOnExit()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } removeOnExit{pictureRoot};
    std::filesystem::create_directories(pictureRoot);
    CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride(
        CNA::Internal::PathToUtf8(pictureRoot));
    library = std::make_unique<MediaLibrary>();
    CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("tests/assets/media/pictures");

    SongCollection* songs = library->getSongsProperty();
    PictureAlbum* root = library->getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);
    library->Dispose();
    library->Dispose();
    ASSERT_TRUE(library->getIsDisposedProperty());

    const std::vector<std::uint8_t> oneByte{0xFF};
    System::IO::MemoryStream oneByteStream(oneByte.data(), 1);
    EXPECT_EQ(CountRefusals({
        [&] { (void)library->getAlbumsProperty(); },
        [&] { (void)library->getArtistsProperty(); },
        [&] { (void)library->getGenresProperty(); },
        [&] { (void)library->getMediaSourceProperty(); },
        [&] { (void)library->getPicturesProperty(); },
        [&] { (void)library->getPlaylistsProperty(); },
        [&] { (void)library->getRootPictureAlbumProperty(); },
        [&] { (void)library->getSavedPicturesProperty(); },
        [&] { (void)library->getSongsProperty(); },
        [&] { (void)library->GetPictureFromToken("token"); },
        [&] { (void)library->SavePicture("disposed", oneByte); },
        [&] { (void)library->SavePicture("disposed", &oneByteStream); },
        [&] { (void)library->SavePicture("disposed", static_cast<System::IO::Stream*>(nullptr)); },
    }), 13u);

    // Disposing the library does not cascade into views taken before it.
    EXPECT_FALSE(songs->getIsDisposedProperty());
    EXPECT_GT(songs->getCountProperty(), 0);
    EXPECT_FALSE((*songs)[0]->getNameProperty().empty());
    EXPECT_FALSE(root->getNameProperty().empty());
    EXPECT_FALSE(std::filesystem::exists(pictureRoot / "Saved Pictures"));
}
