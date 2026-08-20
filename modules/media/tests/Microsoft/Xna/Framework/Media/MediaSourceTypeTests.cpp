// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaSourceType.hpp"

using Microsoft::Xna::Framework::Media::MediaSourceType;

// plans/plan_media.md MEDIA-29: confirmed-correct finding, no code change -- values match FNA exactly.
TEST(MediaSourceTypeTest, ValuesMatchFnaOrder)
{
    EXPECT_EQ(static_cast<int>(MediaSourceType::LocalDevice), 0);
    EXPECT_EQ(static_cast<int>(MediaSourceType::WindowsMediaConnect), 4);
}
