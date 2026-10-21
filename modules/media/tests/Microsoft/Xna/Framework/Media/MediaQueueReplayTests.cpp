// SPDX-License-Identifier: MS-PL
// ISSUE-058: valid queue aliases must survive replacement until Play finishes.
#include <gtest/gtest.h>
#include <string>
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"
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
    EXPECT_GT(caller.getDurationProperty().getTicksProperty(), 0);
#endif
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
