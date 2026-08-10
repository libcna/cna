// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"

using Microsoft::Xna::Framework::Audio::SoundState;

TEST(SoundStateTest, PlayingIsZero)
{
    EXPECT_EQ(static_cast<int>(SoundState::Playing), 0);
}

TEST(SoundStateTest, PausedIsOne)
{
    EXPECT_EQ(static_cast<int>(SoundState::Paused), 1);
}

TEST(SoundStateTest, StoppedIsTwo)
{
    EXPECT_EQ(static_cast<int>(SoundState::Stopped), 2);
}

TEST(SoundStateTest, ValuesAreDistinct)
{
    EXPECT_NE(SoundState::Playing, SoundState::Paused);
    EXPECT_NE(SoundState::Paused, SoundState::Stopped);
    EXPECT_NE(SoundState::Playing, SoundState::Stopped);
}
