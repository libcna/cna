// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Media::MediaPlayer;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::SongCollection;

namespace
{
    constexpr const char* kFixtureA =
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
    constexpr const char* kFixtureB =
        "tests/assets/media/music/Artist One/Album Beta/01 - Twilight.mp3";

    class MediaPlayerTest : public ::testing::Test
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
            MediaPlayer::setIsRepeatingProperty(false);
            MediaPlayer::setIsShuffledProperty(false);
        }
    };
}

// plan_media.md MEDIA-32: the no-SOUND_ENABLED fallback is a pure, always-compiled function, so
// it can be exercised directly regardless of which audio backend this build has.
TEST(MediaPlayerNoSoundFallbackTest, DoesNotTriggerForNullActiveSong)
{
    EXPECT_FALSE(MediaPlayer::DetectSongEndedByElapsedTime(nullptr, System::TimeSpan::FromSeconds(5)));
}

TEST(MediaPlayerNoSoundFallbackTest, DoesNotTriggerWhenDurationIsUnknown)
{
    Song song(kFixtureA);
    ASSERT_EQ(song.getDurationProperty(), System::TimeSpan::Zero);
    EXPECT_FALSE(MediaPlayer::DetectSongEndedByElapsedTime(&song, System::TimeSpan::FromSeconds(999)));
}

TEST(MediaPlayerNoSoundFallbackTest, DoesNotTriggerBeforeDurationElapses)
{
    Song song(kFixtureA, "Sunrise", 2000);
    EXPECT_FALSE(MediaPlayer::DetectSongEndedByElapsedTime(&song, System::TimeSpan::FromMilliseconds(1000)));
}

TEST(MediaPlayerNoSoundFallbackTest, TriggersOnceElapsedReachesDuration)
{
    Song song(kFixtureA, "Sunrise", 2000);
    EXPECT_TRUE(MediaPlayer::DetectSongEndedByElapsedTime(&song, System::TimeSpan::FromMilliseconds(2000)));
    EXPECT_TRUE(MediaPlayer::DetectSongEndedByElapsedTime(&song, System::TimeSpan::FromMilliseconds(2500)));
}

// plan_media.md MEDIA-33: FNA's shuffle picks uniformly among every index, including the one
// currently playing -- confirmed by reading MediaPlayer.cs's NextSong (no exclusion). With a
// 2-song queue and enough trials, a genuinely uniform pick must repeat the same index back-to-back
// at least once; an (incorrect) "exclude current" implementation never would. P(0 repeats in 200
// independent Bernoulli(0.5) trials) is astronomically small, so this is not a flaky test.
TEST_F(MediaPlayerTest, ShuffleCanRepeatTheSameSongIndex)
{
    Song a(kFixtureA, "A");
    Song b(kFixtureB, "B");
    SongCollection songs({&a, &b});

    MediaPlayer::setIsShuffledProperty(true);
    MediaPlayer::Play(songs, 0);

    bool observedRepeat = false;
    SharpRuntime::intcs previousIndex = MediaPlayer::getQueueProperty().getActiveSongIndexProperty();
    for (int i = 0; i < 200; ++i)
    {
        MediaPlayer::MoveNext();
        SharpRuntime::intcs currentIndex = MediaPlayer::getQueueProperty().getActiveSongIndexProperty();
        if (currentIndex == previousIndex)
        {
            observedRepeat = true;
            break;
        }
        previousIndex = currentIndex;
    }
    EXPECT_TRUE(observedRepeat);
}

// plan_media.md MEDIA-34: FNA's own comment -- "XNA duplicates the Song object and then assigns a
// bunch of stuff to it at Play time" -- Play() enqueues a *copy*, not the caller's own instance.
TEST_F(MediaPlayerTest, PlayEnqueuesADuplicateNotTheOriginalInstance)
{
    Song original(kFixtureA, "Sunrise");
    MediaPlayer::Play(&original);

    Song* queued = MediaPlayer::getQueueProperty().getActiveSongProperty();
    ASSERT_NE(queued, nullptr);
    EXPECT_NE(queued, &original);
    EXPECT_EQ(queued->getNameProperty(), original.getNameProperty());
    EXPECT_TRUE(queued->Equals(&original));

    original.setPlayCountProperty(42);
    EXPECT_NE(queued->getPlayCountProperty(), 42);
}

TEST_F(MediaPlayerTest, GameHasControlIsAlwaysTrue)
{
    EXPECT_TRUE(MediaPlayer::getGameHasControlProperty());
}

TEST_F(MediaPlayerTest, VolumeClampsToZeroOneRange)
{
    MediaPlayer::setVolumeProperty(2.0f);
    EXPECT_FLOAT_EQ(MediaPlayer::getVolumeProperty(), 1.0f);
    MediaPlayer::setVolumeProperty(-1.0f);
    EXPECT_FLOAT_EQ(MediaPlayer::getVolumeProperty(), 0.0f);
}
