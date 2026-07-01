// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
    SignedInGamer MakeSignedInGamer() {
        return SignedInGamer::CreateInternal("tag1");
    }
}

TEST(SignedInEventArgsTest, StoresGamer) {
    auto gamer = MakeSignedInGamer();
    SignedInEventArgs args(&gamer);
    EXPECT_EQ(&gamer, args.getGamerProperty());
}

TEST(SignedInEventArgsTest, InheritsEventArgs) {
    auto gamer = MakeSignedInGamer();
    SignedInEventArgs args(&gamer);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(SignedOutEventArgsTest, StoresGamer) {
    auto gamer = MakeSignedInGamer();
    SignedOutEventArgs args(&gamer);
    EXPECT_EQ(&gamer, args.getGamerProperty());
}

TEST(SignedOutEventArgsTest, InheritsEventArgs) {
    auto gamer = MakeSignedInGamer();
    SignedOutEventArgs args(&gamer);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(InviteAcceptedEventArgsTest, StoresGamerAndFlag) {
    auto gamer = MakeSignedInGamer();
    InviteAcceptedEventArgs args(&gamer, true);
    EXPECT_EQ(&gamer, args.getGamerProperty());
    EXPECT_TRUE(args.getIsCurrentSessionProperty());
}

TEST(InviteAcceptedEventArgsTest, FlagFalse) {
    auto gamer = MakeSignedInGamer();
    InviteAcceptedEventArgs args(&gamer, false);
    EXPECT_FALSE(args.getIsCurrentSessionProperty());
}

TEST(InviteAcceptedEventArgsTest, InheritsEventArgs) {
    auto gamer = MakeSignedInGamer();
    InviteAcceptedEventArgs args(&gamer, false);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}
