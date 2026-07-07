// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <algorithm>
#include "System/ArgumentException.hpp"
#include "System/NotImplementedException.hpp"
#include "System/IO/MemoryStream.hpp"

#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

// --- PropertyDictionary ---

TEST(PropertyDictionaryTest, CreateEmpty) {
    auto dict = PropertyDictionary::CreateInternal({});
    EXPECT_EQ(0, dict.getCountProperty());
}

TEST(PropertyDictionaryTest, SetAndGetInt) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);
    EXPECT_EQ(42, dict.GetValueInt32("score"));
}

TEST(PropertyDictionaryTest, SetAndGetFloat) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("ratio", 3.14f);
    EXPECT_FLOAT_EQ(3.14f, dict.GetValueSingle("ratio"));
}

TEST(PropertyDictionaryTest, SetAndGetDouble) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("d", 1.23);
    EXPECT_DOUBLE_EQ(1.23, dict.GetValueDouble("d"));
}

TEST(PropertyDictionaryTest, SetAndGetLong) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("ticks", 1000000LL);
    EXPECT_EQ(1000000LL, dict.GetValueInt64("ticks"));
}

TEST(PropertyDictionaryTest, SetAndGetString) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("name", std::string("hello"));
    EXPECT_EQ("hello", dict.GetValueString("name"));
}

TEST(PropertyDictionaryTest, SetAndGetOutcome) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("outcome", LeaderboardOutcome::Win);
    EXPECT_EQ(LeaderboardOutcome::Win, dict.GetValueOutcome("outcome"));
}

TEST(PropertyDictionaryTest, SetAndGetDateTime) {
    auto dict = PropertyDictionary::CreateInternal({});
    System::DateTime dt(2024, 6, 15);
    dict.SetValue("when", dt);
    EXPECT_TRUE(dt == dict.GetValueDateTime("when"));
}

TEST(PropertyDictionaryTest, SetAndGetTimeSpan) {
    auto dict = PropertyDictionary::CreateInternal({});
    System::TimeSpan ts(1, 2, 3);
    dict.SetValue("elapsed", ts);
    EXPECT_TRUE(ts == dict.GetValueTimeSpan("elapsed"));
}

// Task 9.6: unlike every other GetValueX overload, GetValueStream has no matching SetValue
// overload in FNA either (Stream values only ever get in through the generic object indexer) -
// Add() (a Task 8.1 addition, taking a std::any) is this port's own closest equivalent entry
// point for storing an arbitrary-typed value like a Stream pointer.
TEST(PropertyDictionaryTest, SetAndGetStream) {
    auto dict = PropertyDictionary::CreateInternal({});
    System::IO::MemoryStream stream;
    dict.Add("payload", static_cast<System::IO::Stream*>(&stream));
    EXPECT_EQ(&stream, dict.GetValueStream("payload"));
}

TEST(PropertyDictionaryTest, ContainsKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    EXPECT_FALSE(dict.ContainsKey("x"));
    dict.SetValue("x", 1);
    EXPECT_TRUE(dict.ContainsKey("x"));
}

TEST(PropertyDictionaryTest, TryGetValueFound) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("v", 99);
    std::any out;
    EXPECT_TRUE(dict.TryGetValue("v", out));
    EXPECT_EQ(99, std::any_cast<int>(out));
}

TEST(PropertyDictionaryTest, TryGetValueNotFound) {
    auto dict = PropertyDictionary::CreateInternal({});
    std::any out;
    EXPECT_FALSE(dict.TryGetValue("missing", out));
}

TEST(PropertyDictionaryTest, CountIncrementsOnSet) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("a", 1);
    dict.SetValue("b", 2);
    EXPECT_EQ(2, dict.getCountProperty());
}

// Task 7.4: reading a missing key through the non-const operator[] (the common case - most
// callers hold a non-const PropertyDictionary&) used to silently auto-vivify an empty std::any
// entry and inflate Count, instead of throwing like FNA's real Dictionary<TKey,TValue> indexer
// does. Confirmed fixed: throws instead, and leaves Count unchanged.
TEST(PropertyDictionaryTest, MutableIndexerThrowsOnMissingKeyInsteadOfAutoVivifying) {
    PropertyDictionary dict = PropertyDictionary::CreateInternal({});
    EXPECT_THROW((void) dict["missing"], std::out_of_range);
    EXPECT_EQ(0, dict.getCountProperty());
}

TEST(PropertyDictionaryTest, MutableIndexerReadsAndOverwritesExistingKey) {
    PropertyDictionary dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);
    EXPECT_EQ(42, std::any_cast<int>(dict["score"]));
    dict["score"] = 99;
    EXPECT_EQ(99, dict.GetValueInt32("score"));
}

// Task 9.6: the const operator[] overload had no dedicated coverage of its own - only ever
// exercised incidentally through a non-const PropertyDictionary&.
TEST(PropertyDictionaryTest, ConstIndexerReadsExistingKey) {
    PropertyDictionary dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);
    const PropertyDictionary& constDict = dict;
    EXPECT_EQ(42, std::any_cast<int>(constDict["score"]));
}

TEST(PropertyDictionaryTest, ConstIndexerThrowsOnMissingKey) {
    const PropertyDictionary dict = PropertyDictionary::CreateInternal({});
    EXPECT_THROW((void) dict["missing"], std::out_of_range);
}

// Task 8.1: matches Dictionary<TKey,TValue>.Add's real behavior - unlike SetValue/the indexer
// setter, Add() throws for a key that already exists instead of silently overwriting it.
TEST(PropertyDictionaryTest, AddInsertsNewKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add("score", 42);
    EXPECT_EQ(42, dict.GetValueInt32("score"));
    EXPECT_EQ(1, dict.getCountProperty());
}

TEST(PropertyDictionaryTest, AddThrowsOnDuplicateKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add("score", 42);
    EXPECT_THROW(dict.Add("score", 99), System::ArgumentException);
    EXPECT_EQ(42, dict.GetValueInt32("score")); // unchanged
}

TEST(PropertyDictionaryTest, RemoveReturnsTrueAndDeletesExistingKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);
    EXPECT_TRUE(dict.Remove("score"));
    EXPECT_FALSE(dict.ContainsKey("score"));
    EXPECT_EQ(0, dict.getCountProperty());
}

TEST(PropertyDictionaryTest, RemoveReturnsFalseForMissingKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    EXPECT_FALSE(dict.Remove("missing"));
}

TEST(PropertyDictionaryTest, ClearRemovesEverything) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("a", 1);
    dict.SetValue("b", 2);
    dict.Clear();
    EXPECT_EQ(0, dict.getCountProperty());
    EXPECT_FALSE(dict.ContainsKey("a"));
}

TEST(PropertyDictionaryTest, KeysReturnsEveryKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("a", 1);
    dict.SetValue("b", 2);
    auto keys = dict.Keys();
    ASSERT_EQ(2u, keys.size());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "a"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "b"), keys.end());
}

TEST(PropertyDictionaryTest, ValuesReturnsEveryValue) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("a", 1);
    dict.SetValue("b", 2);
    auto values = dict.Values();
    ASSERT_EQ(2u, values.size());
    int sum = std::any_cast<int>(values[0]) + std::any_cast<int>(values[1]);
    EXPECT_EQ(3, sum);
}

// Task 8.1: FNA's own ICollection<KeyValuePair<string,object>>.IsReadOnly getter is hardcoded
// true, despite Add/Remove/Clear all being real, working mutators - a genuine upstream
// inconsistency, preserved faithfully here rather than corrected.
TEST(PropertyDictionaryTest, IsReadOnlyIsAlwaysTrue) {
    auto dict = PropertyDictionary::CreateInternal({});
    EXPECT_TRUE(dict.getIsReadOnlyProperty());
    dict.SetValue("a", 1);
    EXPECT_TRUE(dict.getIsReadOnlyProperty());
}

TEST(PropertyDictionaryTest, CopyToAlwaysThrows) {
    auto dict = PropertyDictionary::CreateInternal({});
    std::vector<std::pair<std::string, std::any>> array;
    EXPECT_THROW(dict.CopyTo(array, 0), System::NotImplementedException);
}

// --- LeaderboardIdentity ---

TEST(LeaderboardIdentityTest, CreateWithKey) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    EXPECT_EQ("BestScoreLifeTime", id.getKeyProperty());
    EXPECT_EQ(0, id.getGameModeProperty());
}

TEST(LeaderboardIdentityTest, CreateWithKeyAndGameMode) {
    auto id = LeaderboardIdentity::Create(LeaderboardKey::BestTimeRecent, 3);
    EXPECT_EQ("BestTimeRecent", id.getKeyProperty());
    EXPECT_EQ(3, id.getGameModeProperty());
}

TEST(LeaderboardIdentityTest, SettersWork) {
    LeaderboardIdentity id;
    id.setKeyProperty("custom");
    id.setGameModeProperty(7);
    EXPECT_EQ("custom", id.getKeyProperty());
    EXPECT_EQ(7, id.getGameModeProperty());
}

// --- GamerPresence ---

TEST(GamerPresenceTest, DefaultMode) {
    auto p = GamerPresence::CreateInternal();
    EXPECT_EQ(GamerPresenceMode::None, p.getPresenceModeProperty());
    EXPECT_EQ(0, p.getPresenceValueProperty());
}

TEST(GamerPresenceTest, SetPresenceMode) {
    auto p = GamerPresence::CreateInternal();
    p.setPresenceModeProperty(GamerPresenceMode::SinglePlayer);
    EXPECT_EQ(GamerPresenceMode::SinglePlayer, p.getPresenceModeProperty());
}

TEST(GamerPresenceTest, SetPresenceValue) {
    auto p = GamerPresence::CreateInternal();
    p.setPresenceValueProperty(5);
    EXPECT_EQ(5, p.getPresenceValueProperty());
}

// --- GamerPrivileges ---

TEST(GamerPrivilegesTest, DefaultsAllPermissive) {
    auto priv = GamerPrivileges::CreateInternal();
    EXPECT_EQ(GamerPrivilegeSetting::Everyone, priv.getAllowCommunicationProperty());
    EXPECT_TRUE(priv.getAllowOnlineSessionsProperty());
    EXPECT_TRUE(priv.getAllowPremiumContentProperty());
    EXPECT_EQ(GamerPrivilegeSetting::Everyone, priv.getAllowProfileViewingProperty());
    EXPECT_TRUE(priv.getAllowPurchaseContentProperty());
    EXPECT_TRUE(priv.getAllowTradeContentProperty());
    EXPECT_EQ(GamerPrivilegeSetting::Everyone, priv.getAllowUserCreatedContentProperty());
}

// --- GameDefaults ---

// Task 7.3: FNA's own internal GameDefaults() constructor is empty, leaving every property at
// C#'s implicit default(T) - the ordinal-0 enum value (GameDifficulty::Easy,
// ControllerSensitivity::Low), not Normal/Medium.
TEST(GameDefaultsTest, DefaultValues) {
    auto d = GameDefaults::CreateInternal();
    EXPECT_EQ(GameDifficulty::Easy, d.getGameDifficultyProperty());
    EXPECT_EQ(ControllerSensitivity::Low, d.getControllerSensitivityProperty());
    EXPECT_FALSE(d.getPrimaryColorProperty().has_value());
    EXPECT_FALSE(d.getSecondaryColorProperty().has_value());
    EXPECT_FALSE(d.getAutoAimProperty());
    EXPECT_FALSE(d.getAutoCenterProperty());
    EXPECT_FALSE(d.getMoveWithRightThumbStickProperty());
    EXPECT_FALSE(d.getInvertYAxisProperty());
    EXPECT_FALSE(d.getManualTransmissionProperty());
    EXPECT_EQ(RacingCameraAngle::Back, d.getRacingCameraAngleProperty());
    EXPECT_FALSE(d.getAccelerateWithButtonsProperty());
    EXPECT_FALSE(d.getBrakeWithButtonsProperty());
}

// --- Achievement ---

TEST(AchievementTest, PropertiesFromCtor) {
    System::DateTime dt;
    auto a = Achievement::CreateInternal("key1", "Name1", "Desc1", true, false, dt);
    EXPECT_EQ("key1",  a.getKeyProperty());
    EXPECT_EQ("Name1", a.getNameProperty());
    EXPECT_EQ("Desc1", a.getDescriptionProperty());
    EXPECT_TRUE(a.getDisplayBeforeEarnedProperty());
    EXPECT_FALSE(a.getIsEarnedProperty());
    EXPECT_TRUE(a.getEarnedOnlineProperty());
    EXPECT_EQ(0, a.getGamerScoreProperty());
    EXPECT_EQ("", a.getHowToEarnProperty());
}

TEST(AchievementTest, GetPictureThrows) {
    System::DateTime dt;
    auto a = Achievement::CreateInternal("k", "n", "d", false, true, dt);
    EXPECT_THROW(a.GetPicture(), System::NotImplementedException);
}
