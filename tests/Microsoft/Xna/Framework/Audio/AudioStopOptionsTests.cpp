// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"

using Microsoft::Xna::Framework::Audio::AudioStopOptions;

TEST(AudioStopOptionsTest, AsAuthoredIsZero)
{
    EXPECT_EQ(static_cast<int>(AudioStopOptions::AsAuthored), 0);
}

TEST(AudioStopOptionsTest, ImmediateIsOne)
{
    EXPECT_EQ(static_cast<int>(AudioStopOptions::Immediate), 1);
}

TEST(AudioStopOptionsTest, ValuesAreDistinct)
{
    EXPECT_NE(AudioStopOptions::AsAuthored, AudioStopOptions::Immediate);
}
