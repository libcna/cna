// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;

TEST(AudioChannelsTest, MonoIsOne)
{
    EXPECT_EQ(static_cast<int>(AudioChannels::Mono), 1);
}

TEST(AudioChannelsTest, StereoIsTwo)
{
    EXPECT_EQ(static_cast<int>(AudioChannels::Stereo), 2);
}

TEST(AudioChannelsTest, ValuesAreDistinct)
{
    EXPECT_NE(AudioChannels::Mono, AudioChannels::Stereo);
}
