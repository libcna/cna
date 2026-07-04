// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp"

using namespace Microsoft::Xna::Framework::Net;

// NetworkGamer is not yet ported (Task 4.4+); nullptr stands in for the pointer, matching the
// earlier precedent set for SignedInGamer* in GamerServicesEventArgsTests.cpp before that type existed.

TEST(GameEndedEventArgsTest, InheritsEventArgs) {
    GameEndedEventArgs args;
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(GameStartedEventArgsTest, InheritsEventArgs) {
    GameStartedEventArgs args;
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(GamerJoinedEventArgsTest, StoresGamer) {
    GamerJoinedEventArgs args(nullptr);
    EXPECT_EQ(nullptr, args.getGamerProperty());
}

TEST(GamerJoinedEventArgsTest, InheritsEventArgs) {
    GamerJoinedEventArgs args(nullptr);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(GamerLeftEventArgsTest, StoresGamer) {
    GamerLeftEventArgs args(nullptr);
    EXPECT_EQ(nullptr, args.getGamerProperty());
}

TEST(GamerLeftEventArgsTest, InheritsEventArgs) {
    GamerLeftEventArgs args(nullptr);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(HostChangedEventArgsTest, StoresOldAndNewHost) {
    HostChangedEventArgs args(nullptr, nullptr);
    EXPECT_EQ(nullptr, args.getOldHostProperty());
    EXPECT_EQ(nullptr, args.getNewHostProperty());
}

TEST(HostChangedEventArgsTest, InheritsEventArgs) {
    HostChangedEventArgs args(nullptr, nullptr);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(NetworkSessionEndedEventArgsTest, StoresEndReason) {
    NetworkSessionEndedEventArgs args(NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(NetworkSessionEndReason::HostEndedSession, args.getEndReasonProperty());
}

TEST(NetworkSessionEndedEventArgsTest, InheritsEventArgs) {
    NetworkSessionEndedEventArgs args(NetworkSessionEndReason::Disconnected);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(WriteLeaderboardsEventArgsTest, StoresGamerAndFlag) {
    auto args = WriteLeaderboardsEventArgs::CreateInternal(nullptr, true);
    EXPECT_EQ(nullptr, args.getGamerProperty());
    EXPECT_TRUE(args.getIsLeavingProperty());
}

TEST(WriteLeaderboardsEventArgsTest, FlagFalse) {
    auto args = WriteLeaderboardsEventArgs::CreateInternal(nullptr, false);
    EXPECT_FALSE(args.getIsLeavingProperty());
}

TEST(WriteLeaderboardsEventArgsTest, InheritsEventArgs) {
    auto args = WriteLeaderboardsEventArgs::CreateInternal(nullptr, false);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}
