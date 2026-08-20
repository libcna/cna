// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"

using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

// plans/plan_media.md MEDIA-31: confirmed-correct finding, no code change -- values match FNA exactly.
TEST(VideoSoundtrackTypeTest, ValuesMatchFnaOrder)
{
    EXPECT_EQ(static_cast<int>(VideoSoundtrackType::Music), 0);
    EXPECT_EQ(static_cast<int>(VideoSoundtrackType::Dialog), 1);
    EXPECT_EQ(static_cast<int>(VideoSoundtrackType::MusicAndDialog), 2);
}
