// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"

using Microsoft::Xna::Framework::Audio::MicrophoneState;

TEST(MicrophoneStateTest, StartedIsZero)
{
    EXPECT_EQ(static_cast<int>(MicrophoneState::Started), 0);
}

TEST(MicrophoneStateTest, StoppedIsOne)
{
    EXPECT_EQ(static_cast<int>(MicrophoneState::Stopped), 1);
}

TEST(MicrophoneStateTest, ValuesAreDistinct)
{
    EXPECT_NE(MicrophoneState::Started, MicrophoneState::Stopped);
}
