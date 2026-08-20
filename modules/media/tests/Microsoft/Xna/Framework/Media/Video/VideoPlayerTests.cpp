// SPDX-License-Identifier: MS-PL

#include <chrono>
#include <cstdio>
#include <fstream>
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
    constexpr const char* kAudioTailFixture = "tests/assets/media/video/audio_tail.mkv";
}

// plans/plan_media.md MEDIA-1: seeds tests/Microsoft/Xna/Framework/Media/Video/ so the existing
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

// plans/plan_media.md MEDIA-121/MEDIA-27 (found by external code review): VideoPlayer::GetTypeName(),
// not exercised anywhere in this file at all before this.
TEST(VideoPlayerTest, GetTypeNameIsFullyQualified)
{
    VideoPlayer player;
    EXPECT_EQ(player.GetTypeName(), "Microsoft.Xna.Framework.Media.VideoPlayer");
}

// plans/plan_media.md MEDIA-121 (found by external code review): getVideoProperty() after a successful
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

// plans/plan_media.md MEDIA-43: every public method throws once disposed.
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

// plans/plan_media.md MEDIA-43: Dispose() is deliberately kept idempotent (not guarded), since
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

// plans/plan_media.md MEDIA-42: Play() validates the video's declared metadata against what the file
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

// plans/plan_platform.md PLAT-SDL2-8: this case asserts on a real mixer playback stream, which only
// exists under SOUND_ENABLED (the SDL3_mixer selection). Under CNA_AUDIO_PLATFORM=SDL2 or =NULL
// VideoPlayer opens no stream at all, so the assertion would be vacuous rather than failing --
// compiled out instead, while the rest of this suite's video coverage keeps running there.
#ifdef SOUND_ENABLED
// plans/plan_media.md MEDIA-131 regression (found by external code review): Play() left the newly
// opened audio stream paused forever -- playback-stream creation opens every stream paused
// by default, and ReconfigureAudioOutputForCurrentTrack() only resumes it when state_ == Playing,
// but OpenDecoder() (which calls that helper) used to run entirely before Play() itself ever set
// state_ to Playing. Every video with audio played completely silently.
// This uses multi_track_audio.mkv (has a real audio track) since chroma_420.mkv has none.
TEST(VideoPlayerTest, PlayGenuinelyResumesTheAudioStreamNotJustOpensIt)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    ASSERT_TRUE(VideoPlayerTestAccess::HasAudioStream(player));
    EXPECT_FALSE(VideoPlayerTestAccess::IsAudioStreamDevicePaused(player));
}
#endif  // SOUND_ENABLED

// plans/plan_platform.md PLAT-SDL2-8: this case asserts on a real mixer playback stream, which only
// exists under SOUND_ENABLED (the SDL3_mixer selection). Under CNA_AUDIO_PLATFORM=SDL2 or =NULL
// VideoPlayer opens no stream at all, so the assertion would be vacuous rather than failing --
// compiled out instead, while the rest of this suite's video coverage keeps running there.
#ifdef SOUND_ENABLED
// plans/plan_media.md MEDIA-131 regression: Pause() must still actually pause the audio device (the
// fix above must not have removed Pause()'s own real behavior while fixing the resume-on-Play bug).
TEST(VideoPlayerTest, PauseStillActuallyPausesTheAudioStream)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);
    ASSERT_FALSE(VideoPlayerTestAccess::IsAudioStreamDevicePaused(player));

    player.Pause();
    EXPECT_TRUE(VideoPlayerTestAccess::IsAudioStreamDevicePaused(player));

    player.Resume();
    EXPECT_FALSE(VideoPlayerTestAccess::IsAudioStreamDevicePaused(player));
}
#endif  // SOUND_ENABLED

// plans/plan_media.md MEDIA-41: a non-looped 2-second video eventually reaches Stopped once played past
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

// plans/plan_platform.md PLAT-SDL2-8: needs the decoder/mixer engine, which is the SDL3_mixer
// implementation and is absent from the archive for every other CNA_AUDIO_PLATFORM value.
// Without it a SoundEffect reports a zero duration and VideoPlayer opens no audio stream,
// so this case is unobservable there rather than merely untested.
#ifdef SOUND_ENABLED
// plans/plan_media.md MEDIA-130/MEDIA-41: audio_tail.mkv's video track is 2.0s but its audio track is
// deliberately 3.0s (see its own manifest.json) -- confirms VideoPlayer genuinely stays Playing
// past the video's own duration until the queued audio has actually drained, not just until video
// EOF, matching FNA's VideoPlayerTheora "wait for PendingBufferCount==0" behavior. Previously
// documented as a real gap (the underlying bug was fixed and covered only indirectly at the
// VideoDecoder level) -- closed for real now with a genuine VideoPlayer-level test.
TEST(VideoPlayerTest, NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration)
{
    GraphicsDevice gd;
    Video video(kAudioTailFixture, &gd);
    VideoPlayer player;
    player.setIsLoopedProperty(false);
    player.Play(&video);

    bool stillPlayingPastVideoDuration = false;
    bool reachedStopped = false;
    for (int i = 0; i < 60; ++i) // up to ~6s of real wall-clock time for a ~3s audio tail
    {
        player.GetTexture();
        const double elapsedSeconds = i * 0.1;
        if (elapsedSeconds > 2.2 && player.getStateProperty() == MediaState::Playing)
        {
            // Past the video's own 2.0s duration, but still Playing -- proves it's genuinely
            // waiting on the longer audio tail, not just video EOF.
            stillPlayingPastVideoDuration = true;
        }
        if (player.getStateProperty() == MediaState::Stopped)
        {
            reachedStopped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_TRUE(stillPlayingPastVideoDuration);
    EXPECT_TRUE(reachedStopped);
}
#endif  // SOUND_ENABLED

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

// plans/plan_media.md MEDIA-87: real playback-state transitions across Stop/Pause/Resume -- previously
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

// plans/plan_media.md MEDIA-87: Pause()/Resume() are documented no-ops outside their expected source
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

// plans/plan_media.md MEDIA-87: PlayPosition -- zero coverage anywhere else in this file.
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

// plans/plan_media.md MEDIA-89: IsMuted round-trip -- DefaultConstructionMatchesFna only ever checked
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

// plans/plan_media.md MEDIA-89: Volume clamping -- zero coverage anywhere in this file (only the
// default value of 1.0f was checked).
TEST(VideoPlayerTest, VolumeClampsToZeroOneRange)
{
    VideoPlayer player;
    player.setVolumeProperty(2.0f);
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 1.0f);
    player.setVolumeProperty(-1.0f);
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.0f);
}

// plans/plan_media.md MEDIA-90: SetAudioTrackEXT/SetVideoTrackEXT round-trip against a real multi-track
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

// plans/plan_media.md MEDIA-90: a real, numeric regression test for the track-switching bug found by
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

// plans/plan_platform.md PLAT-SDL2-8: needs the decoder/mixer engine, which is the SDL3_mixer
// implementation and is absent from the archive for every other CNA_AUDIO_PLATFORM value.
// Without it a SoundEffect reports a zero duration and VideoPlayer opens no audio stream,
// so this case is unobservable there rather than merely untested.
#ifdef SOUND_ENABLED
// plans/plan_media.md MEDIA-148 (found by external code review): before this fix, a single combined
// ReconfigureAudioAndVideoOutputForCurrentTracks() always tore down and reopened the SDL audio
// stream on every SetVideoTrackEXT() call, even a video-only switch -- discarding whatever audio
// was already queued for playback for no reason. The fix split it into independent
// ReconfigureVideoOutputForCurrentTrack()/ReconfigureAudioOutputForCurrentTrack() halves. This
// proves the split holds: the audio stream's own pointer identity survives a video track switch
// untouched (a torn-down-and-reopened stream would have a different opaque identity).
TEST(VideoPlayerTest, SetVideoTrackEXTDoesNotTearDownTheUnrelatedAudioStream)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    const void* before = VideoPlayerTestAccess::GetAudioStreamPtr(player);
    ASSERT_NE(before, nullptr);

    player.SetVideoTrackEXT(0);

    EXPECT_EQ(VideoPlayerTestAccess::GetAudioStreamPtr(player), before);
}
#endif  // SOUND_ENABLED

// The symmetric half of MEDIA-148: an audio-only track switch must not needlessly reallocate the
// video frame texture either.
TEST(VideoPlayerTest, SetAudioTrackEXTDoesNotRecreateTheUnrelatedVideoTexture)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    auto* before = player.GetTexture();
    ASSERT_NE(before, nullptr);

    player.SetAudioTrackEXT(1);

    EXPECT_EQ(player.GetTexture(), before);
}

// plans/plan_media.md MEDIA-90: the ordering half of the same bug -- a track preference set BEFORE
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

// plans/plan_media.md MEDIA-149 (found by external code review): OpenDecoder()'s try/catch around the
// first-frame decode used to only reset state_ to Stopped on failure, leaving decoder_,
// audioStream_, frameTexture_ and video_->parent_ all still allocated/set -- state_ claimed
// nothing was open while every actual resource said otherwise. The fix calls the same
// CloseDecoder() the rest of the class already relies on for a consistent teardown. Corrupts real
// H264 macroblock data inside the FIRST video packet (byte offset verified via ffprobe pkt_pos to
// fall inside the keyframe packet, not a later one) so the exception is thrown by the very first
// decoder_->NextFrame() call inside OpenDecoder()'s try block, not several frames in -- reusing
// the same corruption technique VideoDecoderTests.cpp's CorruptedMidStreamDataThrowsRatherThan-
// SilentlyEndingCleanly already established for this fixture.
TEST(VideoPlayerTest, PlayOnAFirstFrameDecodeFailureLeavesThePlayerFullyClosedNotHalfOpen)
{
    std::ifstream src("tests/assets/media/video/corrupt_test_h264.mp4", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<char> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 100u);
    src.close();

    const std::string corruptedPath =
        "tests/assets/media/video/.player_first_frame_corrupted_fixture.mp4";
    {
        std::size_t start = bytes.size() * 3 / 10;
        std::size_t length = std::min<std::size_t>(16, bytes.size() - start);
        for (std::size_t i = start; i < start + length; ++i)
        {
            bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0xFF);
        }
        std::ofstream out(corruptedPath, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    GraphicsDevice gd;
    Video video(corruptedPath, &gd);
    VideoPlayer player;

    EXPECT_THROW(player.Play(&video), std::runtime_error);

    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getVideoProperty(), nullptr);
    EXPECT_EQ(VideoPlayerTestAccess::GetAudioStreamPtr(player), nullptr);
    EXPECT_EQ(player.GetTexture(), nullptr);

    // The player must still be fully usable afterward, not left in some broken half-open state.
    Video secondVideo(kFixture, &gd);
    EXPECT_NO_THROW(player.Play(&secondVideo));
    EXPECT_EQ(player.getStateProperty(), MediaState::Playing);

    player.Stop();
    std::remove(corruptedPath.c_str());
}

// plans/plan_platform.md PLAT-SDL2-8: this case asserts on a real mixer playback stream, which only
// exists under SOUND_ENABLED (the SDL3_mixer selection). Under CNA_AUDIO_PLATFORM=SDL2 or =NULL
// VideoPlayer opens no stream at all, so the assertion would be vacuous rather than failing --
// compiled out instead, while the rest of this suite's video coverage keeps running there.
#ifdef SOUND_ENABLED
// plans/plan_media.md MEDIA-153 (found by external code review): decoder_->DrainAudio(audioBuffer_) ran
// unconditionally every decode iteration, but audioBuffer_.clear() only ran inside
// `if (audioStream_)` -- with no audio device (e.g. this simulated failure, or a genuinely headless
// system), audioBuffer_ accumulated the ENTIRE decoded audio track in memory for the rest of
// playback, and CloseDecoder() never cleared it either. Uses SimulateAudioDeviceBecomingUnavailable
// to exercise the no-device path deterministically (this sandbox's own audio driver may or may not
// fail on its own) against audio_tail.mkv, which genuinely has an audio track.
TEST(VideoPlayerTest, AudioBufferDoesNotAccumulateWithoutAnAudioDevice)
{
    GraphicsDevice gd;
    Video video(kAudioTailFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    ASSERT_TRUE(VideoPlayerTestAccess::HasAudioStream(player));
    VideoPlayerTestAccess::SimulateAudioDeviceBecomingUnavailable(player);
    ASSERT_FALSE(VideoPlayerTestAccess::HasAudioStream(player));

    for (int i = 0; i < 15; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        player.GetTexture();
        // DrainAndFlushAudioBuffer() always clears audioBuffer_ at the end, whether or not there
        // was a stream to feed -- so right after any GetTexture() call it must be empty, not just
        // "bounded."
        EXPECT_EQ(VideoPlayerTestAccess::GetAudioBufferSize(player), 0u)
            << "iteration " << i;
    }

    player.Stop();
}
#endif  // SOUND_ENABLED

// plans/plan_platform.md PLAT-SDL2-8: needs the decoder/mixer engine, which is the SDL3_mixer
// implementation and is absent from the archive for every other CNA_AUDIO_PLATFORM value.
// Without it a SoundEffect reports a zero duration and VideoPlayer opens no audio stream,
// so this case is unobservable there rather than merely untested.
#ifdef SOUND_ENABLED
// plans/plan_media.md MEDIA-154 (found by external code review): VideoDecoder::SetAudioStream()/
// SetVideoStream() correctly no-op at the decoder level when re-selecting the already-active track
// (or an out-of-range index), but VideoPlayer::SetAudioTrackEXT()/SetVideoTrackEXT() used to call
// their reconfigure helper unconditionally regardless, tearing down and reopening the SDL audio
// stream (discarding whatever was queued) or reallocating the texture for a "switch" that never
// actually happened. Extends the Phase 10 pointer-identity technique
// (SetVideoTrackEXTDoesNotTearDownTheUnrelatedAudioStream) to the same-track-reselect case.
TEST(VideoPlayerTest, ReselectingTheSameAudioTrackDoesNotTearDownTheStream)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    const void* before = VideoPlayerTestAccess::GetAudioStreamPtr(player);
    ASSERT_NE(before, nullptr);

    player.SetAudioTrackEXT(0); // track 0 is already active -- a true no-op

    EXPECT_EQ(VideoPlayerTestAccess::GetAudioStreamPtr(player), before);
}
#endif  // SOUND_ENABLED

TEST(VideoPlayerTest, ReselectingTheSameVideoTrackDoesNotRecreateTheTexture)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    auto* before = player.GetTexture();
    ASSERT_NE(before, nullptr);

    player.SetVideoTrackEXT(0); // track 0 is already active -- a true no-op

    EXPECT_EQ(player.GetTexture(), before);
}

// plans/plan_platform.md PLAT-SDL2-8: needs the decoder/mixer engine, which is the SDL3_mixer
// implementation and is absent from the archive for every other CNA_AUDIO_PLATFORM value.
// Without it a SoundEffect reports a zero duration and VideoPlayer opens no audio stream,
// so this case is unobservable there rather than merely untested.
#ifdef SOUND_ENABLED
// An out-of-range track index is also a true no-op at the decoder level -- must not tear down the
// stream either.
TEST(VideoPlayerTest, SelectingAnOutOfRangeAudioTrackDoesNotTearDownTheStream)
{
    GraphicsDevice gd;
    Video video(kMultiTrackFixture, &gd);
    VideoPlayer player;
    player.Play(&video);

    const void* before = VideoPlayerTestAccess::GetAudioStreamPtr(player);
    ASSERT_NE(before, nullptr);

    player.SetAudioTrackEXT(99); // out of range -- multi_track_audio.mkv has only 2 audio tracks

    EXPECT_EQ(VideoPlayerTestAccess::GetAudioStreamPtr(player), before);
}
#endif  // SOUND_ENABLED
