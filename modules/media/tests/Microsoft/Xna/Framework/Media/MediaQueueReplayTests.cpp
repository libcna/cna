// SPDX-License-Identifier: MS-PL
// ISSUE-058: valid queue aliases must survive replacement until Play finishes.
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"
#include "CNA/TestSupport/TestScratch.hpp"
#include "Microsoft/Xna/Framework/Media/Album.hpp"
#include "Microsoft/Xna/Framework/Media/Artist.hpp"
#include "Microsoft/Xna/Framework/Media/Genre.hpp"
#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/Environment.hpp"

using namespace Microsoft::Xna::Framework::Media;

namespace
{
    constexpr const char* AudioFile =
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";

    class MediaQueueReplayTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
            MediaPlayer::Stop();
            MediaPlayer::getQueueProperty().Clear();
            MediaPlayer::setIsRepeatingProperty(false);
            MediaPlayer::setIsShuffledProperty(false);
        }

        void TearDown() override
        {
            MediaPlayer::Stop();
            MediaPlayer::getQueueProperty().Clear();
        }

        void SeedQueue()
        {
            auto& queue = MediaPlayer::getQueueProperty();
            queue.Add(new Song(AudioFile, std::string(128, 'A')));
            queue.Add(new Song(AudioFile, std::string(128, 'B')));
            queue.setActiveSongIndexProperty(1);
        }

        void CheckSingleReplay(int index)
        {
            SeedQueue();
            auto& queue = MediaPlayer::getQueueProperty();
            Song* input = queue[index];
            ASSERT_NE(input, nullptr);
            const std::string name = input->getNameProperty();
            MediaPlayer::Play(input);
            ASSERT_EQ(queue.getCountProperty(), 1);
            EXPECT_EQ(queue.getActiveSongIndexProperty(), 0);
            ASSERT_NE(queue.getActiveSongProperty(), nullptr);
            EXPECT_EQ(queue.getActiveSongProperty()->getNameProperty(), name);
            EXPECT_EQ(queue.getActiveSongProperty()->getHandle(), AudioFile);
            EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing);
            // input belonged to the replaced queue; never dereference it after Play.
        }
    };

TEST_F(MediaQueueReplayTest, CallerOwnedSongKeepsExistingMutationSemantics)
{
    SeedQueue();
    Song caller(AudioFile, std::string(128, 'C'));
    caller.setPlayCountProperty(17);
    MediaPlayer::Play(&caller);
    auto& queue = MediaPlayer::getQueueProperty();
    ASSERT_EQ(queue.getCountProperty(), 1);
    ASSERT_NE(queue.getActiveSongProperty(), nullptr);
    EXPECT_NE(queue.getActiveSongProperty(), &caller);
    EXPECT_EQ(queue.getActiveSongProperty()->getNameProperty(), caller.getNameProperty());
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing);
#ifdef SOUND_ENABLED
    EXPECT_EQ(caller.getPlayCountProperty(), 18);
    // RV-MEDIA review #6: XNA's Song keeps the duration its constructor cached (URI .ctor
    // IL_003a-0040, get_Duration IL_0006-000c); the decoded duration is the queue entry's.
    EXPECT_EQ(caller.getDurationProperty().getTicksProperty(), 0);
    EXPECT_GT(queue.getActiveSongProperty()->getDurationProperty().getTicksProperty(), 0);
#endif
}

// RV-MEDIA review #1: XNA's queue item is the native item with its metadata (MediaQueue.get_Item
// IL_001f-0032 -> Song(uint32) IL_0097-00d9). A queue entry rebuilt from the file name lost the
// duration (and album, artist, genre, track number) of every song not yet played.
TEST_F(MediaQueueReplayTest, QueueEntriesKeepTheirSongsMetadata)
{
    Song first(AudioFile, "First", 1234);
    Song second(AudioFile, "Second", 5678);
    SongCollection inputs({&first, &second});
    MediaPlayer::Play(inputs, 0);
    auto& queue = MediaPlayer::getQueueProperty();
    ASSERT_EQ(queue.getCountProperty(), 2);
    EXPECT_EQ(queue[1]->getDurationProperty().getTotalMillisecondsProperty(), 5678.0);
    EXPECT_EQ(queue[1]->getNameProperty(), "Second");
}

// Review rv-fix7 #1: a queue entry keeps a scanned song's Album, Artist and Genre while their
// library lives (XNA's queue item wraps the native item, MediaQueue.get_Item IL_001f-0032, and
// Song.get_Album/get_Artist/get_Genre IL_002a-0048 query it); once the library is gone the entry
// answers the Empty sentinels instead of dangling.
TEST_F(MediaQueueReplayTest, QueueEntriesKeepLibraryRelationshipsWhileTheLibraryLives)
{
    CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("tests/assets/media/music");
    CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("tests/assets/media/pictures");
    auto library = std::make_unique<MediaLibrary>();
    Song* scanned = nullptr;
    for (SharpRuntime::intcs i = 0; i < library->getSongsProperty()->getCountProperty(); ++i)
    {
        Song* candidate = (*library->getSongsProperty())[i];
        if (!candidate->getAlbumProperty()->getNameProperty().empty() &&
            !candidate->getArtistProperty()->getNameProperty().empty() &&
            !candidate->getGenreProperty()->getNameProperty().empty())
        {
            scanned = candidate;
            break;
        }
    }
    ASSERT_NE(scanned, nullptr) << "the fixture library has no song with album, artist and genre";
    Album* album = scanned->getAlbumProperty();
    Artist* artist = scanned->getArtistProperty();
    Genre* genre = scanned->getGenreProperty();

    MediaPlayer::Play(scanned);
    auto& queue = MediaPlayer::getQueueProperty();
    ASSERT_EQ(queue.getCountProperty(), 1);
    Song* entry = queue.getActiveSongProperty();
    ASSERT_NE(entry, scanned) << "the queue holds its own copy";
    EXPECT_EQ(entry->getAlbumProperty(), album);
    EXPECT_EQ(entry->getArtistProperty(), artist);
    EXPECT_EQ(entry->getGenreProperty(), genre);

    library.reset();
    CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("");
    CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("");
    EXPECT_EQ(entry->getAlbumProperty()->getNameProperty(), "");
    EXPECT_EQ(entry->getArtistProperty()->getNameProperty(), "");
    EXPECT_EQ(entry->getGenreProperty()->getNameProperty(), "");
    EXPECT_EQ(entry->getAlbumProperty()->getSongsProperty()->getCountProperty(), 0);
}

// RV-MEDIA review #7: XNA plays the song's handle (MediaQueue.Play(Song) IL_001b-002e); a song
// whose file was removed after it was made is not rebuilt from the file, so Play does not throw
// FileNotFoundException, and the queue holds the song.
TEST_F(MediaQueueReplayTest, PlayingASongWhoseFileWasRemovedDoesNotRebuildItFromTheFile)
{
    const auto copy = CNA::TestSupport::FixtureRoot() / "rv-media-7-removed.ogg";
    std::filesystem::copy_file(AudioFile, copy, std::filesystem::copy_options::overwrite_existing);
    Song song(copy.string(), "Removed");
    std::filesystem::remove(copy);
    EXPECT_NO_THROW(MediaPlayer::Play(&song));
    auto& queue = MediaPlayer::getQueueProperty();
    ASSERT_EQ(queue.getCountProperty(), 1);
    EXPECT_EQ(queue.getActiveSongProperty()->getNameProperty(), "Removed");
}

TEST_F(MediaQueueReplayTest, ReplaysFirstNonActiveQueueEntry)
{
    CheckSingleReplay(0);
}

TEST_F(MediaQueueReplayTest, ReplaysNonFirstActiveQueueEntry)
{
    CheckSingleReplay(1);
}

TEST_F(MediaQueueReplayTest, CallerOwnedCollectionRemainsIndependent)
{
    SeedQueue();
    Song first(AudioFile, std::string(128, 'C'));
    Song second(AudioFile, std::string(128, 'D'));
    first.setPlayCountProperty(17);
    second.setPlayCountProperty(23);
    SongCollection inputs({&first, &second});
    MediaPlayer::Play(inputs, 1);
    auto& queue = MediaPlayer::getQueueProperty();
    ASSERT_EQ(queue.getCountProperty(), 2);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), 1);
    EXPECT_EQ(queue[0]->getNameProperty(), first.getNameProperty());
    EXPECT_EQ(queue[1]->getNameProperty(), second.getNameProperty());
    EXPECT_NE(queue[0], &first);
    EXPECT_NE(queue[1], &second);
    EXPECT_EQ(first.getPlayCountProperty(), 17);
    EXPECT_EQ(second.getPlayCountProperty(), 23);
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing);
}

TEST_F(MediaQueueReplayTest, ReplaysCollectionOfQueueEntriesWithRepeatedAliases)
{
    SeedQueue();
    auto& queue = MediaPlayer::getQueueProperty();
    SongCollection inputs({queue[1], queue[0], queue[1]});
    MediaPlayer::Play(inputs, 2);
    ASSERT_EQ(queue.getCountProperty(), 3);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), 2);
    EXPECT_EQ(queue[0]->getNameProperty(), std::string(128, 'B'));
    EXPECT_EQ(queue[1]->getNameProperty(), std::string(128, 'A'));
    EXPECT_EQ(queue[2]->getNameProperty(), std::string(128, 'B'));
    EXPECT_NE(queue[0], queue[2]);
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing);
}

TEST_F(MediaQueueReplayTest, ReplaysMixedCallerAndQueueCollectionFromIndexZero)
{
    SeedQueue();
    auto& queue = MediaPlayer::getQueueProperty();
    Song caller(AudioFile, std::string(128, 'C'));
    caller.setPlayCountProperty(17);
    SongCollection inputs({queue[1], &caller, queue[0]});
    MediaPlayer::Play(inputs);
    ASSERT_EQ(queue.getCountProperty(), 3);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), 0);
    EXPECT_EQ(queue[0]->getNameProperty(), std::string(128, 'B'));
    EXPECT_EQ(queue[1]->getNameProperty(), caller.getNameProperty());
    EXPECT_EQ(queue[2]->getNameProperty(), std::string(128, 'A'));
    EXPECT_NE(queue[1], &caller);
    EXPECT_EQ(caller.getPlayCountProperty(), 17);
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing);
}
}
