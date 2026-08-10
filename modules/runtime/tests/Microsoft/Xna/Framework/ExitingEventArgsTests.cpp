// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/ExitingEventArgs.hpp"
#include "System/EventArgs.hpp"

using Microsoft::Xna::Framework::ExitingEventArgs;

TEST(ExitingEventArgsTest, DefaultConstructible)
{
    ExitingEventArgs args;
    (void)args;
}

TEST(ExitingEventArgsTest, IsInstanceOfEventArgs)
{
    ExitingEventArgs args;
    System::EventArgs* base = &args;
    EXPECT_NE(base, nullptr);
}
