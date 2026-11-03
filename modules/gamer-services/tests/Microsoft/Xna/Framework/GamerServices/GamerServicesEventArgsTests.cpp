// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "System/ArgumentNullException.hpp"

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

TEST(SignedInEventArgsTest, NullGamerIsRejectedWithItsParameterName) {
    // XNA SignedInEventArgs::.ctor IL_0006..0013 rejects null before storing the gamer.
    try {
        SignedInEventArgs args(nullptr);
        FAIL() << "Expected ArgumentNullException";
    } catch (const System::ArgumentNullException& error) {
        EXPECT_EQ("gamer", error.getParamNameProperty());
    }
}

TEST(SignedOutEventArgsTest, NullGamerIsRejectedWithItsParameterName) {
    // XNA SignedOutEventArgs::.ctor IL_0006..0013 has the same null guard.
    try {
        SignedOutEventArgs args(nullptr);
        FAIL() << "Expected ArgumentNullException";
    } catch (const System::ArgumentNullException& error) {
        EXPECT_EQ("gamer", error.getParamNameProperty());
    }
}

TEST(InviteAcceptedEventArgsTest, NullGamerIsStoredAndTheSessionFlagIsPreserved) {
    // XNA InviteAcceptedEventArgs::.ctor IL_0006..0014 deliberately has no null guard.
    for (bool current : {false, true}) {
        const InviteAcceptedEventArgs args(nullptr, current);
        EXPECT_EQ(nullptr, args.getGamerProperty());
        EXPECT_EQ(current, args.getIsCurrentSessionProperty());
    }
}
