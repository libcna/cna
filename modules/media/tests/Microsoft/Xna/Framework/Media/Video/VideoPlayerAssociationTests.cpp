// SPDX-License-Identifier: MS-PL
// ISSUE-099: retained XNA identity must respect independent C++ object lifetimes.
#include <gtest/gtest.h>
#include <memory>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "System/InvalidOperationException.hpp"
#include "VideoPlayerTestAccess.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using namespace Microsoft::Xna::Framework::Media;
    constexpr const char* kVideo = "tests/assets/media/video/chroma_420.mkv";

    class VideoPlayerAssociationTest : public testing::Test
    {
    protected:
        std::unique_ptr<Video> MakeVideo() { return std::make_unique<Video>(kVideo, &device); }
        void Play(VideoPlayer& player, Video& video)
        {
            player.Play(&video);
            ASSERT_EQ(player.getVideoProperty(), &video);
            ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
            ASSERT_NE(player.GetTexture(), nullptr);
        }
        GraphicsDevice device;
    };

    TEST_F(VideoPlayerAssociationTest, CopyConstructionDoesNotCopyPlayerAssociations)
    {
        auto video = MakeVideo();
        VideoPlayer player;
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        {
            Video copied(*video);
            EXPECT_EQ(copied.getFileNameProperty(), video->getFileNameProperty());
            EXPECT_EQ(copied.getWidthProperty(), video->getWidthProperty());
        }
        EXPECT_EQ(player.getVideoProperty(), video.get());
        EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
    }

    TEST_F(VideoPlayerAssociationTest, CopyAssignmentDoesNotSubscribeToTheSourcePlayer)
    {
        auto video = MakeVideo();
        auto copied = MakeVideo();
        VideoPlayer player;
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        *copied = *video;
        copied.reset();
        EXPECT_EQ(player.getVideoProperty(), video.get());
        EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
    }

    TEST_F(VideoPlayerAssociationTest, CopyAssignmentPreservesDestinationObservers)
    {
        auto video = MakeVideo();
        auto metadata = MakeVideo();
        VideoPlayer player;
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        *video = *metadata;
        video.reset();
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    }

    TEST_F(VideoPlayerAssociationTest, SelfAssignmentRetainsPlayback)
    {
        auto video = MakeVideo();
        VideoPlayer player;
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        const Video& alias = *video;
        *video = alias;
        EXPECT_EQ(player.getVideoProperty(), video.get());
        EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
    }

    TEST_F(VideoPlayerAssociationTest, DestroyingVideoDetachesStoppedAndActivePlayers)
    {
        auto video = MakeVideo();
        VideoPlayer first, second;
        ASSERT_NO_FATAL_FAILURE(Play(first, *video));
        ASSERT_NO_FATAL_FAILURE(Play(second, *video));
        first.Stop();
        EXPECT_EQ(first.getVideoProperty(), video.get());
        video.reset();
        EXPECT_EQ(first.getVideoProperty(), nullptr);
        EXPECT_EQ(second.getVideoProperty(), nullptr);
        EXPECT_EQ(first.getStateProperty(), MediaState::Stopped);
        EXPECT_EQ(second.getStateProperty(), MediaState::Stopped);
    }

    TEST_F(VideoPlayerAssociationTest, DestroyingVideoDetachesDisposedAndActivePlayers)
    {
        auto video = MakeVideo();
        VideoPlayer first, second;
        ASSERT_NO_FATAL_FAILURE(Play(first, *video));
        ASSERT_NO_FATAL_FAILURE(Play(second, *video));
        first.Dispose();
        EXPECT_EQ(first.getVideoProperty(), video.get());
        video.reset();
        EXPECT_EQ(first.getVideoProperty(), nullptr);
        EXPECT_EQ(second.getVideoProperty(), nullptr);
        EXPECT_TRUE(first.getIsDisposedProperty());
        EXPECT_EQ(second.getStateProperty(), MediaState::Stopped);
    }

    TEST_F(VideoPlayerAssociationTest, DestroyingOneRetainedPlayerPreservesTheOtherAssociation)
    {
        auto video = MakeVideo();
        auto first = std::make_unique<VideoPlayer>();
        VideoPlayer second;
        ASSERT_NO_FATAL_FAILURE(Play(*first, *video));
        ASSERT_NO_FATAL_FAILURE(Play(second, *video));
        first->Dispose();
        first.reset();
        EXPECT_EQ(second.getVideoProperty(), video.get());
        EXPECT_EQ(second.getStateProperty(), MediaState::Playing);
        video.reset();
        EXPECT_EQ(second.getVideoProperty(), nullptr);
        EXPECT_EQ(second.getStateProperty(), MediaState::Stopped);
    }

    TEST_F(VideoPlayerAssociationTest, StopRetainsTheFrameButDisposeReleasesDecoderAndTexture)
    {
        auto video = MakeVideo();
        VideoPlayer player;
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        auto* frame = player.GetTexture();
        const auto generation = player.GetFrameGenerationEXT();
        player.Stop();
        EXPECT_EQ(player.GetTexture(), frame);
        EXPECT_EQ(player.GetFrameGenerationEXT(), generation);
        EXPECT_TRUE(VideoPlayerTestAccess::HasFrameTexture(player));
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderWidth(player), 160);
        EXPECT_EQ(player.getPlayPositionProperty(), System::TimeSpan::Zero);
        player.Dispose();
        EXPECT_FALSE(VideoPlayerTestAccess::HasFrameTexture(player));
        EXPECT_EQ(VideoPlayerTestAccess::GetDecoderWidth(player), -1);
        EXPECT_EQ(VideoPlayerTestAccess::GetAudioStreamPtr(player), nullptr);
        EXPECT_EQ(player.getVideoProperty(), video.get());
    }

    TEST_F(VideoPlayerAssociationTest, FailedReplacementDetachesThePreviousVideo)
    {
        auto first = MakeVideo();
        auto next = MakeVideo();
        VideoPlayer player;
        ASSERT_NO_FATAL_FAILURE(Play(player, *first));
        Video mismatch(kVideo, &device, 2000, 1, 1, 25.0f, VideoSoundtrackType::MusicAndDialog);
        EXPECT_THROW(player.Play(&mismatch), System::InvalidOperationException);
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        ASSERT_NO_FATAL_FAILURE(Play(player, *next));
        first.reset();
        EXPECT_EQ(player.getVideoProperty(), next.get());
        EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
    }
}
