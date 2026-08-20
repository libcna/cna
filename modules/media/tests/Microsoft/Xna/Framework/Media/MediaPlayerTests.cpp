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

// plans/plan_media.md MEDIA-32: the no-SOUND_ENABLED fallback is a pure, always-compiled function, so
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

// plans/plan_media.md MEDIA-33: FNA's shuffle picks uniformly among every index, including the one
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

// plans/plan_media.md MEDIA-34: FNA's own comment -- "XNA duplicates the Song object and then assigns a
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

// plans/plan_media.md MEDIA-80: the plain Play(SongCollection) overload (distinct from
// Play(SongCollection, index), already covered by ShuffleCanRepeatTheSameSongIndex/etc.) --
// confirms it starts at index 0, matching Play(songs, 0)'s own documented equivalence.
TEST_F(MediaPlayerTest, PlaySongCollectionStartsAtIndexZero)
{
    Song a(kFixtureA, "A");
    Song b(kFixtureB, "B");
    SongCollection songs({&a, &b});

    MediaPlayer::Play(songs);

    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 0);
    Song* active = MediaPlayer::getQueueProperty().getActiveSongProperty();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->getNameProperty(), "A");
}

// plans/plan_media.md MEDIA-80: State transitions across Play/Pause/Resume/Stop -- not asserted
// anywhere else in this file (other tests exercise the queue/index side effects only).
TEST_F(MediaPlayerTest, StateTransitionsAcrossPlayPauseResumeStop)
{
    EXPECT_EQ(MediaPlayer::getStateProperty(), Microsoft::Xna::Framework::Media::MediaState::Stopped);

    Song song(kFixtureA, "A");
    MediaPlayer::Play(&song);
    EXPECT_EQ(MediaPlayer::getStateProperty(), Microsoft::Xna::Framework::Media::MediaState::Playing);

    MediaPlayer::Pause();
    EXPECT_EQ(MediaPlayer::getStateProperty(), Microsoft::Xna::Framework::Media::MediaState::Paused);

    MediaPlayer::Resume();
    EXPECT_EQ(MediaPlayer::getStateProperty(), Microsoft::Xna::Framework::Media::MediaState::Playing);

    MediaPlayer::Stop();
    EXPECT_EQ(MediaPlayer::getStateProperty(), Microsoft::Xna::Framework::Media::MediaState::Stopped);
}

// plans/plan_media.md MEDIA-81: MovePrevious(), never exercised anywhere else in this file (only
// MoveNext() is, via ShuffleCanRepeatTheSameSongIndex); non-shuffled so movement is deterministic.
TEST_F(MediaPlayerTest, MovePreviousMovesBackwardThroughTheQueue)
{
    Song a(kFixtureA, "A");
    Song b(kFixtureB, "B");
    SongCollection songs({&a, &b});

    MediaPlayer::Play(songs, 1);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 1);

    MediaPlayer::MovePrevious();
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 0);
}

// plans/plan_media.md MEDIA-81: IsRepeating/IsShuffled getters -- only the setters were previously
// exercised (indirectly, via SetUp/TearDown resets and ShuffleCanRepeatTheSameSongIndex).
TEST_F(MediaPlayerTest, IsRepeatingAndIsShuffledGettersReflectSetters)
{
    EXPECT_FALSE(MediaPlayer::getIsRepeatingProperty());
    EXPECT_FALSE(MediaPlayer::getIsShuffledProperty());

    MediaPlayer::setIsRepeatingProperty(true);
    EXPECT_TRUE(MediaPlayer::getIsRepeatingProperty());

    MediaPlayer::setIsShuffledProperty(true);
    EXPECT_TRUE(MediaPlayer::getIsShuffledProperty());
}

// plans/plan_media.md MEDIA-82: IsMuted get/set, zero coverage anywhere else in this file.
TEST_F(MediaPlayerTest, IsMutedGetSet)
{
    EXPECT_FALSE(MediaPlayer::getIsMutedProperty());
    MediaPlayer::setIsMutedProperty(true);
    EXPECT_TRUE(MediaPlayer::getIsMutedProperty());
    MediaPlayer::setIsMutedProperty(false);
    EXPECT_FALSE(MediaPlayer::getIsMutedProperty());
}

// plans/plan_media.md MEDIA-83: ActiveSongChanged/MediaStateChanged, driven through the real, already-
// wired FrameworkDispatcher::Update() call chain (not a direct OnActiveSongChanged()/
// OnMediaStateChanged() invocation) -- confirms the deferred-event plumbing genuinely fires, not
// just that the raise-if-called methods work in isolation. Uses Add()/Remove() (not operator+=)
// so the stack-captured lambdas are unsubscribed before the test returns -- MediaPlayer's event
// fields are static/process-global, so a leaked operator+= subscription capturing local
// references by reference would dangle for the rest of the test binary's run.
TEST_F(MediaPlayerTest, ActiveSongChangedAndMediaStateChangedFireThroughFrameworkDispatcherUpdate)
{
    bool activeSongChangedFired = false;
    bool mediaStateChangedFired = false;
    auto activeSongToken = MediaPlayer::ActiveSongChanged.Add(
        [&activeSongChangedFired](System::Object*, const System::EventArgs&)
        {
            activeSongChangedFired = true;
        });
    auto mediaStateToken = MediaPlayer::MediaStateChanged.Add(
        [&mediaStateChangedFired](System::Object*, const System::EventArgs&)
        {
            mediaStateChangedFired = true;
        });

    Microsoft::Xna::Framework::FrameworkDispatcher::ActiveSongChanged = false;
    Microsoft::Xna::Framework::FrameworkDispatcher::MediaStateChanged = false;

    Song song(kFixtureA, "A");
    MediaPlayer::Play(&song); // flips both flags (new active song + Stopped->Playing transition)

    Microsoft::Xna::Framework::FrameworkDispatcher::Update();

    EXPECT_TRUE(activeSongChangedFired);
    EXPECT_TRUE(mediaStateChangedFired);

    MediaPlayer::ActiveSongChanged.Remove(activeSongToken);
    MediaPlayer::MediaStateChanged.Remove(mediaStateToken);
}

// plans/plan_media.md MEDIA-84/188/189/190: IsVisualizationEnabled/GetVisualizationData.
//
// This test previously asserted the STUB behavior as if it were the specification -- that the
// setter was a no-op, the getter always returned false, and GetVisualizationData left the caller's
// buffer untouched. That locked a non-functional XNA API in as "expected", the same antipattern
// MEDIA-129 had to undo elsewhere in this plan. Visualization is now genuinely implemented
// (a mixer post-output tap + a from-scratch radix-2 FFT), so these assert the real contract.
TEST_F(MediaPlayerTest, IsVisualizationEnabledRoundTrips)
{
    ASSERT_FALSE(MediaPlayer::getIsVisualizationEnabledProperty()) << "must default to off";

    MediaPlayer::setIsVisualizationEnabledProperty(true);
    EXPECT_TRUE(MediaPlayer::getIsVisualizationEnabledProperty());

    MediaPlayer::setIsVisualizationEnabledProperty(false);
    EXPECT_FALSE(MediaPlayer::getIsVisualizationEnabledProperty());
}

// With visualization disabled, XNA produces no data. CNA returns zeroed arrays rather than
// throwing or leaving the caller's buffer untouched -- matching VisualizationData's own
// zero-initialized construction (plans/plan_media.md MEDIA-189).
TEST_F(MediaPlayerTest, GetVisualizationDataZeroesTheBuffersWhileDisabled)
{
    MediaPlayer::setIsVisualizationEnabledProperty(false);

    Microsoft::Xna::Framework::Media::VisualizationData data;
    for (float& v : data.freq) v = 1.0f;
    for (float& v : data.samp) v = 1.0f;

    MediaPlayer::GetVisualizationData(data);

    for (float v : data.freq) EXPECT_FLOAT_EQ(v, 0.0f);
    for (float v : data.samp) EXPECT_FLOAT_EQ(v, 0.0f);
}

// Enabling visualization must not crash or hang even with no audio device present (the headless
// CI case). NOTE: the old wording here said it degrades to "flag is on, data stays zero" -- that
// stopped being true with MEDIA-222, which made the flag reflect reality: with no mixer the enable
// simply does not take effect and the flag stays FALSE. Corrected rather than left contradicting
// the code (plans/plan_media.md MEDIA-225).
TEST_F(MediaPlayerTest, EnablingVisualizationIsSafeWithoutAnAudioDevice)
{
    EXPECT_NO_THROW(MediaPlayer::setIsVisualizationEnabledProperty(true));

    Microsoft::Xna::Framework::Media::VisualizationData data;
    EXPECT_NO_THROW(MediaPlayer::GetVisualizationData(data));

    MediaPlayer::setIsVisualizationEnabledProperty(false);
}

// plans/plan_media.md MEDIA-220: callback installation returns bool and that result is now acted on.
// This asserts the observable contract that follows from it -- IsVisualizationEnabled must never
// report true when no tap could be installed, because a caller would then poll forever for data
// that can never arrive. On a machine where the tap DOES install, it must report true.
// NOTE ON WHAT THIS DOES *NOT* COVER (plans/plan_media.md MEDIA-222): the whole suite runs with a dummy
// SDL audio driver, so GetMixer() always succeeds here and the no-mixer/failed-install branch is
// never exercised. GetMixer() also caches its device, so a later test cannot force it to fail
// either. That branch is therefore correct BY CONSTRUCTION (the flag is assigned only from what
// actually happened, never up front) and NOT by this test -- an earlier name for this test,
// "...NeverClaimsEnabledWithoutAWorkingTap", overclaimed exactly that.
TEST_F(MediaPlayerTest, VisualizationEnabledStateStaysConsistentWithGetVisualizationData)
{
    MediaPlayer::setIsVisualizationEnabledProperty(true);
    const bool enabled = MediaPlayer::getIsVisualizationEnabledProperty();

    Microsoft::Xna::Framework::Media::VisualizationData data;
    MediaPlayer::GetVisualizationData(data);

    if (!enabled)
    {
        // Tap could not be installed: the arrays must stay zeroed rather than holding stale data.
        for (float v : data.samp) EXPECT_FLOAT_EQ(v, 0.0f);
        for (float v : data.freq) EXPECT_FLOAT_EQ(v, 0.0f);
    }
    // Either way, disabling must round-trip cleanly and not crash.
    MediaPlayer::setIsVisualizationEnabledProperty(false);
    EXPECT_FALSE(MediaPlayer::getIsVisualizationEnabledProperty());
}
