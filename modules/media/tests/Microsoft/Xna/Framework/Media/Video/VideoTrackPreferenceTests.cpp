// SPDX-License-Identifier: MS-PL
// ISSUE-100: per-Video settings are applied before decoder output setup.
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "VideoPlayerTestAccess.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using namespace Microsoft::Xna::Framework::Media;
    constexpr const char* kVideo = "tests/assets/media/video/multi_track_audio.mkv";

    TEST(VideoTrackPreferenceTest, DetachedAudioSelectionReachesTheDecoderAndSurvivesReplay)
    {
        GraphicsDevice device;
        Video video(kVideo, &device);
        video.SetAudioTrackEXT(1);
        VideoPlayer player;
        player.Play(&video);
        ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
        ASSERT_NE(player.GetTexture(), nullptr);
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 44100);
        player.Stop();
        player.Play(&video);
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 44100);
    }

    TEST(VideoTrackPreferenceTest, ExplicitVideoPreferenceOverridesThePlayerPreference)
    {
        GraphicsDevice device;
        Video video(kVideo, &device);
        video.SetAudioTrackEXT(0);
        VideoPlayer player;
        player.SetAudioTrackEXT(1);
        player.Play(&video);
        ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
        ASSERT_NE(player.GetTexture(), nullptr);
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 48000);
    }

    TEST(VideoTrackPreferenceTest, PlayerPreferenceStillAppliesWithoutAVideoOverride)
    {
        GraphicsDevice device;
        Video video(kVideo, &device);
        video.SetAudioTrackEXT(-1);
        VideoPlayer player;
        player.SetAudioTrackEXT(1);
        player.Play(&video);
        ASSERT_NE(player.GetTexture(), nullptr);
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 44100);
    }

    TEST(VideoTrackPreferenceTest, DetachedVideoPreferenceDoesNotBecomeANewPlayerDefault)
    {
        GraphicsDevice device;
        Video first(kVideo, &device), second(kVideo, &device);
        first.SetAudioTrackEXT(1);
        VideoPlayer player;
        player.Play(&first);
        ASSERT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 44100);
        player.Play(&second);
        ASSERT_NE(player.GetTexture(), nullptr);
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderSampleRate(player), 48000);
    }
}
