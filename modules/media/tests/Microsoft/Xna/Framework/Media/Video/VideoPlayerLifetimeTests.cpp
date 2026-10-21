// SPDX-License-Identifier: MS-PL
// ISSUE-099: actual decoder/Video lifetimes, separate from the managed guard seam.
#include <gtest/gtest.h>
#include <memory>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using namespace Microsoft::Xna::Framework::Media;

    class VideoPlayerLifetimeTest : public testing::Test
    {
    protected:
        std::unique_ptr<Video> MakeVideo()
        {
            return std::make_unique<Video>("tests/assets/media/video/chroma_420.mkv", &device);
        }

        void Play(VideoPlayer& player, Video& video)
        {
            player.Play(&video);
            ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
            ASSERT_EQ(player.getVideoProperty(), &video);
            auto* texture = player.GetTexture();
            ASSERT_NE(texture, nullptr);
            EXPECT_EQ(texture->getWidthProperty(), 160);
            EXPECT_EQ(texture->getHeightProperty(), 90);
        }

        GraphicsDevice device;
    };

    TEST_F(VideoPlayerLifetimeTest, StopRetainsIdentityUntilVideoDestruction)
    {
        VideoPlayer player;
        auto video = MakeVideo();
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        player.Stop();
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
        EXPECT_EQ(player.getVideoProperty(), video.get());
        video.reset();
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    }

    TEST_F(VideoPlayerLifetimeTest, DisposeRetainsIdentityUntilVideoDestruction)
    {
        VideoPlayer player;
        auto video = MakeVideo();
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        player.Dispose();
        EXPECT_TRUE(player.getIsDisposedProperty());
        EXPECT_EQ(player.getVideoProperty(), video.get());
        video.reset();
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_NO_THROW(player.Dispose());
    }

    TEST_F(VideoPlayerLifetimeTest, DestroyingRetainedPlayerFirstDetachesItsVideo)
    {
        auto video = MakeVideo();
        {
            VideoPlayer player;
            ASSERT_NO_FATAL_FAILURE(Play(player, *video));
            player.Dispose();
            EXPECT_EQ(player.getVideoProperty(), video.get());
        }
        // This destruction must not call back into a dead player (also run under ASan).
        video.reset();
    }

    TEST_F(VideoPlayerLifetimeTest, DestroyingReplacedVideoDoesNotStopCurrentPlayback)
    {
        VideoPlayer player;
        auto first = MakeVideo();
        auto second = MakeVideo();
        ASSERT_NO_FATAL_FAILURE(Play(player, *first));
        ASSERT_NO_FATAL_FAILURE(Play(player, *second));
        first.reset();
        EXPECT_EQ(player.getVideoProperty(), second.get());
        EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
        // Do not dereference the frame if the ownership assertions already failed.
        if (player.getVideoProperty() == second.get())
        {
            EXPECT_NE(player.GetTexture(), nullptr);
        }
    }

    TEST_F(VideoPlayerLifetimeTest, DestroyingActiveVideoStillStopsItsPlayer)
    {
        VideoPlayer player;
        auto video = MakeVideo();
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        video.reset();
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    }

    TEST_F(VideoPlayerLifetimeTest, StoppedVideoCanBePlayedAgain)
    {
        VideoPlayer player;
        auto video = MakeVideo();
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        player.Stop();
        ASSERT_NO_FATAL_FAILURE(Play(player, *video));
        video.reset();
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    }
}
