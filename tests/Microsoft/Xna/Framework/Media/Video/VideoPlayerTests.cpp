// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"

using Microsoft::Xna::Framework::Media::MediaState;
using Microsoft::Xna::Framework::Media::VideoPlayer;

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
