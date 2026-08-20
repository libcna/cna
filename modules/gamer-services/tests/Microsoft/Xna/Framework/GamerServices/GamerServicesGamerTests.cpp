// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <filesystem>
#include <fstream>

#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "SignedInGamerTestAccess.hpp"

namespace {
    // Task 4.7 (plans/plan_net.md Phase 4): achievement/leaderboard persistence now touches a real
    // local store on disk, keyed only by gamertag - many unrelated tests in this file reuse
    // gamertags like "tag1", so without isolation a persistence test could see leftover state
    // from an earlier test (or leak state into a later one). Redirects the store to a dedicated,
    // wiped-clean-before-and-after test app name via StorageDevice::SetAppNameEXT, rather than
    // relying on every test using a globally-unique gamertag.
    struct GamerServicesStoreGuard {
        GamerServicesStoreGuard() {
            Microsoft::Xna::Framework::Storage::StorageDevice::SetAppNameEXT("CnaTestsGamerServices");
            CNA::Internal::GamerServices::ResetStoreForTestingEXT();
        }
        ~GamerServicesStoreGuard() {
            CNA::Internal::GamerServices::ResetStoreForTestingEXT();
        }
    };
}

using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
    FriendGamer MakeGamer() {
        return FriendGamer::CreateInternal("tag1", "Display1", false, false, false, false, false, false);
    }

    // Task 10.1: no current production subclass (FriendGamer/SignedInGamer/NetworkGamer) ever
    // omits Gamer's own displayName argument - they all forward an explicit (possibly empty)
    // string. This minimal test-only subclass exercises the true "argument omitted entirely"
    // path (Gamer's own std::nullopt default), which is otherwise unreachable from outside the
    // GamerServices/Net namespaces.
    class TestOnlyGamer : public Gamer {
    public:
        explicit TestOnlyGamer(const std::string& gamertag) : Gamer(gamertag) {}
    };
}

// --- Gamer (via FriendGamer, since Gamer itself is abstract) ---

TEST(GamerTest, DisplayNameGetSet) {
    auto g = MakeGamer();
    EXPECT_EQ("Display1", g.getDisplayNameProperty());
    g.setDisplayNameProperty("NewName");
    EXPECT_EQ("NewName", g.getDisplayNameProperty());
}

// Task 10.1: matches FNA's own `DisplayName = displayName ?? gamertag`, which substitutes only
// for a true C# null, never for an explicitly-passed empty string. FriendGamer's displayName
// parameter is required (not optional/defaulted) in both FNA and this port, so a caller passing
// an explicit "" must have that "" preserved, not silently coerced to the gamertag.
TEST(GamerTest, DisplayNameStaysEmptyWhenExplicitlyEmpty) {
    auto g = FriendGamer::CreateInternal("tagonly", "", false, false, false, false, false, false);
    EXPECT_EQ("", g.getDisplayNameProperty());
}

// The gamertag fallback only applies when displayName is omitted entirely (Gamer's own
// std::nullopt default) - unreachable through any current production subclass, so exercised
// directly via TestOnlyGamer instead.
TEST(GamerTest, DisplayNameFallsBackToGamertagWhenOmitted) {
    TestOnlyGamer g("tagonly");
    EXPECT_EQ("tagonly", g.getDisplayNameProperty());
}

TEST(GamerTest, GamertagIsReadOnly) {
    auto g = MakeGamer();
    EXPECT_EQ("tag1", g.getGamertagProperty());
}

TEST(GamerTest, IsDisposedDefaultsFalse) {
    auto g = MakeGamer();
    EXPECT_FALSE(g.getIsDisposedProperty());
}

TEST(GamerTest, TagGetSet) {
    auto g = MakeGamer();
    g.setTagProperty(std::any(42));
    EXPECT_EQ(42, std::any_cast<int>(g.getTagProperty()));
}

// Task 4.3 (plans/plan_net.md Phase 4): GetLeaderboard() is now a real implementation - see the
// dedicated LeaderboardWriterTest suite below for full coverage; this confirms it's reachable and
// non-throwing through any Gamer-derived type, not just SignedInGamer.
TEST(GamerTest, LeaderboardWriterGetLeaderboardReturnsRealEntry) {
    GamerServicesStoreGuard guard;
    auto g = MakeGamer();
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    LeaderboardEntry* entry = nullptr;
    EXPECT_NO_THROW(entry = g.getLeaderboardWriterProperty().GetLeaderboard(id));
    ASSERT_NE(nullptr, entry);
    EXPECT_EQ(&g, entry->getGamerProperty());
}

TEST(GamerTest, ToStringReturnsDisplayName) {
    auto g = MakeGamer();
    EXPECT_EQ("Display1", g.ToString());
}

TEST(GamerTest, SignedInGamersStaticNotNull) {
    EXPECT_NE(nullptr, Gamer::getSignedInGamersProperty());
}

// Task 9.1: setSignedInGamersProperty's delete-old-then-replace logic had zero direct coverage -
// only the getter (returning a non-null default) was ever tested. Covers setting once, setting
// twice (the old SignedInGamerCollection wrapper must be replaced cleanly, not merely leaked or
// left dangling - see Task 7.5's write-up for the adjacent SignedInGamer* leak this same setter
// exposed inside GamerServicesDispatcher::Initialize()), and setting to the same pointer (a
// documented no-op via the setter's own `if (signedInGamers_ != value)` guard).
//
// Installs a fresh, empty SignedInGamerCollection on teardown rather than restoring a captured
// "previous" pointer - setSignedInGamersProperty unconditionally deletes whatever it replaces, so
// reusing a captured previous pointer would double-free (see NetworkSessionTests.cpp's own
// RestoreGlobalGuard precedent, first established while fixing Task 2.15's double-free).
TEST(GamerTest, SetSignedInGamersPropertyReplacesThePreviousCollection) {
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    auto* first = new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}));
    Gamer::setSignedInGamersProperty(first);
    EXPECT_EQ(first, Gamer::getSignedInGamersProperty());

    auto* second = new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}));
    Gamer::setSignedInGamersProperty(second); // must replace (and free) `first`, not leak it
    EXPECT_EQ(second, Gamer::getSignedInGamersProperty());

    // Setting to the same pointer again must be a safe no-op, not a self-delete.
    Gamer::setSignedInGamersProperty(second);
    EXPECT_EQ(second, Gamer::getSignedInGamersProperty());
}

TEST(GamerTest, GetProfileReturnsUsableProfile) {
    auto g = MakeGamer();
    GamerProfile* profile = g.GetProfile();
    ASSERT_NE(nullptr, profile);
    EXPECT_FALSE(profile->getIsDisposedProperty());
    profile->Dispose();
    EXPECT_TRUE(profile->getIsDisposedProperty());
    delete profile;
}

TEST(GamerTest, BeginEndGetProfile) {
    auto g = MakeGamer();
    System::IAsyncResult* result = g.BeginGetProfile(System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());
    GamerProfile* profile = g.EndGetProfile(result);
    ASSERT_NE(nullptr, profile);
    delete profile;
    delete result;
}

// audit_net.md High finding: GamerAction stored its AsyncCallback but never invoked it, despite
// this action already completing synchronously (getIsCompletedProperty() == true) right after
// BeginGetProfile returns. Confirms the callback now fires exactly once with the correct
// IAsyncResult identity and AsyncState.
TEST(GamerTest, BeginGetProfileInvokesCallbackExactlyOnceWithCorrectIdentity) {
    auto g = MakeGamer();
    int callCount = 0;
    System::IAsyncResult* observedResult = nullptr;
    std::any state = 3;

    System::IAsyncResult* result = g.BeginGetProfile(
        [&callCount, &observedResult](System::IAsyncResult& ar) {
            ++callCount;
            observedResult = &ar;
        },
        state
    );

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 3);

    GamerProfile* profile = g.EndGetProfile(result);
    delete profile;
    delete result;
}

TEST(GamerTest, GetFromGamertagThrows) {
    EXPECT_THROW(Gamer::GetFromGamertag("someone"), System::NotSupportedException);
}

TEST(GamerTest, BeginGetFromGamertagThrows) {
    EXPECT_THROW(
        Gamer::BeginGetFromGamertag("someone", System::AsyncCallback{}, std::any{}),
        System::NotSupportedException
    );
}

TEST(GamerTest, EndGetFromGamertagThrows) {
    EXPECT_THROW(Gamer::EndGetFromGamertag(nullptr), System::NotSupportedException);
}

TEST(GamerTest, GetPartnerTokenThrows) {
    EXPECT_THROW(Gamer::GetPartnerToken("uri"), System::NotSupportedException);
}

TEST(GamerTest, BeginGetPartnerTokenThrows) {
    EXPECT_THROW(
        Gamer::BeginGetPartnerToken("uri", System::AsyncCallback{}, std::any{}),
        System::NotSupportedException
    );
}

TEST(GamerTest, EndGetPartnerTokenThrows) {
    EXPECT_THROW(Gamer::EndGetPartnerToken(nullptr), System::NotSupportedException);
}

// --- GamerProfile ---

TEST(GamerProfileTest, DefaultValues) {
    auto p = GamerProfile::CreateInternal();
    EXPECT_EQ(0, p.getGamerScoreProperty());
    EXPECT_EQ(GamerZone::Pro, p.getGamerZoneProperty());
    EXPECT_EQ("", p.getMottoProperty());
    EXPECT_FLOAT_EQ(5.0f, p.getReputationProperty());
    EXPECT_EQ(1, p.getTitlesPlayedProperty());
    EXPECT_EQ(0, p.getTotalAchievementsProperty());
    EXPECT_FALSE(p.getIsDisposedProperty());
}

TEST(GamerProfileTest, RegionDefaultsToCurrentRegion) {
    auto p = GamerProfile::CreateInternal();
    EXPECT_EQ("US", p.getRegionProperty().getNameProperty());
}

TEST(GamerProfileTest, Dispose) {
    auto p = GamerProfile::CreateInternal();
    p.Dispose();
    EXPECT_TRUE(p.getIsDisposedProperty());
}

TEST(GamerProfileTest, GetGamerPictureReturnsNull) {
    auto p = GamerProfile::CreateInternal();
    EXPECT_EQ(nullptr, p.GetGamerPicture());
}

// --- LeaderboardEntry ---

TEST(LeaderboardEntryTest, PropertiesFromCtor) {
    auto e = LeaderboardEntry::CreateInternal(nullptr, 100, 5);
    EXPECT_EQ(nullptr, e.getGamerProperty());
    EXPECT_EQ(100, e.getRatingProperty());
    EXPECT_EQ(5, e.getRankingEXTProperty());
    EXPECT_EQ(0, e.getColumnsProperty().getCountProperty());
}

TEST(LeaderboardEntryTest, SetRating) {
    auto e = LeaderboardEntry::CreateInternal(nullptr, 1, 1);
    e.setRatingProperty(999);
    EXPECT_EQ(999, e.getRatingProperty());
}

TEST(LeaderboardEntryTest, ColumnsAreMutable) {
    auto e = LeaderboardEntry::CreateInternal(nullptr, 1, 1);
    e.getColumnsProperty().SetValue("x", 42);
    EXPECT_EQ(42, e.getColumnsProperty().GetValueInt32("x"));
}

TEST(LeaderboardEntryTest, GamerPointerRoundTrips) {
    auto g = MakeGamer();
    auto e = LeaderboardEntry::CreateInternal(&g, 1, 1);
    EXPECT_EQ(&g, e.getGamerProperty());
}

// --- LeaderboardWriter (Task 4.3, plans/plan_net.md Phase 4) ---
//
// LeaderboardWriter has no public constructor (matching real XNA - it's only ever obtained via
// Gamer.LeaderboardWriter), so these are exercised through a real Gamer, not standalone.

TEST(LeaderboardWriterTest, GetLeaderboardReturnsRealEntryForOwningGamer) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("writer_tag");
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);

    LeaderboardEntry* entry = gamer.getLeaderboardWriterProperty().GetLeaderboard(id);
    ASSERT_NE(nullptr, entry);
    EXPECT_EQ(0, entry->getRatingProperty());
    EXPECT_EQ(&gamer, entry->getGamerProperty());
}

TEST(LeaderboardWriterTest, GetLeaderboardReturnsTheSameEntryOnRepeatedCalls) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("writer_tag");
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);

    LeaderboardEntry* first = gamer.getLeaderboardWriterProperty().GetLeaderboard(id);
    LeaderboardEntry* second = gamer.getLeaderboardWriterProperty().GetLeaderboard(id);
    EXPECT_EQ(first, second);
}

// Task 4.3's real "write path": no explicit commit method exists anywhere in real XNA's
// LeaderboardWriter API surface, so setRatingProperty() is the closest honest analog to "I'm done
// configuring this entry" and persists immediately - proven here by destroying the writing
// SignedInGamer and obtaining a *fresh* one for the same gamertag before reading it back.
TEST(LeaderboardWriterTest, SettingRatingPersistsAndSurvivesAcrossFreshObjects) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    {
        auto writer = SignedInGamer::CreateInternal("writer_tag");
        writer.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(1500);
    } // writer fully destroyed here

    auto reader = SignedInGamer::CreateInternal("writer_tag");
    LeaderboardEntry* entry = reader.getLeaderboardWriterProperty().GetLeaderboard(id);
    EXPECT_EQ(1500, entry->getRatingProperty());
}

TEST(LeaderboardWriterTest, ColumnsPersistAlongsideRating) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    {
        auto writer = SignedInGamer::CreateInternal("writer_tag");
        LeaderboardEntry* entry = writer.getLeaderboardWriterProperty().GetLeaderboard(id);
        entry->getColumnsProperty().SetValue("Wins", 7);
        entry->setRatingProperty(500); // the real persistence trigger - see its own doc comment
    }

    auto reader = SignedInGamer::CreateInternal("writer_tag");
    LeaderboardEntry* entry = reader.getLeaderboardWriterProperty().GetLeaderboard(id);
    EXPECT_EQ(500, entry->getRatingProperty());
    EXPECT_EQ(7, entry->getColumnsProperty().GetValueInt32("Wins"));
}

TEST(LeaderboardWriterTest, EntriesAreIsolatedPerLeaderboardIdentity) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("writer_tag");
    auto idA = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto idB = LeaderboardIdentity::Create(LeaderboardKey::BestTimeRecent);

    gamer.getLeaderboardWriterProperty().GetLeaderboard(idA)->setRatingProperty(100);
    gamer.getLeaderboardWriterProperty().GetLeaderboard(idB)->setRatingProperty(200);

    EXPECT_EQ(100, gamer.getLeaderboardWriterProperty().GetLeaderboard(idA)->getRatingProperty());
    EXPECT_EQ(200, gamer.getLeaderboardWriterProperty().GetLeaderboard(idB)->getRatingProperty());
}

// --- LeaderboardReader ---

TEST(LeaderboardReaderTest, PropertiesFromCtor) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 10, 1));
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 20, 2));
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 30, 3));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 2, cache, false);
    EXPECT_EQ(0, reader.getPageStartProperty());
    // Task 4.4 fix-up (plans/plan_net.md Phase 4): totalLeaderboardSize_ used to be left at its default
    // 0 forever (a real bug - getTotalLeaderboardSizeProperty() always lied); it now reflects
    // entryCache_'s own size, since entryCache_ always holds this reader's complete board.
    EXPECT_EQ(3, reader.getTotalLeaderboardSizeProperty());
    EXPECT_FALSE(reader.getIsDisposedProperty());
    EXPECT_EQ("BestScoreLifeTime", reader.getLeaderboardIdentityProperty().getKeyProperty());
    const auto entries = reader.getEntriesProperty();
    EXPECT_EQ(2, entries.getCountProperty());
    EXPECT_EQ(1, entries[0].getRankingEXTProperty());
    EXPECT_EQ(2, entries[1].getRankingEXTProperty());
}

TEST(LeaderboardReaderTest, EntriesLoopBoundMatchesFNAExactly) {
    // FNA's ctor loop is `for (i = pageStart; i < pageSize && i < entryCache.Count; i++)`.
    // With pageStart=1, pageSize=2 that yields only entryCache[1], not two entries.
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 1));
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 2));
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 3));
    auto reader = LeaderboardReader::CreateInternal(id, 1, 2, cache, false);
    const auto entries = reader.getEntriesProperty();
    ASSERT_EQ(1, entries.getCountProperty());
    EXPECT_EQ(2, entries[0].getRankingEXTProperty());
}

TEST(LeaderboardReaderTest, CanPageDownUpEmptyCache) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 10, {}, false);
    EXPECT_FALSE(reader.getCanPageDownProperty());
    EXPECT_FALSE(reader.getCanPageUpProperty());
}

TEST(LeaderboardReaderTest, CanPageDownFriendBoard) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 5; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 2, cache, true);
    EXPECT_TRUE(reader.getCanPageDownProperty());   // (0+2) < 5
    auto reader2 = LeaderboardReader::CreateInternal(id, 3, 2, cache, true);
    EXPECT_FALSE(reader2.getCanPageDownProperty());  // (3+2) < 5 is false
}

TEST(LeaderboardReaderTest, CanPageUpFriendBoard) {
    // Task 4.4 fix-up (plans/plan_net.md Phase 4): friend and non-friend boards now share the exact same
    // bounded-array paging math - entryCache_ always holds this reader's complete board either
    // way (full local board, or the gamer-restricted subset), so friends=true no longer selects a
    // different rule (the old "(pageStart - pageSize) >= 0" full-page-only rule this test used to
    // check is gone; see getCanPageUpProperty()'s own comment for why pageStart_ > 0 is correct
    // for both, including a *partial* previous page from a pivot-centered read).
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 1));
    auto reader = LeaderboardReader::CreateInternal(id, 1, 2, cache, true);
    EXPECT_TRUE(reader.getCanPageUpProperty());      // pageStart(1) > 0
    auto reader2 = LeaderboardReader::CreateInternal(id, 0, 2, cache, true);
    EXPECT_FALSE(reader2.getCanPageUpProperty());    // pageStart(0) not > 0
}

TEST(LeaderboardReaderTest, CanPageDownNonFriendBoardByPageStart) {
    // Task 4.4 fix-up (plans/plan_net.md Phase 4): this test used to assert the exact off-by-one bug it
    // is now named for catching - the old non-friend branch was `pageStart_ < entryCache_.size()`,
    // true for almost the entire board (e.g. pageStart=0, a 1-entry, 1-per-page board: 0 < 1 was
    // true, wrongly claiming a nonexistent second page existed). The correct bounded-array check
    // - the same one the friend-board branch always used - is (pageStart_ + pageSize_) < size.
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 3; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, cache, false);
    EXPECT_TRUE(reader.getCanPageDownProperty());    // (0+1) < 3 - two more pages remain
    auto readerAtLastPage = LeaderboardReader::CreateInternal(id, 2, 1, cache, false);
    EXPECT_FALSE(readerAtLastPage.getCanPageDownProperty()); // (2+1) < 3 is false - on the last page
}

TEST(LeaderboardReaderTest, CanPageDownReflectsTotalLeaderboardSize) {
    // Task 4.4 fix-up (plans/plan_net.md Phase 4): getTotalLeaderboardSizeProperty() used to always
    // return 0 (never assigned); it now equals entryCache_'s own size, matching a single-page
    // local board where the whole board is always cached.
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 5; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, cache, false);
    EXPECT_EQ(5, reader.getTotalLeaderboardSizeProperty());
    EXPECT_TRUE(reader.getCanPageDownProperty());
}

TEST(LeaderboardReaderTest, CanPageDownNonFriendBoardFalse) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 5));
    auto reader = LeaderboardReader::CreateInternal(id, 5, 1, cache, false);
    EXPECT_FALSE(reader.getCanPageDownProperty());  // (5+1) not < 1
}

TEST(LeaderboardReaderTest, CanPageUpNonFriendBoardByPageStart) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 1));
    auto reader = LeaderboardReader::CreateInternal(id, 1, 1, cache, false);
    EXPECT_TRUE(reader.getCanPageUpProperty());     // pageStart(1) > 0
}

TEST(LeaderboardReaderTest, CanPageUpNonFriendBoardFalse) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 1));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, cache, false);
    EXPECT_FALSE(reader.getCanPageUpProperty());    // pageStart(0) not > 0, ranking(1) not > 1
}

TEST(LeaderboardReaderTest, Dispose) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    reader.Dispose();
    EXPECT_TRUE(reader.getIsDisposedProperty());
}

// Task 4.4 (plans/plan_net.md Phase 4): PageDown/PageUp are real now - throw InvalidOperationException
// (not NotSupportedException) only when getCanPageDownProperty()/getCanPageUpProperty() is false,
// matching the Begin/End-pair convention used elsewhere in this codebase.
TEST(LeaderboardReaderTest, PageDownThrowsWhenCannotPageDown) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false); // empty cache - CanPageDown is false
    EXPECT_THROW(reader.PageDown(), System::InvalidOperationException);
}

TEST(LeaderboardReaderTest, BeginPageDownThrowsWhenCannotPageDown) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    EXPECT_THROW(reader.BeginPageDown(System::AsyncCallback{}, std::any{}), System::InvalidOperationException);
}

TEST(LeaderboardReaderTest, EndPageDownThrowsForMismatchedResult) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 4; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 2, cache, true);
    EXPECT_THROW(reader.EndPageDown(nullptr), System::ArgumentException);
}

// Real page-navigation behavior: entryCache_ holds all 4 entries, page size 2 - PageDown must
// advance from [0,2) to [2,4).
TEST(LeaderboardReaderTest, PageDownAdvancesToTheNextPage) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 4; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 2, cache, true);
    const auto firstPage = reader.getEntriesProperty();
    ASSERT_EQ(2, firstPage.getCountProperty());
    EXPECT_EQ(1, firstPage[0].getRankingEXTProperty());

    reader.PageDown();

    EXPECT_EQ(2, reader.getPageStartProperty());
    const auto entries = reader.getEntriesProperty();
    ASSERT_EQ(2, entries.getCountProperty());
    EXPECT_EQ(3, entries[0].getRankingEXTProperty());
    EXPECT_EQ(4, entries[1].getRankingEXTProperty());
}

TEST(LeaderboardReaderTest, PageUpThrowsWhenCannotPageUp) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false); // empty cache - CanPageUp is false
    EXPECT_THROW(reader.PageUp(), System::InvalidOperationException);
}

TEST(LeaderboardReaderTest, BeginPageUpThrowsWhenCannotPageUp) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    EXPECT_THROW(reader.BeginPageUp(System::AsyncCallback{}, std::any{}), System::InvalidOperationException);
}

TEST(LeaderboardReaderTest, EndPageUpThrowsForMismatchedResult) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 4; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 2, 2, cache, true);
    EXPECT_THROW(reader.EndPageUp(nullptr), System::ArgumentException);
}

TEST(LeaderboardReaderTest, PageUpMovesToThePreviousPage) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    for (int i = 0; i < 4; ++i) cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, i + 1));
    auto reader = LeaderboardReader::CreateInternal(id, 2, 2, cache, true);

    reader.PageUp();

    EXPECT_EQ(0, reader.getPageStartProperty());
    const auto entries = reader.getEntriesProperty();
    ASSERT_EQ(2, entries.getCountProperty());
    EXPECT_EQ(1, entries[0].getRankingEXTProperty());
    EXPECT_EQ(2, entries[1].getRankingEXTProperty());
}

// --- LeaderboardReader::Read/BeginRead/EndRead (Task 4.4, real local-store-backed reads) ---

namespace {
    // LeaderboardReader::Read matches persisted records against Gamer::getSignedInGamersProperty()
    // (a real, live Gamer* is required - see plans/plan_net.md Task 4.4's own documented limitation),
    // which SignedInGamer::CreateInternal alone does not populate - callers must publish their
    // test gamers into it explicitly. Restores the previous (empty) list afterward so this doesn't
    // leak into unrelated tests.
    struct SignedInGamersGuard {
        explicit SignedInGamersGuard(std::vector<SignedInGamer*> gamers) {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
                SignedInGamerCollection::CreateInternal(std::move(gamers))
            ));
        }
        ~SignedInGamersGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    };
}

TEST(LeaderboardReaderTest, ReadReturnsEntriesSortedByRatingDescending) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto low = SignedInGamer::CreateInternal("low_tag");
    auto high = SignedInGamer::CreateInternal("high_tag");
    SignedInGamersGuard signedIn({&low, &high});
    low.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(10);
    high.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(90);

    LeaderboardReader reader = LeaderboardReader::Read(id, 0, 10);

    const auto entries = reader.getEntriesProperty();
    ASSERT_EQ(2, entries.getCountProperty());
    EXPECT_EQ(90, entries[0].getRatingProperty());
    EXPECT_EQ(1, entries[0].getRankingEXTProperty());
    EXPECT_EQ(10, entries[1].getRatingProperty());
    EXPECT_EQ(2, entries[1].getRankingEXTProperty());
}

// Documented limitation (plans/plan_net.md Task 4.4): a persisted gamertag with no currently
// signed-in Gamer* match is skipped, since LeaderboardEntry needs a real, live Gamer*.
TEST(LeaderboardReaderTest, ReadSkipsPersistedEntriesWithNoCurrentlySignedInGamer) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    {
        auto ephemeral = SignedInGamer::CreateInternal("ephemeral_tag");
        SignedInGamersGuard signedIn({&ephemeral});
        ephemeral.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(500);
    } // ephemeral is gone, and no longer published as signed-in either

    LeaderboardReader reader = LeaderboardReader::Read(id, 0, 10);
    EXPECT_EQ(0, reader.getEntriesProperty().getCountProperty());
}

TEST(LeaderboardReaderTest, ReadCentersOnPivotGamer) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto a = SignedInGamer::CreateInternal("a_tag");
    auto b = SignedInGamer::CreateInternal("b_tag");
    auto c = SignedInGamer::CreateInternal("c_tag");
    SignedInGamersGuard signedIn({&a, &b, &c});
    a.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(300);
    b.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(200);
    c.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(100);

    LeaderboardReader reader = LeaderboardReader::Read(id, &b, 1);

    const auto entries = reader.getEntriesProperty();
    ASSERT_EQ(1, entries.getCountProperty());
    EXPECT_EQ(&b, entries[0].getGamerProperty());
}

TEST(LeaderboardReaderTest, ReadRestrictsToGivenGamersList) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto a = SignedInGamer::CreateInternal("a_tag");
    auto b = SignedInGamer::CreateInternal("b_tag");
    auto c = SignedInGamer::CreateInternal("c_tag");
    SignedInGamersGuard signedIn({&a, &b, &c});
    a.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(300);
    b.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(200);
    c.getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(100);

    LeaderboardReader reader = LeaderboardReader::Read(id, std::vector<Gamer*>{&a, &c}, &a, 10);

    const auto entries = reader.getEntriesProperty();
    ASSERT_EQ(2, entries.getCountProperty());
    EXPECT_EQ(&a, entries[0].getGamerProperty());
    EXPECT_EQ(&c, entries[1].getGamerProperty());
}

TEST(LeaderboardReaderTest, BeginReadOverloadsReturnRealResults) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);

    System::IAsyncResult* r1 = LeaderboardReader::BeginRead(id, 0, 10, System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, r1);
    EXPECT_TRUE(r1->getIsCompletedProperty());
    LeaderboardReader::EndRead(r1);
    delete r1;

    System::IAsyncResult* r2 = LeaderboardReader::BeginRead(id, nullptr, 10, System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, r2);
    LeaderboardReader::EndRead(r2);
    delete r2;

    System::IAsyncResult* r3 = LeaderboardReader::BeginRead(
        id, std::vector<Gamer*>{}, nullptr, 10, System::AsyncCallback{}, std::any{}
    );
    ASSERT_NE(nullptr, r3);
    LeaderboardReader::EndRead(r3);
    delete r3;
}

// audit_net.md-style callback check: every Begin* in this codebase must actually invoke its
// callback, not just store it (Phase 13's own High finding, applied here for a newly-implemented
// Begin* rather than a pre-existing one).
TEST(LeaderboardReaderTest, BeginReadInvokesCallbackExactlyOnce) {
    GamerServicesStoreGuard guard;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    int callCount = 0;
    System::IAsyncResult* result = LeaderboardReader::BeginRead(
        id, 0, 10,
        [&callCount](System::IAsyncResult&) { ++callCount; },
        std::any{}
    );
    EXPECT_EQ(callCount, 1);
    LeaderboardReader::EndRead(result);
    delete result;
}

TEST(LeaderboardReaderTest, EndReadThrowsForMismatchedResult) {
    EXPECT_THROW(LeaderboardReader::EndRead(nullptr), System::ArgumentException);
}

// --- SignedInGamer ---
// IsHeadset() is not covered: it requires a real Microsoft::Xna::Framework::Audio::Microphone,
// which is only constructible via MicrophoneFactory against a real SDL audio device.

TEST(SignedInGamerTest, DefaultsFromCreateInternal) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    EXPECT_EQ("tag1", gamer.getGamertagProperty());
    EXPECT_EQ("tag1", gamer.getDisplayNameProperty());
    EXPECT_FALSE(gamer.getIsGuestProperty());
    EXPECT_FALSE(gamer.getIsSignedInToLiveProperty());
    EXPECT_EQ(1, gamer.getPartySizeProperty());
    EXPECT_EQ(Microsoft::Xna::Framework::PlayerIndex::One, gamer.getPlayerIndexProperty());
}

TEST(SignedInGamerTest, CustomParameters) {
    auto gamer = SignedInGamer::CreateInternal("tag2", true, true, Microsoft::Xna::Framework::PlayerIndex::Two);
    EXPECT_TRUE(gamer.getIsGuestProperty());
    EXPECT_TRUE(gamer.getIsSignedInToLiveProperty());
    EXPECT_EQ(Microsoft::Xna::Framework::PlayerIndex::Two, gamer.getPlayerIndexProperty());
}

TEST(SignedInGamerTest, PartySizeSet) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    gamer.setPartySizeProperty(4);
    EXPECT_EQ(4, gamer.getPartySizeProperty());
}

// Task 9.11: FNA's real SignedInGamer.Presence is get-only but returns a GamerPresence *class*
// (reference type), so real game code writes `gamer.Presence.PresenceMode = ...` and mutates the
// same live object - a const-only getPresenceProperty() would make this permanently impossible in
// C++. Proves the non-const overload mutates the gamer's own live GamerPresence, not a copy: the
// change is visible through a completely separate later call to the property, including the
// const overload used by a const-qualified reference to the same gamer.
TEST(SignedInGamerTest, PresencePropertyNonConstOverloadMutatesLiveObject) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    ASSERT_EQ(GamerPresenceMode::None, gamer.getPresenceProperty().getPresenceModeProperty());

    gamer.getPresenceProperty().setPresenceModeProperty(GamerPresenceMode::SinglePlayer);

    EXPECT_EQ(GamerPresenceMode::SinglePlayer, gamer.getPresenceProperty().getPresenceModeProperty());
    const SignedInGamer& constGamer = gamer;
    EXPECT_EQ(GamerPresenceMode::SinglePlayer, constGamer.getPresenceProperty().getPresenceModeProperty());
}

// Task 7.3: FNA's own internal GameDefaults() constructor is empty, leaving GameDifficulty at
// C#'s implicit default(T) - the ordinal-0 value, GameDifficulty::Easy, not Normal.
TEST(SignedInGamerTest, GameDefaultsPresencePrivilegesDefaults) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    EXPECT_EQ(GameDifficulty::Easy, gamer.getGameDefaultsProperty().getGameDifficultyProperty());
    EXPECT_EQ(GamerPresenceMode::None, gamer.getPresenceProperty().getPresenceModeProperty());
    EXPECT_EQ(GamerPrivilegeSetting::Everyone, gamer.getPrivilegesProperty().getAllowCommunicationProperty());
}

TEST(SignedInGamerTest, InheritsGamer) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    EXPECT_NE(nullptr, dynamic_cast<Gamer*>(&gamer));
}

TEST(SignedInGamerTest, IsFriendAlwaysFalse) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto other = MakeGamer();
    EXPECT_FALSE(gamer.IsFriend(&other));
    EXPECT_FALSE(gamer.IsFriend(nullptr));
}

TEST(SignedInGamerTest, GetFriendsIsEmpty) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto friends = gamer.GetFriends();
    EXPECT_EQ(0, friends.getCountProperty());
}

TEST(SignedInGamerTest, AwardAchievementDoesNotThrow) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    EXPECT_NO_THROW(gamer.AwardAchievement("some_key"));
}

TEST(SignedInGamerTest, BeginEndAwardAchievement) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    System::IAsyncResult* result = gamer.BeginAwardAchievement("key", System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(result->getIsCompletedProperty());
    gamer.EndAwardAchievement(result);
    delete result;
}

// audit_net.md High finding: GamerAction stored its AsyncCallback but never invoked it, despite
// this action already completing synchronously right after BeginAwardAchievement returns.
// Confirms the callback now fires exactly once with the correct IAsyncResult identity/AsyncState.
TEST(SignedInGamerTest, BeginAwardAchievementInvokesCallbackExactlyOnceWithCorrectIdentity) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    int callCount = 0;
    System::IAsyncResult* observedResult = nullptr;
    std::any state = 5;

    System::IAsyncResult* result = gamer.BeginAwardAchievement(
        "key",
        [&callCount, &observedResult](System::IAsyncResult& ar) {
            ++callCount;
            observedResult = &ar;
        },
        state
    );

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 5);

    gamer.EndAwardAchievement(result);
    delete result;
}

// Confirms a callback that reentrantly calls the matching End* from within itself (the standard
// APM pattern of checking IsCompleted and immediately consuming the result) does not make
// BeginAwardAchievement return a stale null - EndAwardAchievement nulls statStoreAction_ as a
// side effect, so the pointer to return must be captured before invoking the callback.
TEST(SignedInGamerTest, BeginAwardAchievementCallbackCanReentrantlyCallEndAwardAchievement) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    bool ended = false;

    System::IAsyncResult* result = gamer.BeginAwardAchievement(
        "key",
        [&gamer, &ended](System::IAsyncResult& ar) {
            gamer.EndAwardAchievement(&ar);
            ended = true;
        },
        std::any{}
    );

    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(ended);
    delete result;
}

TEST(SignedInGamerTest, GetAchievementsReturnsEmptyCollectionWhenNoneEarned) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto achievements = gamer.GetAchievements();
    EXPECT_EQ(0, achievements.getCountProperty());
}

// Task 4.5/4.7 (plans/plan_net.md Phase 4): AwardAchievement now persists to a real local store, and
// GetAchievements() loads it back - confirms the earned key/IsEarned/EarnedDateTime round-trip
// through disk, not just in-memory state that happens to survive within one test. Name/
// Description/GamerScore have no local source of truth (see AwardAchievement's own doc comment)
// so they're expected to stay at their empty/zero defaults.
TEST(SignedInGamerTest, AwardAchievementPersistsAndGetAchievementsReflectsIt) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("persist_tag");

    gamer.AwardAchievement("first_steps");
    auto achievements = gamer.GetAchievements();

    ASSERT_EQ(1, achievements.getCountProperty());
    EXPECT_EQ("first_steps", achievements[0].getKeyProperty());
    EXPECT_TRUE(achievements[0].getIsEarnedProperty());
}

// The real disk-persistence proof: destroy every in-process SignedInGamer/AchievementCollection
// object between writing and reading, and confirm the earned achievement still comes back -
// state that only survived in memory would not.
TEST(SignedInGamerTest, AwardedAchievementSurvivesAcrossFreshObjects) {
    GamerServicesStoreGuard guard;
    {
        auto writer = SignedInGamer::CreateInternal("reload_tag");
        writer.AwardAchievement("collector");
    } // writer (and any in-memory-only state) is fully destroyed here

    auto reader = SignedInGamer::CreateInternal("reload_tag");
    auto achievements = reader.GetAchievements();
    ASSERT_EQ(1, achievements.getCountProperty());
    EXPECT_EQ("collector", achievements[0].getKeyProperty());
}

// AwardAchievement/BeginAwardAchievement are the sync/async form of the same real operation -
// confirms BeginAwardAchievement's persistence path (not just AwardAchievement's) round-trips.
TEST(SignedInGamerTest, BeginAwardAchievementPersistsToo) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("begin_award_tag");

    System::IAsyncResult* result = gamer.BeginAwardAchievement("speed_demon", System::AsyncCallback{}, std::any{});
    gamer.EndAwardAchievement(result);
    delete result;

    auto achievements = gamer.GetAchievements();
    ASSERT_EQ(1, achievements.getCountProperty());
    EXPECT_EQ("speed_demon", achievements[0].getKeyProperty());
}

// Awarding the same key twice must not duplicate the entry (upsert, not append).
TEST(SignedInGamerTest, AwardingTheSameAchievementTwiceDoesNotDuplicate) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("dup_tag");
    gamer.AwardAchievement("collector");
    gamer.AwardAchievement("collector");
    auto achievements = gamer.GetAchievements();
    EXPECT_EQ(1, achievements.getCountProperty());
}

// Achievements are per-gamertag - a different gamertag's store must stay empty.
TEST(SignedInGamerTest, AchievementsAreIsolatedPerGamertag) {
    GamerServicesStoreGuard guard;
    auto gamerA = SignedInGamer::CreateInternal("tagA");
    auto gamerB = SignedInGamer::CreateInternal("tagB");
    gamerA.AwardAchievement("first_steps");

    EXPECT_EQ(1, gamerA.GetAchievements().getCountProperty());
    EXPECT_EQ(0, gamerB.GetAchievements().getCountProperty());
}

// Task 4.7: a corrupt/missing store file must never crash - starts empty instead.
TEST(SignedInGamerTest, GetAchievementsHandlesMissingOrCorruptStoreFileGracefully) {
    GamerServicesStoreGuard guard;
    const std::string root = CNA::Internal::GamerServices::GetGamerServicesStoreRootEXT();
    const std::string path = root + "/achievements/corrupt_tag.json";
    std::filesystem::create_directories(root + "/achievements");
    {
        std::ofstream corrupt(path);
        corrupt << "{ this is not valid json";
    }

    auto gamer = SignedInGamer::CreateInternal("corrupt_tag");
    AchievementCollection achievements{AchievementCollection::CreateInternal({})};
    EXPECT_NO_THROW(achievements = gamer.GetAchievements());
    EXPECT_EQ(0, achievements.getCountProperty());
}

TEST(SignedInGamerTest, BeginGetAchievementsTwiceThrows) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    System::IAsyncResult* result = gamer.BeginGetAchievements(System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, result);
    EXPECT_THROW(
        gamer.BeginGetAchievements(System::AsyncCallback{}, std::any{}),
        System::InvalidOperationException
    );
    auto achievements = gamer.EndGetAchievements(result);
    EXPECT_EQ(0, achievements.getCountProperty());
    delete result;
    // After End, the in-progress guard is cleared, so a new Begin no longer throws.
    System::IAsyncResult* result2 = gamer.BeginGetAchievements(System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, result2);
    gamer.EndGetAchievements(result2);
    delete result2;
}

// audit_net.md High finding: same as BeginAwardAchievement above, for BeginGetAchievements.
TEST(SignedInGamerTest, BeginGetAchievementsInvokesCallbackExactlyOnceWithCorrectIdentity) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    int callCount = 0;
    System::IAsyncResult* observedResult = nullptr;
    std::any state = 9;

    System::IAsyncResult* result = gamer.BeginGetAchievements(
        [&callCount, &observedResult](System::IAsyncResult& ar) {
            ++callCount;
            observedResult = &ar;
        },
        state
    );

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 9);

    gamer.EndGetAchievements(result);
    delete result;
}

// Confirms a callback that reentrantly calls the matching End* from within itself does not make
// BeginGetAchievements return a stale null, and that the in-progress guard (statReceiveAction_)
// is correctly cleared by the reentrant call rather than left stale.
TEST(SignedInGamerTest, BeginGetAchievementsCallbackCanReentrantlyCallEndGetAchievements) {
    GamerServicesStoreGuard guard;
    auto gamer = SignedInGamer::CreateInternal("tag1");
    bool ended = false;

    System::IAsyncResult* result = gamer.BeginGetAchievements(
        [&gamer, &ended](System::IAsyncResult& ar) {
            (void) gamer.EndGetAchievements(&ar);
            ended = true;
        },
        std::any{}
    );

    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(ended);
    delete result;

    System::IAsyncResult* result2 = nullptr;
    EXPECT_NO_THROW(result2 = gamer.BeginGetAchievements(System::AsyncCallback{}, std::any{}));
    ASSERT_NE(nullptr, result2);
    gamer.EndGetAchievements(result2);
    delete result2;
}

TEST(SignedInGamerTest, SignedInEventFires) {
    auto gamer = SignedInGamer::CreateInternal("signin_tag");
    SignedInGamer* seen = nullptr;
    auto token = SignedInGamer::SignedIn.Add(
        [&seen](System::Object* /*sender*/, const SignedInEventArgs& e) {
            seen = e.getGamerProperty();
        }
    );
    SignedInGamerTestAccess::OnSignIn(&gamer);
    EXPECT_EQ(&gamer, seen);
    SignedInGamer::SignedIn.Remove(token);
}

TEST(SignedInGamerTest, SignedOutEventFires) {
    auto gamer = SignedInGamer::CreateInternal("signout_tag");
    SignedInGamer* seen = nullptr;
    auto token = SignedInGamer::SignedOut.Add(
        [&seen](System::Object* /*sender*/, const SignedOutEventArgs& e) {
            seen = e.getGamerProperty();
        }
    );
    SignedInGamerTestAccess::OnSignOut(&gamer);
    EXPECT_EQ(&gamer, seen);
    SignedInGamer::SignedOut.Remove(token);
}
