// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

// SignedInGamer is not yet ported; use nullptr as a stand-in for the pointer.

TEST(SignedInEventArgsTest, StoresGamer) {
    SignedInEventArgs args(nullptr);
    EXPECT_EQ(nullptr, args.getGamerProperty());
}

TEST(SignedInEventArgsTest, InheritsEventArgs) {
    SignedInEventArgs args(nullptr);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(SignedOutEventArgsTest, StoresGamer) {
    SignedOutEventArgs args(nullptr);
    EXPECT_EQ(nullptr, args.getGamerProperty());
}

TEST(SignedOutEventArgsTest, InheritsEventArgs) {
    SignedOutEventArgs args(nullptr);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(InviteAcceptedEventArgsTest, StoresGamerAndFlag) {
    InviteAcceptedEventArgs args(nullptr, true);
    EXPECT_EQ(nullptr, args.getGamerProperty());
    EXPECT_TRUE(args.getIsCurrentSessionProperty());
}

TEST(InviteAcceptedEventArgsTest, FlagFalse) {
    InviteAcceptedEventArgs args(nullptr, false);
    EXPECT_FALSE(args.getIsCurrentSessionProperty());
}

TEST(InviteAcceptedEventArgsTest, InheritsEventArgs) {
    InviteAcceptedEventArgs args(nullptr, false);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}
