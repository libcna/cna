// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>

#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

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
#include "SignedInGamerTestAccess.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
    FriendGamer MakeGamer() {
        return FriendGamer::CreateInternal("tag1", "Display1", false, false, false, false, false, false);
    }
}

// --- Gamer (via FriendGamer, since Gamer itself is abstract) ---

TEST(GamerTest, DisplayNameGetSet) {
    auto g = MakeGamer();
    EXPECT_EQ("Display1", g.getDisplayNameProperty());
    g.setDisplayNameProperty("NewName");
    EXPECT_EQ("NewName", g.getDisplayNameProperty());
}

TEST(GamerTest, DisplayNameFallsBackToGamertagWhenEmpty) {
    auto g = FriendGamer::CreateInternal("tagonly", "", false, false, false, false, false, false);
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

TEST(GamerTest, LeaderboardWriterGetLeaderboardThrows) {
    auto g = MakeGamer();
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    EXPECT_THROW(g.getLeaderboardWriterProperty().GetLeaderboard(id), System::NotSupportedException);
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

// --- LeaderboardWriter ---

TEST(LeaderboardWriterTest, GetLeaderboardThrows) {
    LeaderboardWriter w;
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    EXPECT_THROW(w.GetLeaderboard(id), System::NotSupportedException);
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
    EXPECT_EQ(0, reader.getTotalLeaderboardSizeProperty());
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
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 1));
    auto reader = LeaderboardReader::CreateInternal(id, 5, 2, cache, true);
    EXPECT_TRUE(reader.getCanPageUpProperty());     // (5-2) >= 0
    auto reader2 = LeaderboardReader::CreateInternal(id, 1, 2, cache, true);
    EXPECT_FALSE(reader2.getCanPageUpProperty());    // (1-2) >= 0 is false
}

TEST(LeaderboardReaderTest, CanPageDownNonFriendBoardByPageStart) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 5));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, cache, false);
    EXPECT_TRUE(reader.getCanPageDownProperty());   // pageStart(0) < cache.size()(1)
}

TEST(LeaderboardReaderTest, CanPageDownNonFriendBoardByRanking) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, -1));
    auto reader = LeaderboardReader::CreateInternal(id, 5, 1, cache, false);
    EXPECT_TRUE(reader.getCanPageDownProperty());   // ranking(-1) < TotalLeaderboardSize(0)
}

TEST(LeaderboardReaderTest, CanPageDownNonFriendBoardFalse) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 5));
    auto reader = LeaderboardReader::CreateInternal(id, 5, 1, cache, false);
    EXPECT_FALSE(reader.getCanPageDownProperty());  // pageStart(5) not < 1, ranking(5) not < 0
}

TEST(LeaderboardReaderTest, CanPageUpNonFriendBoardByPageStart) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 1));
    auto reader = LeaderboardReader::CreateInternal(id, 1, 1, cache, false);
    EXPECT_TRUE(reader.getCanPageUpProperty());     // pageStart(1) > 0
}

TEST(LeaderboardReaderTest, CanPageUpNonFriendBoardByRanking) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::vector<LeaderboardEntry> cache;
    cache.push_back(LeaderboardEntry::CreateInternal(nullptr, 0, 5));
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, cache, false);
    EXPECT_TRUE(reader.getCanPageUpProperty());     // ranking(5) > 1
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

TEST(LeaderboardReaderTest, PageDownThrows) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    EXPECT_THROW(reader.PageDown(), System::NotSupportedException);
}

TEST(LeaderboardReaderTest, BeginEndPageDownThrow) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    EXPECT_THROW(reader.BeginPageDown(System::AsyncCallback{}, std::any{}), System::NotSupportedException);
    EXPECT_THROW(reader.EndPageDown(nullptr), System::NotSupportedException);
}

TEST(LeaderboardReaderTest, PageUpThrows) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    EXPECT_THROW(reader.PageUp(), System::NotSupportedException);
}

TEST(LeaderboardReaderTest, BeginEndPageUpThrow) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    auto reader = LeaderboardReader::CreateInternal(id, 0, 1, {}, false);
    EXPECT_THROW(reader.BeginPageUp(System::AsyncCallback{}, std::any{}), System::NotSupportedException);
    EXPECT_THROW(reader.EndPageUp(nullptr), System::NotSupportedException);
}

TEST(LeaderboardReaderTest, ReadOverloadsThrow) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    EXPECT_THROW(LeaderboardReader::Read(id, 0, 10), System::NotSupportedException);
    EXPECT_THROW(LeaderboardReader::Read(id, nullptr, 10), System::NotSupportedException);
    EXPECT_THROW(LeaderboardReader::Read(id, std::vector<Gamer*>{}, nullptr, 10), System::NotSupportedException);
}

TEST(LeaderboardReaderTest, BeginReadOverloadsThrow) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    EXPECT_THROW(
        LeaderboardReader::BeginRead(id, 0, 10, System::AsyncCallback{}, std::any{}),
        System::NotSupportedException
    );
    EXPECT_THROW(
        LeaderboardReader::BeginRead(id, nullptr, 10, System::AsyncCallback{}, std::any{}),
        System::NotSupportedException
    );
    EXPECT_THROW(
        LeaderboardReader::BeginRead(id, std::vector<Gamer*>{}, nullptr, 10, System::AsyncCallback{}, std::any{}),
        System::NotSupportedException
    );
}

TEST(LeaderboardReaderTest, EndReadThrows) {
    EXPECT_THROW(LeaderboardReader::EndRead(nullptr), System::NotSupportedException);
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
    auto gamer = SignedInGamer::CreateInternal("tag1");
    EXPECT_NO_THROW(gamer.AwardAchievement("some_key"));
}

TEST(SignedInGamerTest, BeginEndAwardAchievement) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    System::IAsyncResult* result = gamer.BeginAwardAchievement("key", System::AsyncCallback{}, std::any{});
    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(result->getIsCompletedProperty());
    gamer.EndAwardAchievement(result);
    delete result;
}

TEST(SignedInGamerTest, GetAchievementsReturnsEmptyCollection) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    auto achievements = gamer.GetAchievements();
    EXPECT_EQ(0, achievements.getCountProperty());
}

TEST(SignedInGamerTest, BeginGetAchievementsTwiceThrows) {
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
