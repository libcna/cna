// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/ArgumentException.hpp"
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

// Task 9.5: the empty-collection case above only ever exercises index == 0 == size(); confirm
// the boundary check also holds once the collection is actually populated (index == size() with
// count > 0, not just count == 0).
TEST(AchievementCollectionTest, IndexByIntOutOfRangeOnPopulatedCollectionThrowsArgumentOutOfRangeException) {
    System::DateTime dt;
    std::vector<Achievement> v;
    v.push_back(Achievement::CreateInternal("k1", "Name1", "Desc", false, false, dt));
    auto col = AchievementCollection::CreateInternal(std::move(v));
    EXPECT_THROW((void) col[1], System::ArgumentOutOfRangeException);
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

namespace {
    Achievement MakeAchievement(const std::string& key) {
        return Achievement::CreateInternal(key, "Name-" + key, "Desc", false, false, System::DateTime{});
    }
}

// Task 8.2: Achievement::operator== (structural equality, since by-value storage has no
// reference-identity equivalent for FNA's own real Achievement) is required by IndexOf/Contains/
// Remove below - currently zero coverage.
TEST(AchievementTest, EqualityIsStructural) {
    auto dt = System::DateTime{};
    auto a = Achievement::CreateInternal("k", "N", "D", false, true, dt);
    auto b = Achievement::CreateInternal("k", "N", "D", false, true, dt);
    auto c = Achievement::CreateInternal("different", "N", "D", false, true, dt);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(AchievementCollectionTest, IndexOfFindsAndReportsNotFound) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("b")};
    auto col = AchievementCollection::CreateInternal(v);
    EXPECT_EQ(0, col.IndexOf(v[0]));
    EXPECT_EQ(1, col.IndexOf(v[1]));
    EXPECT_EQ(-1, col.IndexOf(MakeAchievement("missing")));
}

TEST(AchievementCollectionTest, InsertAtIndexShiftsLaterElements) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("c")};
    auto col = AchievementCollection::CreateInternal(v);
    col.Insert(1, MakeAchievement("b"));
    ASSERT_EQ(3, col.getCountProperty());
    EXPECT_EQ("a", col[0].getKeyProperty());
    EXPECT_EQ("b", col[1].getKeyProperty());
    EXPECT_EQ("c", col[2].getKeyProperty());
}

TEST(AchievementCollectionTest, InsertAtCountAppends) {
    std::vector<Achievement> v{MakeAchievement("a")};
    auto col = AchievementCollection::CreateInternal(v);
    col.Insert(col.getCountProperty(), MakeAchievement("b"));
    ASSERT_EQ(2, col.getCountProperty());
    EXPECT_EQ("b", col[1].getKeyProperty());
}

TEST(AchievementCollectionTest, InsertOutOfRangeThrows) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_THROW(col.Insert(-1, MakeAchievement("a")), System::ArgumentOutOfRangeException);
    EXPECT_THROW(col.Insert(1, MakeAchievement("a")), System::ArgumentOutOfRangeException);
}

TEST(AchievementCollectionTest, RemoveAtDeletesTheRightElement) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("b"), MakeAchievement("c")};
    auto col = AchievementCollection::CreateInternal(v);
    col.RemoveAt(1);
    ASSERT_EQ(2, col.getCountProperty());
    EXPECT_EQ("a", col[0].getKeyProperty());
    EXPECT_EQ("c", col[1].getKeyProperty());
}

TEST(AchievementCollectionTest, RemoveAtOutOfRangeThrows) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_THROW(col.RemoveAt(-1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(col.RemoveAt(0), System::ArgumentOutOfRangeException);
}

TEST(AchievementCollectionTest, AddAppendsToTheEnd) {
    auto col = AchievementCollection::CreateInternal({});
    col.Add(MakeAchievement("a"));
    col.Add(MakeAchievement("b"));
    ASSERT_EQ(2, col.getCountProperty());
    EXPECT_EQ("a", col[0].getKeyProperty());
    EXPECT_EQ("b", col[1].getKeyProperty());
}

TEST(AchievementCollectionTest, RemoveDeletesMatchingElementAndReportsNotFound) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("b")};
    auto col = AchievementCollection::CreateInternal(v);
    EXPECT_TRUE(col.Remove(v[0]));
    ASSERT_EQ(1, col.getCountProperty());
    EXPECT_EQ("b", col[0].getKeyProperty());
    EXPECT_FALSE(col.Remove(MakeAchievement("missing")));
}

TEST(AchievementCollectionTest, ClearRemovesEverythingWithoutDisposing) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("b")};
    auto col = AchievementCollection::CreateInternal(v);
    col.Clear();
    EXPECT_EQ(0, col.getCountProperty());
    EXPECT_FALSE(col.getIsDisposedProperty());
}

TEST(AchievementCollectionTest, ContainsFindsAndReportsNotFound) {
    std::vector<Achievement> v{MakeAchievement("a")};
    auto col = AchievementCollection::CreateInternal(v);
    EXPECT_TRUE(col.Contains(v[0]));
    EXPECT_FALSE(col.Contains(MakeAchievement("missing")));
}

TEST(AchievementCollectionTest, CopyToCopiesStartingAtArrayIndex) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("b")};
    auto col = AchievementCollection::CreateInternal(v);
    std::vector<Achievement> dest(3, MakeAchievement("placeholder"));
    col.CopyTo(dest, 1);
    EXPECT_EQ("placeholder", dest[0].getKeyProperty());
    EXPECT_EQ("a", dest[1].getKeyProperty());
    EXPECT_EQ("b", dest[2].getKeyProperty());
}

TEST(AchievementCollectionTest, CopyToThrowsWhenDestinationTooSmall) {
    std::vector<Achievement> v{MakeAchievement("a"), MakeAchievement("b")};
    auto col = AchievementCollection::CreateInternal(v);
    std::vector<Achievement> dest(2, MakeAchievement("placeholder"));
    EXPECT_THROW(col.CopyTo(dest, 1), System::ArgumentException);
}

TEST(AchievementCollectionTest, CopyToThrowsForNegativeArrayIndex) {
    auto col = AchievementCollection::CreateInternal({});
    std::vector<Achievement> dest;
    EXPECT_THROW(col.CopyTo(dest, -1), System::ArgumentOutOfRangeException);
}

// Task 8.2: FNA's own hardcoded ICollection<Achievement>.IsReadOnly getter, despite
// Insert/RemoveAt/Add/Remove/Clear all being real, working mutators - the conventional .NET
// pattern for an explicit-interface-only mutable surface, not an inconsistency like Task 8.1's
// PropertyDictionary case (there is no upstream bug here).
TEST(AchievementCollectionTest, IsReadOnlyIsAlwaysTrue) {
    auto col = AchievementCollection::CreateInternal({});
    EXPECT_TRUE(col.getIsReadOnlyProperty());
    col.Add(MakeAchievement("a"));
    EXPECT_TRUE(col.getIsReadOnlyProperty());
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

// Task 9.2: InviteReceivedFrom/InviteRejected/InviteSentTo were never referenced by any test.
// Confirmed against FNA's own real internal FriendGamer(...) constructor: it hardcodes
// InviteReceivedFrom = false; InviteRejected = false; InviteSentTo = false; regardless of any
// constructor argument - a faithfully-preserved upstream stub, not a CNA gap, matching the same
// pattern already covered here for IsJoinable/HasVoice/InviteAccepted/Presence.
TEST(FriendGamerTest, DefaultStubFlags) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, false, false);
    EXPECT_FALSE(fg.getIsJoinableProperty());
    EXPECT_FALSE(fg.getHasVoiceProperty());
    EXPECT_FALSE(fg.getInviteAcceptedProperty());
    EXPECT_FALSE(fg.getInviteReceivedFromProperty());
    EXPECT_FALSE(fg.getInviteRejectedProperty());
    EXPECT_FALSE(fg.getInviteSentToProperty());
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

// Task 9.9: the case above only ever covers the empty collection (returns nullptr for any
// index). Confirmed against FNA's own real indexer (`return collection[(int)index];`, bounds-
// checked only against Count) that operator[](PlayerIndex) indexes the underlying collection
// directly by the enum's ordinal position - it is not a lookup by each gamer's own PlayerIndex
// property - so this only produces intuitive results when gamers happen to be stored in
// PlayerIndex order, exactly as GamerServicesDispatcher::Initialize() does.
TEST(SignedInGamerCollectionTest, PlayerIndexOperatorOnPopulatedCollection) {
    auto gamerOne   = SignedInGamer::CreateInternal("a", true, false, PlayerIndex::One);
    auto gamerTwo   = SignedInGamer::CreateInternal("b", true, false, PlayerIndex::Two);
    auto gamerThree = SignedInGamer::CreateInternal("c", true, false, PlayerIndex::Three);
    auto col = SignedInGamerCollection::CreateInternal({&gamerOne, &gamerTwo, &gamerThree});

    EXPECT_EQ(&gamerOne,   col[PlayerIndex::One]);
    EXPECT_EQ(&gamerTwo,   col[PlayerIndex::Two]);
    EXPECT_EQ(&gamerThree, col[PlayerIndex::Three]);

    // Boundary: index == size() returns nullptr, not an out-of-range exception (unlike the int
    // indexer's own ArgumentOutOfRangeException behavior above).
    EXPECT_EQ(nullptr, col[PlayerIndex::Four]);

    int count = 0;
    for (const auto& gamer : col) { (void) gamer; ++count; }
    EXPECT_EQ(3, count);
}

// Task 8.3: GamerCollection<T>'s new IndexOf/Contains/CopyTo, matching FNA's real
// GamerCollection<T> (which derives from ReadOnlyCollection<T> and inherits these). Compares by
// pointer identity - already an exact match for FNA's own reference-type equality semantics,
// since GamerCollection<T> stores T* and native pointer comparison already is reference-identity
// comparison (no operator== needed, unlike Achievement/LeaderboardEntry's own value-storage
// workaround). Exercised through SignedInGamerCollection, a concrete GamerCollection<T> subclass.

TEST(SignedInGamerCollectionTest, IndexOfFindsAndReportsNotFound) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto gamerC = SignedInGamer::CreateInternal("c"); // never added to the collection
    auto col = SignedInGamerCollection::CreateInternal({&gamerA, &gamerB});
    EXPECT_EQ(0, col.IndexOf(&gamerA));
    EXPECT_EQ(1, col.IndexOf(&gamerB));
    EXPECT_EQ(-1, col.IndexOf(&gamerC));
}

TEST(SignedInGamerCollectionTest, ContainsFindsAndReportsNotFound) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto col = SignedInGamerCollection::CreateInternal({&gamerA});
    EXPECT_TRUE(col.Contains(&gamerA));
    EXPECT_FALSE(col.Contains(&gamerB));
}

TEST(SignedInGamerCollectionTest, CopyToCopiesStartingAtIndex) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto placeholder = SignedInGamer::CreateInternal("placeholder");
    auto col = SignedInGamerCollection::CreateInternal({&gamerA, &gamerB});
    std::vector<SignedInGamer*> dest(3, &placeholder);
    col.CopyTo(dest, 1);
    EXPECT_EQ(&placeholder, dest[0]);
    EXPECT_EQ(&gamerA, dest[1]);
    EXPECT_EQ(&gamerB, dest[2]);
}

TEST(SignedInGamerCollectionTest, CopyToThrowsWhenDestinationTooSmall) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto placeholder = SignedInGamer::CreateInternal("placeholder");
    auto col = SignedInGamerCollection::CreateInternal({&gamerA, &gamerB});
    std::vector<SignedInGamer*> dest(2, &placeholder);
    EXPECT_THROW(col.CopyTo(dest, 1), System::ArgumentException);
}

TEST(SignedInGamerCollectionTest, CopyToThrowsForNegativeIndex) {
    auto col = SignedInGamerCollection::CreateInternal({});
    std::vector<SignedInGamer*> dest;
    EXPECT_THROW(col.CopyTo(dest, -1), System::ArgumentOutOfRangeException);
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

// audit_net.md Medium finding: MoveNext() previously dereferenced collection_ (via
// collection_->size()) unconditionally, with no guard matching getCurrent()'s own - Dispose()
// sets collection_ to nullptr, so it.Dispose(); it.MoveNext(); was an immediate null-pointer
// dereference. Confirms MoveNext() now throws the same catchable exception getCurrent() already
// did in this situation, both before and after any prior MoveNext() call.
TEST(GamerCollectionEnumeratorTest, MoveNextAfterDisposeThrowsInsteadOfDereferencingNull) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto col = SignedInGamerCollection::CreateInternal({&gamer});
    auto it = col.GetEnumerator();
    it.Dispose();
    EXPECT_THROW((void) it.MoveNext(), System::ArgumentOutOfRangeException);
}

TEST(GamerCollectionEnumeratorTest, MoveNextAfterMoveNextThenDisposeThrows) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto col = SignedInGamerCollection::CreateInternal({&gamer});
    auto it = col.GetEnumerator();
    ASSERT_TRUE(it.MoveNext());
    it.Dispose();
    EXPECT_THROW((void) it.MoveNext(), System::ArgumentOutOfRangeException);
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

// Task 9.3: Reset() had zero test coverage - confirms enumeration genuinely restarts from before
// the first element, not just that Reset() compiles.
TEST(GamerCollectionEnumeratorTest, ResetRestartsEnumeration) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto col = SignedInGamerCollection::CreateInternal({&gamerA, &gamerB});
    auto it = col.GetEnumerator();
    ASSERT_TRUE(it.MoveNext());
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&gamerB, it.getCurrent());
    EXPECT_FALSE(it.MoveNext()); // now past the end

    it.Reset();
    EXPECT_THROW((void) it.getCurrent(), System::ArgumentOutOfRangeException); // back to pre-MoveNext()
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&gamerA, it.getCurrent()); // enumeration genuinely restarted, not just position_ + 1
}

// Task 9.3: the same enumerator behavior (GetEnumerator/MoveNext/getCurrent/Reset/Dispose) was
// only ever exercised through SignedInGamerCollection - confirms it works identically through
// FriendCollection, GamerCollection<T>'s other concrete subclass, with 2+ elements (not just 0 or
// 1, which the pre-existing FriendCollectionTest cases were limited to).
TEST(GamerCollectionEnumeratorTest, WorksThroughFriendCollectionToo) {
    auto fgA = FriendGamer::CreateInternal("a", "A", false, false, false, false, false, false);
    auto fgB = FriendGamer::CreateInternal("b", "B", false, false, false, false, false, false);
    auto col = FriendCollection::CreateInternal({&fgA, &fgB});
    auto it = col.GetEnumerator();
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&fgA, it.getCurrent());
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&fgB, it.getCurrent());
    EXPECT_FALSE(it.MoveNext());

    it.Reset();
    ASSERT_TRUE(it.MoveNext());
    EXPECT_EQ(&fgA, it.getCurrent());
}

// Task 9.3: GamerCollection<T>::Add/Remove (NOXNA mutators) had zero test coverage across every
// GamerServices test file - exactly the coverage gap that let Task 7.8's getCurrent() bug ship
// undetected. Exercised through both concrete subclasses, with 2+ elements.

TEST(SignedInGamerCollectionTest, AddAppendsAndRemoveDeletesTheRightElement) {
    auto gamerA = SignedInGamer::CreateInternal("a");
    auto gamerB = SignedInGamer::CreateInternal("b");
    auto gamerC = SignedInGamer::CreateInternal("c");
    auto col = SignedInGamerCollection::CreateInternal({&gamerA, &gamerB});

    col.Add(&gamerC);
    ASSERT_EQ(3, col.getCountProperty());
    EXPECT_EQ(&gamerC, col[2]);

    col.Remove(&gamerA);
    ASSERT_EQ(2, col.getCountProperty());
    EXPECT_EQ(&gamerB, col[0]);
    EXPECT_EQ(&gamerC, col[1]);
}

TEST(FriendCollectionTest, AddAppendsAndRemoveDeletesTheRightElement) {
    auto fgA = FriendGamer::CreateInternal("a", "A", false, false, false, false, false, false);
    auto fgB = FriendGamer::CreateInternal("b", "B", false, false, false, false, false, false);
    auto fgC = FriendGamer::CreateInternal("c", "C", false, false, false, false, false, false);
    auto col = FriendCollection::CreateInternal({&fgA, &fgB});

    col.Add(&fgC);
    ASSERT_EQ(3, col.getCountProperty());
    EXPECT_EQ(&fgC, col[2]);

    col.Remove(&fgA);
    ASSERT_EQ(2, col.getCountProperty());
    EXPECT_EQ(&fgB, col[0]);
    EXPECT_EQ(&fgC, col[1]);
}

// --- LeaderboardEntry ---

// Task 9.7: LeaderboardEntry::operator==/operator!= (structural equality over gamer/rating/
// ranking, since by-value storage has no reference-identity equivalent for FNA's own real
// LeaderboardEntry) had zero coverage, despite the class's own doc comment stating the operator
// exists specifically to support ReadOnlyCollection<T>::IndexOf/Contains.
TEST(LeaderboardEntryTest, EqualityIsStructural) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, false, false);
    auto a = LeaderboardEntry::CreateInternal(&fg, 100, 1);
    auto b = LeaderboardEntry::CreateInternal(&fg, 100, 1);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(LeaderboardEntryTest, DifferingGamerIsNotEqual) {
    auto fgA = FriendGamer::CreateInternal("a", "A", false, false, false, false, false, false);
    auto fgB = FriendGamer::CreateInternal("b", "B", false, false, false, false, false, false);
    auto a = LeaderboardEntry::CreateInternal(&fgA, 100, 1);
    auto b = LeaderboardEntry::CreateInternal(&fgB, 100, 1);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(LeaderboardEntryTest, DifferingRatingIsNotEqual) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, false, false);
    auto a = LeaderboardEntry::CreateInternal(&fg, 100, 1);
    auto b = LeaderboardEntry::CreateInternal(&fg, 200, 1);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(LeaderboardEntryTest, DifferingRankingIsNotEqual) {
    auto fg = FriendGamer::CreateInternal("t", "d", false, false, false, false, false, false);
    auto a = LeaderboardEntry::CreateInternal(&fg, 100, 1);
    auto b = LeaderboardEntry::CreateInternal(&fg, 100, 2);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}
