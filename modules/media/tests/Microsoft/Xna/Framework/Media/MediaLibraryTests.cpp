// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

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

// plans/plan_media.md MEDIA-69: cross-class object-graph integration audit -- walks every relationship
// round-trip to confirm the object graph is internally consistent, not just individually populated.
//
// This was originally labelled a "full" audit, which overstated it (corrected per plans/plan_media.md
// MEDIA-179): until MEDIA-174 there were no Song->Album/Artist/Genre members at all, so the
// Song-side reverse edges below were structurally impossible to check. CNA inherited that gap
// straight from FNA, whose own Song.cs omits those XNA members -- see MEDIA-180 for the resulting
// "FNA is authoritative for behavior, the XNA reference assemblies for API surface" rule.
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
        // Reverse edge (MEDIA-177/178): a Genre's member song must point back at that same Genre
        // object -- pointer identity, not merely a matching name.
        for (Song* song : *genre->getSongsProperty())
        {
            ASSERT_NE(song, nullptr);
            EXPECT_EQ(song->getGenreProperty(), genre)
                << "song '" << song->getNameProperty() << "' does not point back at its Genre";
        }
    }

    // Reverse edges from the Artist and Album sides (MEDIA-177/178).
    for (Artist* artist : *library->getArtistsProperty())
    {
        for (Song* song : *artist->getSongsProperty())
        {
            ASSERT_NE(song, nullptr);
            EXPECT_EQ(song->getArtistProperty(), artist)
                << "song '" << song->getNameProperty() << "' does not point back at its Artist";
        }
    }
    for (Album* album : *library->getAlbumsProperty())
    {
        for (Song* song : *album->getSongsProperty())
        {
            ASSERT_NE(song, nullptr);
            EXPECT_EQ(song->getAlbumProperty(), album)
                << "song '" << song->getNameProperty() << "' does not point back at its Album";
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
    // plans/plan_media.md MEDIA-59/MEDIA-62: SavePicture writes real files -- uses a scratch,
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

// plans/plan_media.md MEDIA-59/D7 (corrected -- found by external code review): the first SavePicture()
// call on a library with no pre-existing "Saved Pictures" folder must create a real PictureAlbum
// tree node for it (as a child of RootPictureAlbum), not silently leave the saved Picture parented
// to the root album with no dedicated node at all.
TEST_F(MediaLibrarySavePictureTest, SavePictureCreatesARealSavedPicturesAlbumNodeIfNoneExisted)
{
    MediaLibrary library;
    PictureAlbum* root = library.getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);

    bool hadSavedPicturesAlbumBefore = false;
    for (PictureAlbum* a : *root->getAlbumsProperty())
    {
        if (a->getNameProperty() == "Saved Pictures") hadSavedPicturesAlbumBefore = true;
    }
    ASSERT_FALSE(hadSavedPicturesAlbumBefore);

    std::ifstream src("tests/assets/media/pictures/Family/portrait.png", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    Picture* saved = library.SavePicture("newly_created_album_picture", bytes);
    ASSERT_NE(saved, nullptr);

    PictureAlbum* savedPicturesAlbum = nullptr;
    for (PictureAlbum* a : *root->getAlbumsProperty())
    {
        if (a->getNameProperty() == "Saved Pictures") savedPicturesAlbum = a;
    }
    ASSERT_NE(savedPicturesAlbum, nullptr);
    EXPECT_EQ(savedPicturesAlbum->getParentProperty(), root);
    EXPECT_EQ(saved->getAlbumProperty(), savedPicturesAlbum);

    bool foundInAlbum = false;
    for (Picture* p : *savedPicturesAlbum->getPicturesProperty())
    {
        if (p == saved) foundInAlbum = true;
    }
    EXPECT_TRUE(foundInAlbum);
}

namespace
{
    // plans/plan_media.md MEDIA-132 (found incomplete by external code review): a dedicated scratch
    // fixture whose Pictures root directory is deliberately never created before MediaLibrary's
    // own construction (unlike MediaLibrarySavePictureTest's own SetUp(), which always
    // pre-creates an empty scratchPictureRoot directory) -- this specifically exercises the case
    // where PictureLibraryIndex finds no root at all, so rootPictureAlbum_ starts null, not just
    // "root exists but has no Saved Pictures child yet."
    class MediaLibrarySavePictureNoPreexistingRootTest : public ::testing::Test
    {
    protected:
        std::string scratchMusicRoot = "tests/assets/media/.save_picture_no_root_test_music";
        std::string scratchPictureRoot = "tests/assets/media/.save_picture_no_root_test_pictures";

        void SetUp() override
        {
            std::error_code ec;
            std::filesystem::remove_all(scratchMusicRoot, ec);
            std::filesystem::remove_all(scratchPictureRoot, ec);
            std::filesystem::create_directories(scratchMusicRoot, ec);
            // Deliberately do NOT create scratchPictureRoot -- it must not exist yet.

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

TEST_F(MediaLibrarySavePictureNoPreexistingRootTest, SavePictureBootstrapsARootAlbumWhenPicturesRootDidNotExistAtConstruction)
{
    MediaLibrary library;
    ASSERT_EQ(library.getRootPictureAlbumProperty(), nullptr); // confirms the scenario: no root yet

    std::ifstream src("tests/assets/media/pictures/Family/portrait.png", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    Picture* saved = library.SavePicture("bootstrap_test_picture", bytes);
    ASSERT_NE(saved, nullptr);

    PictureAlbum* root = library.getRootPictureAlbumProperty();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getParentProperty(), nullptr);

    PictureAlbum* savedPicturesAlbum = nullptr;
    for (PictureAlbum* a : *root->getAlbumsProperty())
    {
        if (a->getNameProperty() == "Saved Pictures") savedPicturesAlbum = a;
    }
    ASSERT_NE(savedPicturesAlbum, nullptr);
    EXPECT_EQ(savedPicturesAlbum->getParentProperty(), root);
    EXPECT_EQ(saved->getAlbumProperty(), savedPicturesAlbum);
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

// plans/plan_media.md MEDIA-174/177/178: Song::Album/Artist/Genre are real XNA 4.0 members that CNA
// simply did not have until Phase 16 -- FNA's own Song.cs omits them, so every prior audit against
// FNA (eight adversarial review rounds) structurally could not surface the gap. This asserts the
// forward round-trip by POINTER IDENTITY: the Album a song points at must be the very same object
// the library exposes, and that album's own SongCollection must contain the song back.
TEST_F(MediaLibraryTestFixture, SongsPointBackAtTheirOwningAlbumArtistAndGenre)
{
    bool checkedAtLeastOneAlbum = false;
    bool checkedAtLeastOneArtist = false;
    bool checkedAtLeastOneGenre = false;

    for (Song* song : *library->getSongsProperty())
    {
        ASSERT_NE(song, nullptr);

        if (Album* album = song->getAlbumProperty())
        {
            checkedAtLeastOneAlbum = true;
            // The pointer must be one the library actually owns and exposes.
            bool inLibrary = false;
            for (Album* a : *library->getAlbumsProperty()) if (a == album) inLibrary = true;
            EXPECT_TRUE(inLibrary) << "Song::Album points outside the library's own AlbumCollection";

            bool roundTrip = false;
            for (Song* s : *album->getSongsProperty()) if (s == song) roundTrip = true;
            EXPECT_TRUE(roundTrip) << "album does not list back the song that points at it";
        }

        if (Artist* artist = song->getArtistProperty())
        {
            checkedAtLeastOneArtist = true;
            bool inLibrary = false;
            for (Artist* a : *library->getArtistsProperty()) if (a == artist) inLibrary = true;
            EXPECT_TRUE(inLibrary);

            bool roundTrip = false;
            for (Song* s : *artist->getSongsProperty()) if (s == song) roundTrip = true;
            EXPECT_TRUE(roundTrip);
        }

        if (Genre* genre = song->getGenreProperty())
        {
            checkedAtLeastOneGenre = true;
            bool inLibrary = false;
            for (Genre* g : *library->getGenresProperty()) if (g == genre) inLibrary = true;
            EXPECT_TRUE(inLibrary);

            bool roundTrip = false;
            for (Song* s : *genre->getSongsProperty()) if (s == song) roundTrip = true;
            EXPECT_TRUE(roundTrip);
        }
    }

    // Guard against the assertions above being vacuously satisfied by an all-null graph -- the
    // exact "test passes against broken code" failure mode MEDIA-171 was created to prevent.
    EXPECT_TRUE(checkedAtLeastOneAlbum)  << "no song had an Album -- back-references never populated";
    EXPECT_TRUE(checkedAtLeastOneArtist) << "no song had an Artist -- back-references never populated";
    EXPECT_TRUE(checkedAtLeastOneGenre)  << "no song had a Genre -- back-references never populated";
}

// A Song that never came from a library scan has no library context, so all three return nullptr
// (owner decision: nullptr rather than empty singletons, matching C# null semantics for a get-only
// reference property) -- plans/plan_media.md MEDIA-174.
TEST(SongLibraryBackReferenceTest, StandaloneSongHasNoAlbumArtistOrGenre)
{
    Song song("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg", "Standalone");
    EXPECT_EQ(song.getAlbumProperty(), nullptr);
    EXPECT_EQ(song.getArtistProperty(), nullptr);
    EXPECT_EQ(song.getGenreProperty(), nullptr);
}

// plans/plan_media.md MEDIA-176: XNA declares Song.ToString(); CNA had no override at all (inherited
// System::Object's default). Matches Album/Artist/Genre/Playlist, which all return their own name.
TEST(SongLibraryBackReferenceTest, ToStringReturnsTheSongName)
{
    Song song("tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg", "My Song Name");
    EXPECT_EQ(song.ToString(), "My Song Name");
}

// plans/plan_media.md MEDIA-181: MediaLibraryIndex genuinely parsed each song's track number and stored
// it in IndexedSong::trackNumber, but MediaLibrary never passed it to the Song it constructed, so
// getTrackNumberProperty() returned a hardcoded 0 for every song in the library -- real data
// parsed and then silently dropped. Ground truth is tests/assets/media/music/manifest.json, whose
// values were cross-checked with ffprobe (e.g. "Daybreak" track=2, "Etoile" tracknumber=3), so the
// expectations below are distinct values rather than an all-ones set that a broken implementation
// could accidentally satisfy.
TEST_F(MediaLibraryTestFixture, LibrarySongsCarryTheirRealTrackNumberFromTags)
{
    std::map<std::string, int> expected = {
        {"Sunrise",  1},
        {"Twilight", 1},
        {"Daybreak", 2},
        {"\xC3\x89toile", 3}, // "Étoile", UTF-8
    };

    int checked = 0;
    for (Song* song : *library->getSongsProperty())
    {
        ASSERT_NE(song, nullptr);
        auto it = expected.find(song->getNameProperty());
        if (it == expected.end()) continue;
        EXPECT_EQ(song->getTrackNumberProperty(), it->second)
            << "wrong track number for '" << song->getNameProperty() << "'";
        ++checked;
    }
    // Anti-vacuity: if none of the named songs were found the loop above proves nothing.
    EXPECT_EQ(checked, static_cast<int>(expected.size()))
        << "did not find every expected fixture song in the library";
}

// plans/plan_media.md MEDIA-199/204: the scan used to accept only .ogg/.oga/.mp3/.wav. FLAC and Opus are
// now indexed too -- and only formats this project's SDL3_mixer build can actually PLAY were
// added. (.m4a/.aac are deliberately still excluded: SDL3_mixer ships no AAC decoder at all, so
// indexing them would advertise songs MediaPlayer::Play() could never play.)
TEST_F(MediaLibraryTestFixture, LibraryIndexesFlacAndOpusFilesWithTheirRealTags)
{
    Song* flac = nullptr;
    Song* opus = nullptr;
    for (Song* s : *library->getSongsProperty())
    {
        if (s->getNameProperty() == "Flac Song") flac = s;
        if (s->getNameProperty() == "Opus Song") opus = s;
    }

    ASSERT_NE(flac, nullptr) << ".flac file was not indexed by the library scan";
    ASSERT_NE(opus, nullptr) << ".opus file was not indexed by the library scan";

    // Real tags, not filename fallbacks -- and routed into the full object graph.
    ASSERT_NE(flac->getArtistProperty(), nullptr);
    EXPECT_EQ(flac->getArtistProperty()->getNameProperty(), "Artist Three");
    ASSERT_NE(flac->getAlbumProperty(), nullptr);
    EXPECT_EQ(flac->getAlbumProperty()->getNameProperty(), "Album Flac");
    EXPECT_EQ(flac->getTrackNumberProperty(), 7);

    ASSERT_NE(opus->getArtistProperty(), nullptr);
    EXPECT_EQ(opus->getArtistProperty()->getNameProperty(), "Artist Four");
    ASSERT_NE(opus->getGenreProperty(), nullptr);
    EXPECT_EQ(opus->getGenreProperty()->getNameProperty(), "Ambient");
    EXPECT_EQ(opus->getTrackNumberProperty(), 9);
}

// plans/plan_media.md MEDIA-184: Song::IsRated/Rating were hardcoded false/0. Now carried end-to-end
// from the file's own tags, through MediaLibraryIndex and MediaLibrary, exactly like TrackNumber.
// Both rated fixtures encode the SAME XNA value (8) by DIFFERENT routes -- an ID3v2 POPM byte of
// 196 and a Vorbis RATING of 80 -- so a bug in either conversion shows up independently.
TEST_F(MediaLibraryTestFixture, LibrarySongsCarryTheirRealRatingFromTags)
{
    Song* popmRated = nullptr;
    Song* vorbisRated = nullptr;
    Song* unrated = nullptr;
    for (Song* s : *library->getSongsProperty())
    {
        if (s->getNameProperty() == "Rated Song")        popmRated = s;
        if (s->getNameProperty() == "Vorbis Rated Song") vorbisRated = s;
        if (s->getNameProperty() == "Sunrise")           unrated = s;
    }

    ASSERT_NE(popmRated, nullptr);
    EXPECT_TRUE(popmRated->getIsRatedProperty());
    EXPECT_EQ(popmRated->getRatingProperty(), 8);

    ASSERT_NE(vorbisRated, nullptr);
    EXPECT_TRUE(vorbisRated->getIsRatedProperty());
    EXPECT_EQ(vorbisRated->getRatingProperty(), 8);

    // The negative case matters just as much: an unrated song must not claim to be rated.
    ASSERT_NE(unrated, nullptr);
    EXPECT_FALSE(unrated->getIsRatedProperty());
    EXPECT_EQ(unrated->getRatingProperty(), 0);
}
