// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaState.hpp"

using Microsoft::Xna::Framework::Media::MediaState;

// plans/plan_media.md MEDIA-1/MEDIA-30: seeds tests/Microsoft/Xna/Framework/Media/ so the existing
// GLOB_RECURSE test discovery picks up the namespace with no CMakeLists.txt change. Extended into
// the full enum-value/order suite by MEDIA-85 in Phase 6.
TEST(MediaStateTest, ValuesMatchFnaOrder)
{
    EXPECT_EQ(static_cast<int>(MediaState::Stopped), 0);
    EXPECT_EQ(static_cast<int>(MediaState::Playing), 1);
    EXPECT_EQ(static_cast<int>(MediaState::Paused), 2);
}
