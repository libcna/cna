// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>

#include "MediaLibraryTestFixture.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/AlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/ArtistCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/GenreCollection.hpp"
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"
#include "Microsoft/Xna/Framework/Media/Picture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/FileStream.hpp"

using namespace Microsoft::Xna::Framework::Media;
using Microsoft::Xna::Framework::Media::Test::MediaLibraryTestFixture;

TEST_F(MediaLibraryTestFixture, DefaultConstructorPopulatesEveryCollection)
{
    EXPECT_FALSE(library->getIsDisposedProperty());
    ASSERT_NE(library->getMediaSourceProperty(), nullptr);
    EXPECT_EQ(library->getMediaSourceProperty()->getMediaSourceTypeProperty(), MediaSourceType::LocalDevice);
    EXPECT_GT(library->getSongsProperty()->getCountProperty(), 0);
    EXPECT_GT(library->getAlbumsProperty()->getCountProperty(), 0);
    EXPECT_GT(library->getArtistsProperty()->getCountProperty(), 0);
    EXPECT_GT(library->getGenresProperty()->getCountProperty(), 0);
    EXPECT_GT(library->getPicturesProperty()->getCountProperty(), 0);
    EXPECT_GT(library->getPlaylistsProperty()->getCountProperty(), 0);
    ASSERT_NE(library->getRootPictureAlbumProperty(), nullptr);
    ASSERT_NE(library->getSavedPicturesProperty(), nullptr);
}

TEST_F(MediaLibraryTestFixture, MediaSourceConstructorAcceptsTheRealLocalDeviceSource)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    ASSERT_EQ(sources.size(), 1u);
    MediaLibrary fromSource(sources[0]);
    EXPECT_EQ(fromSource.getMediaSourceProperty()->getMediaSourceTypeProperty(), MediaSourceType::LocalDevice);
    EXPECT_GT(fromSource.getSongsProperty()->getCountProperty(), 0);
    delete sources[0];
}

TEST_F(MediaLibraryTestFixture, ConstructorThrowsArgumentNullExceptionForNullSource)
{
    EXPECT_THROW({ MediaLibrary lib(nullptr); }, System::ArgumentNullException);
}

TEST_F(MediaLibraryTestFixture, GetPictureFromTokenRoundTripsViaRealToken)
{
    Picture* first = (*library->getPicturesProperty())[0];
    ASSERT_NE(first, nullptr);
    Picture* found = library->GetPictureFromToken(first->getTokenEXT());
    EXPECT_EQ(found, first);
}

TEST_F(MediaLibraryTestFixture, GetPictureFromTokenReturnsNullForUnknownToken)
{
    EXPECT_EQ(library->GetPictureFromToken("not-a-real-token"), nullptr);
}

TEST_F(MediaLibraryTestFixture, DisposeMarksLibraryDisposed)
{
    library->Dispose();
    EXPECT_TRUE(library->getIsDisposedProperty());
}

TEST_F(MediaLibraryTestFixture, GetTypeNameIsFullyQualified)
{
    EXPECT_EQ(library->GetTypeName(), "Microsoft.Xna.Framework.Media.MediaLibrary");
}

// plan_media.md MEDIA-69: full cross-class object-graph integration audit -- walks every
// relationship round-trip to confirm the object graph is internally consistent, not just
// individually populated.
TEST_F(MediaLibraryTestFixture, ObjectGraphIsInternallyConsistent)
{
    // Every Genre's Albums/Songs actually belong to that Genre, and vice versa.
    for (Genre* genre : *library->getGenresProperty())
    {
        for (Album* album : *genre->getAlbumsProperty())
        {
            ASSERT_NE(album->getGenreProperty(), nullptr);
            EXPECT_TRUE(album->getGenreProperty()->Equals(genre));
        }
        for (Song* song : *genre->getSongsProperty())
        {
            EXPECT_NE(song, nullptr);
        }
    }

    // Every Artist's Albums actually point back to that Artist.
    for (Artist* artist : *library->getArtistsProperty())
    {
        for (Album* album : *artist->getAlbumsProperty())
        {
            ASSERT_NE(album->getArtistProperty(), nullptr);
            EXPECT_TRUE(album->getArtistProperty()->Equals(artist));
        }
    }

    // Every Album appears in its Artist's AlbumCollection and (if it has one) its Genre's.
    for (Album* album : *library->getAlbumsProperty())
    {
        ASSERT_NE(album->getArtistProperty(), nullptr);
        bool foundInArtist = false;
        for (Album* a : *album->getArtistProperty()->getAlbumsProperty())
        {
            if (a == album) foundInArtist = true;
        }
        EXPECT_TRUE(foundInArtist);

        if (album->getGenreProperty() != nullptr)
        {
            bool foundInGenre = false;
            for (Album* a : *album->getGenreProperty()->getAlbumsProperty())
            {
                if (a == album) foundInGenre = true;
            }
            EXPECT_TRUE(foundInGenre);
        }
    }

    // Every Picture's Album.Pictures actually contains that Picture.
    for (Picture* picture : *library->getPicturesProperty())
    {
        ASSERT_NE(picture->getAlbumProperty(), nullptr);
        bool found = false;
        for (Picture* p : *picture->getAlbumProperty()->getPicturesProperty())
        {
            if (p == picture) found = true;
        }
        EXPECT_TRUE(found);
    }

    // Every PictureAlbum's children report it as their Parent, recursively from the root.
    std::function<void(PictureAlbum*)> walk = [&](PictureAlbum* node)
    {
        for (PictureAlbum* child : *node->getAlbumsProperty())
        {
            EXPECT_EQ(child->getParentProperty(), node);
            walk(child);
        }
    };
    walk(library->getRootPictureAlbumProperty());

    // Every Song in the top-level SongCollection is reachable, and no Genre/Artist/Album song list
    // contains a Song absent from the top-level collection.
    std::vector<Song*> allSongs(library->getSongsProperty()->begin(), library->getSongsProperty()->end());
    auto isKnownSong = [&](Song* s) {
        return std::find(allSongs.begin(), allSongs.end(), s) != allSongs.end();
    };
    for (Artist* artist : *library->getArtistsProperty())
    {
        for (Song* s : *artist->getSongsProperty())
        {
            EXPECT_TRUE(isKnownSong(s));
        }
    }
}

namespace
{
    // plan_media.md MEDIA-59/MEDIA-62: SavePicture writes real files -- uses a scratch,
    // writable copy of the Pictures fixture rather than the checked-in tree, so the test doesn't
    // leave an untracked "Saved Pictures" directory inside the repo's real fixture tree.
    class MediaLibrarySavePictureTest : public ::testing::Test
    {
    protected:
        std::string scratchMusicRoot = "tests/assets/media/.save_picture_test_music";
        std::string scratchPictureRoot = "tests/assets/media/.save_picture_test_pictures";

        void SetUp() override
        {
            std::error_code ec;
            std::filesystem::remove_all(scratchMusicRoot, ec);
            std::filesystem::remove_all(scratchPictureRoot, ec);
            std::filesystem::create_directories(scratchMusicRoot, ec);
            std::filesystem::create_directories(scratchPictureRoot, ec);

            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride(scratchMusicRoot);
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride(scratchPictureRoot);
        }

        void TearDown() override
        {
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("");
            std::error_code ec;
            std::filesystem::remove_all(scratchMusicRoot, ec);
            std::filesystem::remove_all(scratchPictureRoot, ec);
        }
    };
}

TEST_F(MediaLibrarySavePictureTest, SavePictureFromBufferCreatesARealReadablePicture)
{
    std::ifstream src("tests/assets/media/pictures/Family/portrait.png", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(bytes.empty());

    MediaLibrary library;
    EXPECT_EQ(library.getSavedPicturesProperty()->getCountProperty(), 0);

    Picture* saved = library.SavePicture("my_saved_picture", bytes);
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->getWidthProperty(), 100);
    EXPECT_EQ(saved->getHeightProperty(), 80);

    EXPECT_EQ(library.getSavedPicturesProperty()->getCountProperty(), 1);
    EXPECT_EQ((*library.getSavedPicturesProperty())[0], saved);

    bool foundInTopLevel = false;
    for (Picture* p : *library.getPicturesProperty())
    {
        if (p == saved) foundInTopLevel = true;
    }
    EXPECT_TRUE(foundInTopLevel);

    EXPECT_EQ(library.GetPictureFromToken(saved->getTokenEXT()), saved);
}

TEST_F(MediaLibrarySavePictureTest, SavePictureFromStreamProducesTheSameResultAsFromBuffer)
{
    auto* stream = new System::IO::FileStream("tests/assets/media/pictures/Vacation/beach.jpg");
    MediaLibrary library;
    Picture* saved = library.SavePicture("from_stream", stream);
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->getWidthProperty(), 64);
    EXPECT_EQ(saved->getHeightProperty(), 48);
}

TEST_F(MediaLibrarySavePictureTest, SavePictureFromStreamThrowsArgumentNullExceptionForNullSource)
{
    MediaLibrary library;
    EXPECT_THROW(library.SavePicture("name", static_cast<System::IO::Stream*>(nullptr)),
                 System::ArgumentNullException);
}
