// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"

using Microsoft::Xna::Framework::FrameworkDispatcher;

TEST(FrameworkDispatcherTest, UpdateDoesNotThrowWithNoStreams)
{
    FrameworkDispatcher::Streams.clear();
    FrameworkDispatcher::ActiveSongChanged = false;
    FrameworkDispatcher::MediaStateChanged = false;
    EXPECT_NO_THROW(FrameworkDispatcher::Update());
}

TEST(FrameworkDispatcherTest, InitialStateIsEmpty)
{
    EXPECT_FALSE(FrameworkDispatcher::ActiveSongChanged);
    EXPECT_FALSE(FrameworkDispatcher::MediaStateChanged);
}
