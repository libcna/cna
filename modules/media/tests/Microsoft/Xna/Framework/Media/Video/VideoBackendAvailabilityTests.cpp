// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Media/AudioDurationProbe.hpp"
#include "CNA/Internal/Media/VideoDecoder.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoPlayer;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

namespace
{
    constexpr const char* kFixture = "tests/assets/media/video/chroma_420.mkv";
}

#ifdef CNA_VIDEO_AVAILABLE
TEST(VideoBackendAvailabilityTest, EnabledBuildReportsDecoderAvailable)
{
    EXPECT_TRUE(CNA::Internal::Media::IsVideoDecoderAvailable());
    EXPECT_NO_THROW(CNA::Internal::Media::RequireVideoDecoderAvailable());
}
#else
TEST(VideoBackendAvailabilityTest, DisabledBuildRetainsMetadataApiAndRejectsDecoding)
{
    EXPECT_FALSE(CNA::Internal::Media::IsVideoDecoderAvailable());
    EXPECT_THROW(
        CNA::Internal::Media::RequireVideoDecoderAvailable(),
        System::NotSupportedException);

    GraphicsDevice graphicsDevice;
    EXPECT_THROW(
        Video missing("tests/assets/media/video/does-not-exist.mkv", &graphicsDevice),
        System::IO::FileNotFoundException);
    EXPECT_THROW(Video raw(kFixture, &graphicsDevice), System::NotSupportedException);
    EXPECT_THROW(
        (void)Video::FromUriEXT(kFixture, &graphicsDevice),
        System::NotSupportedException);

    Video metadata(
        kFixture,
        &graphicsDevice,
        2000,
        160,
        90,
        25.0f,
        VideoSoundtrackType::MusicAndDialog);
    EXPECT_EQ(metadata.getWidthProperty(), 160);
    EXPECT_EQ(metadata.getHeightProperty(), 90);
    EXPECT_FLOAT_EQ(metadata.getFramesPerSecondProperty(), 25.0f);

    VideoPlayer player;
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getVideoProperty(), nullptr);
    EXPECT_EQ(player.GetTexture(), nullptr);
    EXPECT_THROW(player.Play(&metadata), System::NotSupportedException);
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getVideoProperty(), nullptr);
    EXPECT_NO_THROW(player.Pause());
    EXPECT_NO_THROW(player.Resume());
    EXPECT_NO_THROW(player.Stop());

    CNA::Internal::Media::VideoDecoder decoder;
    EXPECT_FALSE(decoder.IsOpen());
    EXPECT_THROW(decoder.Open(kFixture), System::NotSupportedException);
    EXPECT_NO_THROW(decoder.Close());
    EXPECT_THROW(decoder.SeekToStart(), System::NotSupportedException);
    EXPECT_THROW(decoder.SetAudioStream(0), System::NotSupportedException);
    EXPECT_THROW(decoder.SetVideoStream(0), System::NotSupportedException);
    std::vector<uint8_t> rgba;
    double pts = 0.0;
    EXPECT_THROW(decoder.NextFrame(rgba, pts), System::NotSupportedException);
    std::vector<float> samples;
    EXPECT_THROW(decoder.DrainAudio(samples), System::NotSupportedException);
    EXPECT_EQ(CNA::Internal::Media::AudioDurationProbe::ProbeDurationMS(kFixture), 0);
}
#endif
