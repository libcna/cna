// SPDX-License-Identifier: MS-PL

#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "VideoPlayerTestAccess.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoPlayer;
using Microsoft::Xna::Framework::Media::VideoPlayerTestAccess;

namespace
{
    constexpr const char* kFixture = "tests/assets/media/video/chroma_420.mkv";
    constexpr const char* kMultiTrackFixture = "tests/assets/media/video/multi_track_audio.mkv";
}

// plan_media.md MEDIA-1: seeds tests/Microsoft/Xna/Framework/Media/Video/ so the existing
// GLOB_RECURSE test discovery picks up the subfolder with no CMakeLists.txt change. Extended into
// the full playback/disposal/loop-mute-volume/track-selection suites by MEDIA-87..90 in Phase 6.
TEST(VideoPlayerTest, DefaultConstructionMatchesFna)
{
    VideoPlayer player;
    EXPECT_FALSE(player.getIsDisposedProperty());
    EXPECT_FALSE(player.getIsLoopedProperty());
    EXPECT_FALSE(player.getIsMutedProperty());
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 1.0f);
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getVideoProperty(), nullptr);
}

// plan_media.md MEDIA-121/MEDIA-27 (found by external code review): VideoPlayer::GetTypeName(),
// not exercised anywhere in this file at all before this.
TEST(VideoPlayerTest, GetTypeNameIsFullyQualified)
{
    VideoPlayer player;
    EXPECT_EQ(player.GetTypeName(), "Microsoft.Xna.Framework.Media.VideoPlayer");
}

// plan_media.md MEDIA-121 (found by external code review): getVideoProperty() after a successful
// Play() -- previously untested (only the default/never-played nullptr case was), and this
// specific gap masked a real bug during Phase 8's own track-switching fix: OpenDecoder()'s
// CloseDecoder() call unconditionally resets video_ to nullptr and nothing restored it, so
// getVideoProperty() would have silently returned nullptr for the entire remainder of every
// Play() call.
TEST(VideoPlayerTest, GetVideoPropertyReturnsThePlayedVideoAfterPlay)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    EXPECT_EQ(player.getVideoProperty(), &video);
}

// plan_media.md MEDIA-43: every public method throws once disposed.
TEST(VideoPlayerTest, MethodsThrowObjectDisposedExceptionAfterDispose)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    VideoPlayer player;
    player.Play(&video);
    player.Dispose();

    EXPECT_THROW(player.Play(&video), System::ObjectDisposedException);
    EXPECT_THROW(player.Stop(), System::ObjectDisposedException);
    EXPECT_THROW(player.Pause(), System::ObjectDisposedException);
    EXPECT_THROW(player.Resume(), System::ObjectDisposedException);
    EXPECT_THROW(player.GetTexture(), System::ObjectDisposedException);
    EXPECT_THROW(player.SetAudioTrackEXT(0), System::ObjectDisposedException);
    EXPECT_THROW(player.SetVideoTrackEXT(0), System::ObjectDisposedException);
}

// plan_media.md MEDIA-43: Dispose() is deliberately kept idempotent (not guarded), since
// ~VideoPlayer() unconditionally calls Dispose() -- see the CheckDisposed comment in
// VideoPlayer.cpp for why replicating FNA's literal double-Dispose-throws behavior would be
// dangerous in C++.
TEST(VideoPlayerTest, DisposeIsIdempotent)
{
    VideoPlayer player;
    player.Dispose();
    EXPECT_NO_THROW(player.Dispose());
    EXPECT_TRUE(player.getIsDisposedProperty());
}

// plan_media.md MEDIA-42: Play() validates the video's declared metadata against what the file
// actually reports and throws InvalidOperationException on mismatch.
TEST(VideoPlayerTest, PlayThrowsInvalidOperationExceptionOnDimensionMismatch)
{
    GraphicsDevice gd;
    // chroma_420.mkv is actually 160x90 @ 25fps -- declare deliberately wrong metadata via the
    // XNB-sourced (7-arg) constructor, which trusts the caller without probing the file.
    Video mismatched(kFixture, &gd, 2000, 320, 240, 25.0f,
                      Microsoft::Xna::Framework::Media::VideoSoundtrackType::MusicAndDialog);

    VideoPlayer player;
    EXPECT_THROW(player.Play(&mismatched), System::InvalidOperationException);
}

TEST(VideoPlayerTest, PlayWithMatchingMetadataDoesNotThrow)
{
    GraphicsDevice gd;
    Video correct(kFixture, &gd, 2000, 160, 90, 25.0f,
                   Microsoft::Xna::Framework::Media::VideoSoundtrackType::MusicAndDialog);

    VideoPlayer player;
    EXPECT_NO_THROW(player.Play(&correct));
    EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
}

TEST(VideoPlayerTest, PlayRealFixtureProducesATextureOfCorrectSize)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    ASSERT_EQ(video.getWidthProperty(), 160);
    ASSERT_EQ(video.getHeightProperty(), 90);

    VideoPlayer player;
    player.Play(&video);
    auto* texture = player.GetTexture();
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidthProperty(), 160);
    EXPECT_EQ(texture->getHeightProperty(), 90);
}

// plan_media.md MEDIA-41: a non-looped 2-second video eventually reaches Stopped once played past
// its own duration (whether or not a real audio device is available in this environment -- if one
// is, this also exercises the "wait for queued audio to drain" branch; if not, audioStream_ stays
// null and it stops as soon as the decoder reports EOF, which is still correct).
TEST(VideoPlayerTest, NonLoopedVideoEventuallyStopsAfterItsDuration)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    VideoPlayer player;
    player.setIsLoopedProperty(false);
    player.Play(&video);

    bool reachedStopped = false;
    for (int i = 0; i < 40; ++i) // up to ~4s of real wall-clock time for a ~2s clip
    {
        player.GetTexture();
        if (player.getStateProperty() == MediaState::Stopped)
        {
            reachedStopped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_TRUE(reachedStopped);
}

TEST(VideoPlayerTest, LoopedVideoKeepsPlayingPastItsDuration)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    VideoPlayer player;
    player.setIsLoopedProperty(true);
    player.Play(&video);

    std::this_thread::sleep_for(std::chrono::milliseconds(2500)); // past the ~2s clip duration
    player.GetTexture();
    EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
}

// plan_media.md MEDIA-87: real playback-state transitions across Stop/Pause/Resume -- previously
// only Play()'s own Playing transition was covered (PlayWithMatchingMetadataDoesNotThrow) and
// Stop/Pause/Resume were only exercised post-Dispose() as throw-guards.
TEST(VideoPlayerTest, StopPauseResumeTransitionStateCorrectly)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    VideoPlayer player;

    player.Play(&video);
    ASSERT_EQ(player.getStateProperty(), MediaState::Playing);

    player.Pause();
    EXPECT_EQ(player.getStateProperty(), MediaState::Paused);

    player.Resume();
    EXPECT_EQ(player.getStateProperty(), MediaState::Playing);

    player.Stop();
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
}

// plan_media.md MEDIA-87: Pause()/Resume() are documented no-ops outside their expected source
// state (Playing for Pause, Paused for Resume) -- confirms that guard, not just the happy path.
TEST(VideoPlayerTest, PauseAndResumeAreNoOpsOutsideExpectedState)
{
    VideoPlayer player;
    ASSERT_EQ(player.getStateProperty(), MediaState::Stopped);

    player.Pause(); // no-op: not Playing
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);

    player.Resume(); // no-op: not Paused
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
}

// plan_media.md MEDIA-87: PlayPosition -- zero coverage anywhere else in this file.
TEST(VideoPlayerTest, PlayPositionIsZeroWhenStoppedAndAdvancesWhilePlaying)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    VideoPlayer player;

    EXPECT_EQ(player.getPlayPositionProperty(), System::TimeSpan::Zero);

    player.Play(&video);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_GT(player.getPlayPositionProperty().getTotalMillisecondsProperty(), 0.0);

    player.Stop();
    EXPECT_EQ(player.getPlayPositionProperty(), System::TimeSpan::Zero);
}

// plan_media.md MEDIA-89: IsMuted round-trip -- DefaultConstructionMatchesFna only ever checked
// the default (false) value, never an actual set/get round trip.
TEST(VideoPlayerTest, IsMutedRoundTrips)
{
    VideoPlayer player;
    EXPECT_FALSE(player.getIsMutedProperty());
    player.setIsMutedProperty(true);
    EXPECT_TRUE(player.getIsMutedProperty());
    player.setIsMutedProperty(false);
    EXPECT_FALSE(player.getIsMutedProperty());
}

// plan_media.md MEDIA-89: Volume clamping -- zero coverage anywhere in this file (only the
// default value of 1.0f was checked).
TEST(VideoPlayerTest, VolumeClampsToZeroOneRange)
{
    VideoPlayer player;
    player.setVolumeProperty(2.0f);
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 1.0f);
    player.setVolumeProperty(-1.0f);
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.0f);
}

// plan_media.md MEDIA-90: SetAudioTrackEXT/SetVideoTrackEXT round-trip against a real multi-track
// fixture (tests/assets/media/video/multi_track_audio.mkv, added Phase 6 -- no such fixture
// existed before). VideoPlayer exposes no getter to directly observe which track is active (the
// underlying switch itself is directly, numerically verified at the VideoDecoder level by
// VideoDecoderTests.cpp's SetAudioStreamSwitchesToTheRequestedTrack), so this confirms the
// VideoPlayer-level wiring: calling these methods after Play() doesn't throw and playback keeps
// producing valid frames afterward.
TEST(VideoPlayerTest, SetAudioTrackEXTAndSetVideoTrackEXTDoNotBreakPlaybackAfterPlay)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    EXPECT_NO_THROW(player.SetAudioTrackEXT(1));
    EXPECT_NO_THROW(player.SetVideoTrackEXT(0));

    auto* texture = player.GetTexture();
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidthProperty(), 160);
    EXPECT_EQ(texture->getHeightProperty(), 90);
}

// plan_media.md MEDIA-90: a real, numeric regression test for the track-switching bug found by
// external code review -- the previous version of this test only checked "doesn't throw" and the
// texture's dimensions, which the fixture's single video track could never actually distinguish.
// multi_track_audio.mkv's two audio tracks deliberately use different sample rates (48000 Hz vs
// 44100 Hz -- see its manifest.json), so this can directly prove SetAudioTrackEXT() mid-playback
// actually reconfigures the real decoder-facing sample rate, not just that the call succeeds.
TEST(VideoPlayerTest, SetAudioTrackEXTMidPlaybackActuallyChangesTheActiveSampleRate)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    ASSERT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 48000); // track 0, the default

    player.SetAudioTrackEXT(1);
    EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 44100); // track 1
}

// plan_media.md MEDIA-90: the ordering half of the same bug -- a track preference set BEFORE
// Play() (not just mid-playback, as the test above covers) must still apply to the real decoder
// used for the SDL audio stream/texture created inside Play() -> OpenDecoder(), not just be
// silently ignored because it wasn't there yet when OpenDecoder() first built them.
TEST(VideoPlayerTest, AudioTrackPreferenceSetBeforePlayAppliesToTheOpenedDecoder)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;

    player.SetAudioTrackEXT(1); // set before Play() -- decoder_ doesn't exist yet
    player.Play(&video);

    EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 44100); // track 1, not the default
}
