// SPDX-License-Identifier: MS-PL
// ISSUE-099: retain the existing C++-only destruction contract in pause_and_loop.
#include <gtest/gtest.h>
#include <memory>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using namespace Microsoft::Xna::Framework::Media;

    TEST(VideoDestroyedFrameTest, DestroyingVideoRetainsTheExistingNoFrameContract)
    {
        GraphicsDevice device;
        VideoPlayer player;
        EXPECT_THROW((void)player.GetTexture(), System::InvalidOperationException);
        auto video = std::make_unique<Video>("tests/assets/media/video/chroma_420.mkv", &device);
        player.Play(video.get());
        ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
        ASSERT_NE(player.GetTexture(), nullptr);
        video.reset();
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_EQ(player.getPlayPositionProperty(), System::TimeSpan::Zero);
        // Same conjunction as the pre-existing demo; not a new XNA expectation.
        EXPECT_EQ(player.GetTexture(), nullptr);

        // A fresh playback clears the detached state, and a failed replacement must not
        // inherit the C++ destruction exception to the ordinary no-video guard.
        Video replay("tests/assets/media/video/chroma_420.mkv", &device);
        player.Play(&replay);
        ASSERT_NE(player.GetTexture(), nullptr);
        Video mismatch("tests/assets/media/video/chroma_420.mkv", &device,
                       2000, 1, 1, 25.0f, VideoSoundtrackType::MusicAndDialog);
        EXPECT_THROW(player.Play(&mismatch), System::InvalidOperationException);
        EXPECT_EQ(player.getVideoProperty(), nullptr);
        EXPECT_THROW((void)player.GetTexture(), System::InvalidOperationException);
        player.Dispose();
        EXPECT_THROW((void)player.GetTexture(), System::ObjectDisposedException);
    }
}
