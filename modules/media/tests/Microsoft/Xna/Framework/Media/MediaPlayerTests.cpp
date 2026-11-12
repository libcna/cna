// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Media/MediaPlayer.hpp"
#include "System/Environment.hpp"
#include "System/InvalidOperationException.hpp"

#include <chrono>
#include <thread>
#include <limits>
#include <typeinfo>
#include <stdexcept>
#include <vector>
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"

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

// MEDIA-32: the no-SOUND_ENABLED fallback is a pure, always-compiled function, so
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

// MEDIA-33: FNA's shuffle picks uniformly among every index, including the one
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

// MEDIA-34: FNA's own comment -- "XNA duplicates the Song object and then assigns a
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

// CNA-DEMOS-ISSUE-098 follow-up: XNA's MediaQueue.Play refuses a song whose handle Dispose
// released with InvalidOperationException (SongPlaybackFailed), before the queue changes.
TEST_F(MediaPlayerTest, PlayRefusesADisposedSongWithoutTouchingTheQueue)
{
    Song current(kFixtureA, "Sunrise");
    MediaPlayer::Play(&current);
    Song* queued = MediaPlayer::getQueueProperty().getActiveSongProperty();
    ASSERT_NE(queued, nullptr);

    Song disposed(kFixtureB, "Twilight");
    disposed.Dispose();
    EXPECT_THROW(MediaPlayer::Play(&disposed), System::InvalidOperationException);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty(), queued);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 1);
}

// A song taken from a collection can be disposed while the collection keeps it; playing the
// collection enqueues live copies and still counts nothing against the disposed original.
TEST_F(MediaPlayerTest, PlayingACollectionCopiesADisposedMemberIntoTheQueue)
{
    Song first(kFixtureA, "Sunrise");
    Song second(kFixtureB, "Twilight");
    SongCollection songs({&first, &second});
    first.Dispose();

    EXPECT_NO_THROW(MediaPlayer::Play(songs, 0));
    ASSERT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 2);
    EXPECT_FALSE(MediaPlayer::getQueueProperty()[0]->getIsDisposedProperty());
    EXPECT_EQ(MediaPlayer::getQueueProperty()[0]->getNameProperty(), "Sunrise");
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

// MEDIA-80: the plain Play(SongCollection) overload (distinct from
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

// MEDIA-80: State transitions across Play/Pause/Resume/Stop -- not asserted
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

// MEDIA-81: MovePrevious(), never exercised anywhere else in this file (only
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

// MEDIA-81: IsRepeating/IsShuffled getters -- only the setters were previously
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

// MEDIA-82: IsMuted get/set, zero coverage anywhere else in this file.
TEST_F(MediaPlayerTest, IsMutedGetSet)
{
    EXPECT_FALSE(MediaPlayer::getIsMutedProperty());
    MediaPlayer::setIsMutedProperty(true);
    EXPECT_TRUE(MediaPlayer::getIsMutedProperty());
    MediaPlayer::setIsMutedProperty(false);
    EXPECT_FALSE(MediaPlayer::getIsMutedProperty());
}

// MEDIA-83: ActiveSongChanged/MediaStateChanged, driven through the real, already-
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

// RV-MEDIA review #4: XNA's FrameworkDispatcher.Update copies and clears its pending calls before
// invoking them (IL_001b-005a, IL_00b3-00c4), so a state change a handler makes is reported by the
// next Update. CNA cleared each flag after its handler ran and lost the Pause made inside it.
TEST_F(MediaPlayerTest, AStateChangeMadeInsideAMediaHandlerIsReportedByTheNextUpdate)
{
    using Microsoft::Xna::Framework::Media::MediaState;
    std::vector<MediaState> seen;
    auto token = MediaPlayer::MediaStateChanged.Add(
        [&seen](System::Object*, const System::EventArgs&)
        {
            seen.push_back(MediaPlayer::getStateProperty());
            if (seen.size() == 1) MediaPlayer::Pause();
        });
    Microsoft::Xna::Framework::FrameworkDispatcher::ActiveSongChanged = false;
    Microsoft::Xna::Framework::FrameworkDispatcher::MediaStateChanged = false;
    Song song(kFixtureA, "A");
    MediaPlayer::Play(&song);
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    MediaPlayer::MediaStateChanged.Remove(token);
    EXPECT_EQ(seen, (std::vector<MediaState>{MediaState::Playing, MediaState::Paused}))
        << "the Pause made inside the first notification was not reported";
}

// rv-fix5 review #3: XNA clears its dispatched copy only after every call returned (Update
// IL_0112-0117), so a handler that throws leaves the whole batch for the next Update: the song
// notification is raised again and the state notification it pre-empted is not lost.
TEST_F(MediaPlayerTest, AThrowingMediaHandlerLeavesItsBatchForTheNextUpdate)
{
    int songs = 0;
    int states = 0;
    auto songToken = MediaPlayer::ActiveSongChanged.Add(
        [&songs](System::Object*, const System::EventArgs&)
        {
            if (++songs == 1) throw std::runtime_error("first song notification");
        });
    auto stateToken = MediaPlayer::MediaStateChanged.Add(
        [&states](System::Object*, const System::EventArgs&) { ++states; });
    Microsoft::Xna::Framework::FrameworkDispatcher::ActiveSongChanged = false;
    Microsoft::Xna::Framework::FrameworkDispatcher::MediaStateChanged = false;
    Song song(kFixtureA, "A");
    MediaPlayer::Play(&song);
    EXPECT_THROW(Microsoft::Xna::Framework::FrameworkDispatcher::Update(), std::runtime_error);
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    MediaPlayer::ActiveSongChanged.Remove(songToken);
    MediaPlayer::MediaStateChanged.Remove(stateToken);
    EXPECT_EQ(songs, 2);
    EXPECT_EQ(states, 1) << "the state notification behind the throwing handler was lost";
}

// MEDIA-84/188/189/190: IsVisualizationEnabled/GetVisualizationData.
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

// XNA MediaPlayer.GetVisualizationData IL_0013..0018 branches directly to ret (IL_0040)
// while disabled. Preserve the caller's data, including nonzero data from an earlier capture.
TEST_F(MediaPlayerTest, Tq023GetVisualizationDataPreservesBothBuffersWhileDisabled)
{
    MediaPlayer::setIsVisualizationEnabledProperty(false);
    Microsoft::Xna::Framework::Media::VisualizationData data;
    for (std::size_t i = 0; i < 256; ++i)
    {
        data.freq[i] = static_cast<float>(i) / 256.0f;
        data.samp[i] = -static_cast<float>(i + 1) / 256.0f;
    }
    MediaPlayer::GetVisualizationData(data);
    for (std::size_t i = 0; i < 256; ++i)
    {
        // Hand-derived dyadic sentinels are exact; verify every slot and both domains.
        EXPECT_FLOAT_EQ(data.getFrequenciesProperty()[i], static_cast<float>(i) / 256.0f);
        EXPECT_FLOAT_EQ(data.getSamplesProperty()[i], -static_cast<float>(i + 1) / 256.0f);
    }
}

// Enabling visualization must not crash or hang even with no audio device present (the headless
// CI case). NOTE: the old wording here said it degrades to "flag is on, data stays zero" -- that
// stopped being true with MEDIA-222, which made the flag reflect reality: with no mixer the enable
// simply does not take effect and the flag stays FALSE. Corrected rather than left contradicting
// the code (MEDIA-225).
TEST_F(MediaPlayerTest, EnablingVisualizationIsSafeWithoutAnAudioDevice)
{
    EXPECT_NO_THROW(MediaPlayer::setIsVisualizationEnabledProperty(true));

    Microsoft::Xna::Framework::Media::VisualizationData data;
    EXPECT_NO_THROW(MediaPlayer::GetVisualizationData(data));

    MediaPlayer::setIsVisualizationEnabledProperty(false);
}

// MEDIA-220: callback installation returns bool and that result is now acted on.
// This asserts the observable contract that follows from it -- IsVisualizationEnabled must never
// report true when no tap could be installed, because a caller would then poll forever for data
// that can never arrive. On a machine where the tap DOES install, it must report true.
// NOTE ON WHAT THIS DOES *NOT* COVER (MEDIA-222): the whole suite runs with a dummy
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

#if defined(CNA_AUDIO_PLATFORM_ALSA)
// X11-0161: an MP3 and a FLAC song stream through CNA's own mixer to their end.
// MediaPlayer's position is a clock, not the mixer's, so what proves the file was decoded and
// played is the song finishing on its own -- which only the mixer, reaching the stream's last
// frame on ALSA's real-time `null` device, can make happen.
TEST_F(MediaPlayerTest, Mp3AndFlacSongsPlayThroughCnasOwnMixerToTheirEnd)
{
    using Microsoft::Xna::Framework::FrameworkDispatcher;
    using Microsoft::Xna::Framework::Media::MediaState;
    for (const auto& [fixture, seconds] : {std::pair<const char*, double>{kFixtureB, 2.0376},
                                          {"tests/assets/media/music/Artist Three/Album Flac/01 - Flac Song.flac", 1.0}})
    {
        Song song(fixture, "song");
        const auto start = std::chrono::steady_clock::now();
        MediaPlayer::Play(&song);
        ASSERT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing) << fixture;
        while (MediaPlayer::getStateProperty() == MediaState::Playing &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(8))
        {
            FrameworkDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Stopped) << fixture << " never ended";
        EXPECT_GE(elapsed, seconds - 0.25) << fixture << " ended before it could have been played";
        EXPECT_LE(elapsed, seconds + 2.0) << fixture;
        MediaPlayer::Stop();
    }
}
#endif

TEST_F(MediaPlayerTest, Tq002MutePreservesVolumeAndVolumeChangesDoNotUnmute)
{
    const float savedVolume = MediaPlayer::getVolumeProperty();
    const bool savedMute = MediaPlayer::getIsMutedProperty();
    // FNA MediaPlayer.Volume and IsMuted store independent properties; muting only changes
    // the effective output gain. XNA set_Volume IL_0000..0022 clamps before its native call.
    MediaPlayer::setIsMutedProperty(false);
    MediaPlayer::setVolumeProperty(0.375f);
    MediaPlayer::setIsMutedProperty(true);
    EXPECT_FLOAT_EQ(MediaPlayer::getVolumeProperty(), 0.375f);
    MediaPlayer::setVolumeProperty(0.625f);
    EXPECT_TRUE(MediaPlayer::getIsMutedProperty());
    EXPECT_FLOAT_EQ(MediaPlayer::getVolumeProperty(), 0.625f);
    MediaPlayer::setIsMutedProperty(false);
    EXPECT_FLOAT_EQ(MediaPlayer::getVolumeProperty(), 0.625f);
    MediaPlayer::setVolumeProperty(savedVolume);
    MediaPlayer::setIsMutedProperty(savedMute);
}

TEST_F(MediaPlayerTest, Tq002RepeatedTransportCallsPreserveQueueAndStoppedPosition)
{
    using Microsoft::Xna::Framework::Media::MediaState;
    // FNA Pause/Resume guard their source state; Stop retains the queue and resets its clock.
    MediaPlayer::Pause();
    MediaPlayer::Resume();
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 0);
    Song song(kFixtureA, "Retained");
    MediaPlayer::Play(&song);
    Song* queued = MediaPlayer::getQueueProperty().getActiveSongProperty();
    ASSERT_NE(queued, nullptr);
    MediaPlayer::Pause();
    const auto pausedPosition = MediaPlayer::getPlayPositionProperty();
    MediaPlayer::Pause();
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Paused);
    EXPECT_EQ(MediaPlayer::getPlayPositionProperty(), pausedPosition);
    MediaPlayer::Resume();
    MediaPlayer::Resume();
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Playing);
    MediaPlayer::Stop();
    MediaPlayer::Stop();
    MediaPlayer::Resume();
    MediaPlayer::Pause();
    EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(MediaPlayer::getPlayPositionProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 1);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 0);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty(), queued);
}

TEST_F(MediaPlayerTest, Tq002SequentialQueueWrapsAtBothEndsWithAndWithoutRepeat)
{
    // Corrected by TQ-023: XNA MediaQueue.MoveNext/MovePrevious IL_0009..0026 wrap
    // manual endpoint moves even with repeating off; FNA's clamping is a divergence.
    Song a(kFixtureA, "First"), b(kFixtureB, "Second");
    SongCollection songs({&a, &b});
    MediaPlayer::Play(songs, 0);
    MediaPlayer::MovePrevious();
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 1);
    ASSERT_NE(MediaPlayer::getQueueProperty().getActiveSongProperty(), nullptr);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty()->getNameProperty(), "Second");
    MediaPlayer::MoveNext();
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 0);
    MediaPlayer::MoveNext();
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 1);
    ASSERT_NE(MediaPlayer::getQueueProperty().getActiveSongProperty(), nullptr);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty()->getNameProperty(), "Second");
    MediaPlayer::setIsRepeatingProperty(true);
    MediaPlayer::MoveNext();
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 0);
    ASSERT_NE(MediaPlayer::getQueueProperty().getActiveSongProperty(), nullptr);
    EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty()->getNameProperty(), "First");
    EXPECT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 2);
}

namespace
{
    template<class Exception, class Action>
    void ExpectMediaArgument(Action action, const char* parameter)
    {
        try { action(); ADD_FAILURE() << "Expected argument refusal"; }
        catch (const Exception& error)
        {
            EXPECT_EQ(typeid(error), typeid(Exception));
            EXPECT_EQ(error.getParamNameProperty(), parameter);
        }
        catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
    }
}

TEST_F(MediaPlayerTest, Tq023InvalidPlayPreservesPausedQueuePositionAndPendingEvents)
{
    using Microsoft::Xna::Framework::FrameworkDispatcher;
    using Microsoft::Xna::Framework::Media::MediaState;
    Song first(kFixtureA, "Retained"), second(kFixtureB, "Second");
    SongCollection songs({&first, &second}), empty({});
    MediaPlayer::Play(songs, 1);
    MediaPlayer::Pause();
    ASSERT_EQ(MediaPlayer::getStateProperty(), MediaState::Paused);
    Song* retained = MediaPlayer::getQueueProperty().getActiveSongProperty();
    const auto position = MediaPlayer::getPlayPositionProperty();
    FrameworkDispatcher::ActiveSongChanged = false;
    FrameworkDispatcher::MediaStateChanged = false;
    const auto unchanged = [&]
    {
        EXPECT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 2);
        EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 1);
        EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty(), retained);
        EXPECT_EQ(MediaPlayer::getStateProperty(), MediaState::Paused);
        EXPECT_EQ(MediaPlayer::getPlayPositionProperty(), position);
        EXPECT_FALSE(FrameworkDispatcher::ActiveSongChanged);
        EXPECT_FALSE(FrameworkDispatcher::MediaStateChanged);
    };
    // XNA MediaPlayer.Play delegates to MediaQueue.Play: Song IL_0000..0018;
    // collection IL_0013..002a; indexed collection IL_002b..0047. All precede native mutation.
    ExpectMediaArgument<System::ArgumentNullException>([] { MediaPlayer::Play(nullptr); }, "song");
    unchanged();
    ExpectMediaArgument<System::ArgumentException>([&] { MediaPlayer::Play(empty); }, "songCollection");
    unchanged();
    for (int index : {-1, 0, 1})
    {
        // Empty-collection validation precedes index validation even for -1.
        ExpectMediaArgument<System::ArgumentException>([&] { MediaPlayer::Play(empty, index); }, "songCollection");
        unchanged();
    }
    for (int index : {-1, 2, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
    {
        ExpectMediaArgument<System::ArgumentOutOfRangeException>([&] { MediaPlayer::Play(songs, index); }, "index");
        unchanged();
    }
    SongCollection disposed({&first});
    disposed.Dispose();
    // XNA MediaQueue.Play reads SongCollection.Count (ThrowIfDisposed) before touching playback.
    EXPECT_THROW(MediaPlayer::Play(disposed), System::ObjectDisposedException);
    unchanged();
    EXPECT_THROW(MediaPlayer::Play(disposed, -1), System::ObjectDisposedException);
    unchanged();
}

TEST_F(MediaPlayerTest, Tq023ManualNavigationWrapsBothEndsRegardlessOfRepeatOrShuffle)
{
    // XNA MediaQueue.MoveNext IL_0009..0026 and MovePrevious IL_0009..0026 explicitly
    // wrap endpoints through ActiveSongIndex, without reading IsRepeating or IsShuffled.
    Song a(kFixtureA, "First"), b(kFixtureB, "Middle"), c(kFixtureA, "Last");
    SongCollection songs({&a, &b, &c});
    for (bool repeat : {false, true}) for (bool shuffle : {false, true})
    {
        SCOPED_TRACE(::testing::Message() << "repeat=" << repeat << " shuffle=" << shuffle);
        MediaPlayer::setIsRepeatingProperty(repeat);
        MediaPlayer::setIsShuffledProperty(shuffle);
        MediaPlayer::Play(songs, 2);
        MediaPlayer::MoveNext();
        EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 0);
        ASSERT_NE(MediaPlayer::getQueueProperty().getActiveSongProperty(), nullptr);
        EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty()->getNameProperty(), "First");
        MediaPlayer::MovePrevious();
        EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 2);
        ASSERT_NE(MediaPlayer::getQueueProperty().getActiveSongProperty(), nullptr);
        EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongProperty()->getNameProperty(), "Last");
        EXPECT_EQ(MediaPlayer::getQueueProperty().getCountProperty(), 3);
        if (!shuffle)
        {
            MediaPlayer::MovePrevious();
            EXPECT_EQ(MediaPlayer::getQueueProperty().getActiveSongIndexProperty(), 1);
        }
    }
}

TEST_F(MediaPlayerTest, Tq023TransportEventsAreDeferredOrderedAndNotRepeatedByNoOps)
{
    using Microsoft::Xna::Framework::FrameworkDispatcher;
    using Microsoft::Xna::Framework::Media::MediaState;
    // FNA FrameworkDispatcher.Update: ActiveSongChanged before MediaStateChanged, once per
    // pending flag. MediaPlayer.set_State only flags an actual state change.
    std::vector<char> events;
    std::vector<MediaState> observedStates;
    const auto activeToken = MediaPlayer::ActiveSongChanged.Add(
        [&](System::Object* sender, const System::EventArgs&)
        { EXPECT_EQ(sender, nullptr); events.push_back('A'); observedStates.push_back(MediaPlayer::getStateProperty()); });
    const auto stateToken = MediaPlayer::MediaStateChanged.Add(
        [&](System::Object* sender, const System::EventArgs&)
        { EXPECT_EQ(sender, nullptr); events.push_back('S'); observedStates.push_back(MediaPlayer::getStateProperty()); });
    // Unsubscribe on every exit, including an unexpected exception.
    struct Unsubscribe
    {
        decltype(activeToken) active;
        decltype(stateToken) state;
        ~Unsubscribe() { MediaPlayer::ActiveSongChanged.Remove(active); MediaPlayer::MediaStateChanged.Remove(state); }
    } unsubscribe{activeToken, stateToken};
    FrameworkDispatcher::ActiveSongChanged = false;
    FrameworkDispatcher::MediaStateChanged = false;
    Song song(kFixtureA, "Event song");
    MediaPlayer::Play(&song);
    EXPECT_TRUE(events.empty());
    FrameworkDispatcher::Update();
    EXPECT_EQ(events, (std::vector<char>{'A', 'S'}));
    FrameworkDispatcher::Update();
    EXPECT_EQ(events.size(), 2u);
    MediaPlayer::Pause();
    MediaPlayer::Pause();
    EXPECT_EQ(events.size(), 2u);
    FrameworkDispatcher::Update();
    MediaPlayer::Resume();
    MediaPlayer::Resume();
    FrameworkDispatcher::Update();
    MediaPlayer::Stop();
    MediaPlayer::Stop();
    FrameworkDispatcher::Update();
    MediaPlayer::Pause();
    FrameworkDispatcher::Update();
    EXPECT_EQ(events, (std::vector<char>{'A', 'S', 'S', 'S', 'S'}));
    EXPECT_EQ(observedStates, (std::vector<MediaState>{MediaState::Playing, MediaState::Playing,
        MediaState::Paused, MediaState::Playing, MediaState::Stopped}));
}
