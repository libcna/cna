// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/DateTime.hpp"
#include "System/IndexOutOfRangeException.hpp"

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

// Task 7.9: FNA's own string-key indexer explicitly does `throw new IndexOutOfRangeException();`
// - not std::out_of_range.
TEST(AchievementCollectionTest, IndexByKeyNotFound) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_THROW(col["missing"], System::IndexOutOfRangeException);
}

// Task 7.9: FNA's own int indexer (List<T>) throws ArgumentOutOfRangeException, not
// std::out_of_range.
TEST(AchievementCollectionTest, IndexByIntOutOfRangeThrowsArgumentOutOfRangeException) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_THROW((void) col[0], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void) col[-1], System::ArgumentOutOfRangeException);
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

// Task 7.12: FriendCollection (like GamerCollection<T> in general) is a non-owning view -
// Dispose() must never attempt to delete FriendGamer pointers it doesn't own, matching FNA's own
// real FriendCollection.Dispose() (collection.Clear() alone, relying on .NET's GC). Confirmed by
// constructing a real, caller-owned FriendGamer and proving it survives Dispose() intact and is
// still safely deletable by the caller afterward - if Dispose() had wrongly freed it, this would
// be a use-after-free/double-free.
TEST(FriendCollectionTest, DisposeDoesNotOwnOrFreeFriendGamerPointers) {
    auto* fg = new FriendGamer(FriendGamer::CreateInternal(
        "tag1", "Display1", false, false, false, false, false, false
    ));
    auto col = FriendCollection::CreateInternal({fg});
    ASSERT_EQ(1, col.getCountProperty());

    col.Dispose();
    EXPECT_TRUE(col.getIsDisposedProperty());
    EXPECT_EQ("tag1", fg->getGamertagProperty()); // still valid, not freed by Dispose()

    delete fg; // caller-owned; safe only because Dispose() never touched it
}

// --- SignedInGamerCollection ---

TEST(SignedInGamerCollectionTest, EmptyCollection) {
    auto col = SignedInGamerCollection::CreateInternal({});
    EXPECT_EQ(0, col.getCountProperty());
}

// Task 7.9: the base GamerCollection<T>::operator[](int) (inherited here from
// SignedInGamerCollection) used std::vector::at(), throwing std::out_of_range - FNA's own
// ReadOnlyCollection<T> -> List<T> int indexer throws ArgumentOutOfRangeException instead.
TEST(SignedInGamerCollectionTest, IntIndexOutOfRangeThrowsArgumentOutOfRangeException) {
    auto col = SignedInGamerCollection::CreateInternal({});
    EXPECT_THROW((void) col[0], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void) col[-1], System::ArgumentOutOfRangeException);
}

TEST(SignedInGamerCollectionTest, PlayerIndexOutOfBounds) {
    auto col = SignedInGamerCollection::CreateInternal({});
    EXPECT_EQ(nullptr, col[PlayerIndex::One]);
    EXPECT_EQ(nullptr, col[PlayerIndex::Four]);
}

// --- GamerCollection<T>::GamerCollectionEnumerator (Task 7.8) ---
//
// Raw std::vector::operator[] on an unvalidated position was real undefined behavior for
// position == -1 (the pre-MoveNext() starting value, casting to a huge std::size_t) or past the
// end of the collection. FNA's own equivalent (`collection[position]`, via
// ReadOnlyCollection<T>'s indexer -> List<T>'s own indexer) throws a catchable
// ArgumentOutOfRangeException in both cases instead. Exercised through SignedInGamerCollection,
// a concrete GamerCollection<T> subclass.

TEST(GamerCollectionEnumeratorTest, GetCurrentBeforeFirstMoveNextThrows) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto col = SignedInGamerCollection::CreateInternal({&gamer});
    auto it = col.GetEnumerator();
    EXPECT_THROW((void) it.getCurrent(), System::ArgumentOutOfRangeException);
}

TEST(GamerCollectionEnumeratorTest, GetCurrentPastTheEndThrows) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto col = SignedInGamerCollection::CreateInternal({&gamer});
    auto it = col.GetEnumerator();
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&gamer, it.getCurrent());
    EXPECT_FALSE(it.MoveNext()); // advances past the single element
    EXPECT_THROW((void) it.getCurrent(), System::ArgumentOutOfRangeException);
}

TEST(GamerCollectionEnumeratorTest, GetCurrentAfterDisposeThrows) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto col = SignedInGamerCollection::CreateInternal({&gamer});
    auto it = col.GetEnumerator();
    ASSERT_TRUE(it.MoveNext());
    it.Dispose();
    EXPECT_THROW((void) it.getCurrent(), System::ArgumentOutOfRangeException);
}

TEST(GamerCollectionEnumeratorTest, MoveNextAndGetCurrentEnumerateInOrder) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto col = SignedInGamerCollection::CreateInternal({&gamerA, &gamerB});
    auto it = col.GetEnumerator();
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&gamerA, it.getCurrent());
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&gamerB, it.getCurrent());
    EXPECT_FALSE(it.MoveNext());
}
