// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp"

using namespace Microsoft::Xna::Framework::Net;

// Task 5.6: every gamer-carrying EventArgs test below used to pass nullptr for every gamer
// parameter (dating back to before NetworkGamer was ported) - which can't catch a constructor
// argument-order bug (e.g. HostChangedEventArgs accidentally swapping oldHost/newHost - both
// would silently still read back as nullptr either way). Two distinct, non-null sentinel
// NetworkGamer instances (a nullptr NetworkSession* is fine - the constructor never dereferences
// it) let each test assert the *correct*, distinguishable one comes back from each property.
namespace {
    NetworkGamer MakeSentinelGamer(const std::string& gamertag) {
        return NetworkGamer::CreateInternal(nullptr, gamertag);
    }
}

TEST(GameEndedEventArgsTest, InheritsEventArgs) {
    GameEndedEventArgs args;
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(GameStartedEventArgsTest, InheritsEventArgs) {
    GameStartedEventArgs args;
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(GamerJoinedEventArgsTest, StoresGamer) {
    auto gamer = MakeSentinelGamer("Gamer");
    GamerJoinedEventArgs args(&gamer);
    EXPECT_EQ(&gamer, args.getGamerProperty());
}

TEST(GamerJoinedEventArgsTest, InheritsEventArgs) {
    auto gamer = MakeSentinelGamer("Gamer");
    GamerJoinedEventArgs args(&gamer);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(GamerLeftEventArgsTest, StoresGamer) {
    auto gamer = MakeSentinelGamer("Gamer");
    GamerLeftEventArgs args(&gamer);
    EXPECT_EQ(&gamer, args.getGamerProperty());
}

TEST(GamerLeftEventArgsTest, InheritsEventArgs) {
    auto gamer = MakeSentinelGamer("Gamer");
    GamerLeftEventArgs args(&gamer);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}

TEST(HostChangedEventArgsTest, StoresOldAndNewHost) {
    auto oldHost = MakeSentinelGamer("OldHost");
    auto newHost = MakeSentinelGamer("NewHost");
    HostChangedEventArgs args(&oldHost, &newHost);
    // Two distinct sentinels catch a constructor argument-order bug that nullptr/nullptr never
    // could - if oldHost/newHost were accidentally swapped, these assertions would fail.
    EXPECT_EQ(&oldHost, args.getOldHostProperty());
    EXPECT_EQ(&newHost, args.getNewHostProperty());
    EXPECT_NE(args.getOldHostProperty(), args.getNewHostProperty());
}

TEST(HostChangedEventArgsTest, InheritsEventArgs) {
    auto oldHost = MakeSentinelGamer("OldHost");
    auto newHost = MakeSentinelGamer("NewHost");
    HostChangedEventArgs args(&oldHost, &newHost);
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
    auto gamer = MakeSentinelGamer("Gamer");
    auto args = WriteLeaderboardsEventArgs::CreateInternal(&gamer, true);
    EXPECT_EQ(&gamer, args.getGamerProperty());
    EXPECT_TRUE(args.getIsLeavingProperty());
}

TEST(WriteLeaderboardsEventArgsTest, FlagFalse) {
    auto gamer = MakeSentinelGamer("Gamer");
    auto args = WriteLeaderboardsEventArgs::CreateInternal(&gamer, false);
    EXPECT_FALSE(args.getIsLeavingProperty());
}

TEST(WriteLeaderboardsEventArgsTest, InheritsEventArgs) {
    auto gamer = MakeSentinelGamer("Gamer");
    auto args = WriteLeaderboardsEventArgs::CreateInternal(&gamer, false);
    EXPECT_NE(nullptr, dynamic_cast<System::EventArgs*>(&args));
}
