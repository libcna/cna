// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/DateTime.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework;

// --- AchievementCollection ---

TEST(AchievementCollectionTest, EmptyCollection) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_EQ(0, col.getCountProperty());
    EXPECT_FALSE(col.getIsDisposedProperty());
}

TEST(AchievementCollectionTest, IndexByInt) {
    System::DateTime dt;
    std::vector<Achievement> v;
    v.push_back(Achievement::CreateInternal("k1", "Name1", "Desc", false, false, dt));
    auto col = AchievementCollection::CreateInternal(std::move(v));
    EXPECT_EQ(1, col.getCountProperty());
    EXPECT_EQ("k1", col[0].getKeyProperty());
}

TEST(AchievementCollectionTest, IndexByKey) {
    System::DateTime dt;
    std::vector<Achievement> v;
    v.push_back(Achievement::CreateInternal("ach1", "N", "D", true, true, dt));
    auto col = AchievementCollection::CreateInternal(std::move(v));
    EXPECT_EQ("ach1", col["ach1"].getKeyProperty());
}

TEST(AchievementCollectionTest, IndexByKeyNotFound) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_THROW(col["missing"], std::out_of_range);
}

TEST(AchievementCollectionTest, Dispose) {
    auto col = AchievementCollection::CreateInternal({});
    col.Dispose();
    EXPECT_TRUE(col.getIsDisposedProperty());
    col.Dispose(); // idempotent
    EXPECT_TRUE(col.getIsDisposedProperty());
}

TEST(AchievementCollectionTest, RangeFor) {
    System::DateTime dt;
    std::vector<Achievement> v;
    v.push_back(Achievement::CreateInternal("a", "A", "D", false, false, dt));
    v.push_back(Achievement::CreateInternal("b", "B", "D", false, false, dt));
    auto col = AchievementCollection::CreateInternal(std::move(v));
    int count = 0;
    for (const auto& a : col) { (void)a; ++count; }
    EXPECT_EQ(2, count);
}

// --- FriendGamer ---

TEST(FriendGamerTest, BasicProperties) {
    auto fg = FriendGamer::CreateInternal("tag1", "Display1", true, false, false, false, false, false);
    EXPECT_EQ("tag1",     fg.getGamertagProperty());
    EXPECT_EQ("Display1", fg.getDisplayNameProperty());
    EXPECT_TRUE(fg.getIsOnlineProperty());
    EXPECT_FALSE(fg.getIsPlayingProperty());
    EXPECT_FALSE(fg.getIsAwayProperty());
    EXPECT_FALSE(fg.getIsBusyProperty());
}

TEST(FriendGamerTest, RequestFlags) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, true, false);
    EXPECT_TRUE(fg.getFriendRequestSentToProperty());
    EXPECT_FALSE(fg.getFriendRequestReceivedFromProperty());
}

TEST(FriendGamerTest, DefaultStubFlags) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, false, false);
    EXPECT_FALSE(fg.getIsJoinableProperty());
    EXPECT_FALSE(fg.getHasVoiceProperty());
    EXPECT_FALSE(fg.getInviteAcceptedProperty());
    EXPECT_EQ("", fg.getPresenceProperty());
}

TEST(FriendGamerTest, InheritsGamer) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, false, false);
    EXPECT_NE(nullptr, dynamic_cast<Gamer*>(&fg));
}

// --- FriendCollection ---

TEST(FriendCollectionTest, EmptyCollection) {
    auto col = FriendCollection::CreateInternal({});
    EXPECT_EQ(0, col.getCountProperty());
    EXPECT_FALSE(col.getIsDisposedProperty());
}

TEST(FriendCollectionTest, Dispose) {
    auto col = FriendCollection::CreateInternal({});
    col.Dispose();
    EXPECT_TRUE(col.getIsDisposedProperty());
}

// --- SignedInGamerCollection ---

TEST(SignedInGamerCollectionTest, EmptyCollection) {
    auto col = SignedInGamerCollection::CreateInternal({});
    EXPECT_EQ(0, col.getCountProperty());
}

TEST(SignedInGamerCollectionTest, PlayerIndexOutOfBounds) {
    auto col = SignedInGamerCollection::CreateInternal({});
    EXPECT_EQ(nullptr, col[PlayerIndex::One]);
    EXPECT_EQ(nullptr, col[PlayerIndex::Four]);
}
