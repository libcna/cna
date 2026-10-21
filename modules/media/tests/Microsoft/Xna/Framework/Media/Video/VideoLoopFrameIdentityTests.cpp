// SPDX-License-Identifier: MS-PL
// ISSUE-099 C frame follow-up: looping may reset the decode cursor, not the held frame's timestamp.
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "VideoPlayerTestAccess.hpp"

namespace
{
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using namespace Microsoft::Xna::Framework::Media;

class VideoLoopFrameIdentityTest : public testing::Test
{
protected:
    GraphicsDevice device;
    Video video{"tests/assets/media/video/chroma_420.mkv", &device};
    VideoPlayer player;

    void SetUp() override
    {
        player.setIsLoopedProperty(true);
        player.Play(&video);
        ASSERT_NE(player.GetTexture(), nullptr);
        ASSERT_GT(player.GetFrameGenerationEXT(), 0U);
    }

    void ReachLoopBoundary()
    {
        const auto before = player.GetFrameGenerationEXT();
        // The authored fixture is exactly two seconds. Exercise actual decoder EOF/seek,
        // changing only the playback clock through the established native test friend.
        VideoPlayerTestAccess::SetElapsedSeconds(player, 3.0);
        ASSERT_NE(player.GetTexture(), nullptr);
        ASSERT_GT(player.GetFrameGenerationEXT(), before);
        ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
    }
};

TEST_F(VideoLoopFrameIdentityTest, LoopBoundaryRetainsTimestampForTheStillVisibleFrame)
{
    ASSERT_NO_FATAL_FAILURE(ReachLoopBoundary());
    EXPECT_GE(player.GetFramePresentationTimeEXT(), 0.0);
}

TEST_F(VideoLoopFrameIdentityTest, StopAtLoopBoundaryPreservesFrameIdentity)
{
    ASSERT_NO_FATAL_FAILURE(ReachLoopBoundary());
    const auto generation = player.GetFrameGenerationEXT();
    const auto pts = player.GetFramePresentationTimeEXT();
    player.Stop();
    auto* frame = player.GetTexture();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(player.GetTexture(), frame);
    EXPECT_EQ(player.GetFrameGenerationEXT(), generation);
    EXPECT_DOUBLE_EQ(player.GetFramePresentationTimeEXT(), pts);
    EXPECT_GE(player.GetFramePresentationTimeEXT(), 0.0);
}

TEST_F(VideoLoopFrameIdentityTest, StopBeforeLoopRetainsFrameIdentity)
{
    player.Pause();
    auto* frame = player.GetTexture();
    const auto generation = player.GetFrameGenerationEXT();
    const auto pts = player.GetFramePresentationTimeEXT();
    player.Stop();
    EXPECT_EQ(player.GetTexture(), frame);
    EXPECT_EQ(player.GetFrameGenerationEXT(), generation);
    EXPECT_DOUBLE_EQ(player.GetFramePresentationTimeEXT(), pts);
    EXPECT_GE(pts, 0.0);
}

TEST_F(VideoLoopFrameIdentityTest, DisposeClearsTimestampButDoesNotRestartGeneration)
{
    const auto generation = player.GetFrameGenerationEXT();
    player.Dispose();
    EXPECT_LT(player.GetFramePresentationTimeEXT(), 0.0);
    EXPECT_EQ(player.GetFrameGenerationEXT(), generation);
}
} // namespace
