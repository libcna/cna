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

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoPlayer;

namespace
{
    constexpr const char* kFixture = "tests/assets/media/video/chroma_420.mkv";
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
