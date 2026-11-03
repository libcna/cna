// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include <algorithm>
#include <limits>
#include <functional>
#include "System/InvalidCastException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotImplementedException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"
#include "System/Collections/Generic/KeyValuePair.hpp"
#include "System/IO/MemoryStream.hpp"

#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
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
    // IDictionary.TryGetValue assigns default(object), i.e. null, on a failed lookup.
    std::any out = 123;
    EXPECT_FALSE(dict.TryGetValue("missing", out));
    EXPECT_FALSE(out.has_value());
    EXPECT_EQ(0, dict.getCountProperty());
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
    EXPECT_THROW((void) dict["missing"], System::Collections::Generic::KeyNotFoundException);
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
    EXPECT_THROW((void) dict["missing"], System::Collections::Generic::KeyNotFoundException);
}

TEST(PropertyDictionaryTest, EveryTypedGetterDistinguishesMissingKeysFromWrongBoxedTypes) {
    // CNA's local IDictionary contract (PropertyDictionary.hpp, Add): Windows XNA's
    // GetValue* methods are ProFeatureNotSupported stubs. Dictionary lookup throws
    // KeyNotFoundException; unboxing an incompatible object throws InvalidCastException.
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add("wrong", true); // bool is none of the nine supported getter types.
    const std::vector<std::function<void(const std::string&)>> getters = {
        [&](const auto& key) { (void)dict.GetValueInt32(key); },
        [&](const auto& key) { (void)dict.GetValueInt64(key); },
        [&](const auto& key) { (void)dict.GetValueSingle(key); },
        [&](const auto& key) { (void)dict.GetValueDouble(key); },
        [&](const auto& key) { (void)dict.GetValueString(key); },
        [&](const auto& key) { (void)dict.GetValueOutcome(key); },
        [&](const auto& key) { (void)dict.GetValueDateTime(key); },
        [&](const auto& key) { (void)dict.GetValueTimeSpan(key); },
        [&](const auto& key) { (void)dict.GetValueStream(key); }
    };
    for (std::size_t i = 0; i < getters.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_THROW(getters[i]("missing"), System::Collections::Generic::KeyNotFoundException);
        EXPECT_THROW(getters[i]("wrong"), System::InvalidCastException);
        EXPECT_EQ(1, dict.getCountProperty());
        EXPECT_TRUE(std::any_cast<bool>(std::as_const(dict)["wrong"]));
    }
}

TEST(PropertyDictionaryTest, NumericGettersDoNotCoerceDifferentBoxedNumericTypes) {
    // CLR unboxing requires the exact boxed type, even when the number is representable.
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("number", 7);
    EXPECT_THROW((void)dict.GetValueInt64("number"), System::InvalidCastException);
    EXPECT_THROW((void)dict.GetValueSingle("number"), System::InvalidCastException);
    EXPECT_THROW((void)dict.GetValueDouble("number"), System::InvalidCastException);
    EXPECT_EQ(7, dict.GetValueInt32("number"));
    dict.SetValue("number", 7.0);
    EXPECT_EQ(1, dict.getCountProperty());
    EXPECT_THROW((void)dict.GetValueInt32("number"), System::InvalidCastException);
    EXPECT_DOUBLE_EQ(7.0, dict.GetValueDouble("number"));
}

TEST(PropertyDictionaryTest, CopyToRejectsInvalidRangesBeforeWritingAnyPair) {
    // ICollection.CopyTo validates the offset before capacity; failure leaves the array intact.
    auto dict = PropertyDictionary::CreateInternal({{"score", 42}});
    std::vector<std::pair<std::string, std::any>> destination{{"sentinel", 99}};
    EXPECT_THROW(dict.CopyTo(destination, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(dict.CopyTo(destination, 2), System::ArgumentOutOfRangeException);
    EXPECT_THROW(dict.CopyTo(destination, 1), System::ArgumentException);
    ASSERT_EQ(1u, destination.size());
    EXPECT_EQ("sentinel", destination[0].first);
    EXPECT_EQ(99, std::any_cast<int>(destination[0].second));
    auto empty = PropertyDictionary::CreateInternal({});
    empty.CopyTo(destination, 1); // array.Length is legal for an empty collection.
    EXPECT_EQ("sentinel", destination[0].first);
}

TEST(LeaderboardIdentityTest, EveryNamedKeyAndArbitraryGameModeArePreserved) {
    // XNA LeaderboardIdentity.Create(key, gameMode), IL_000a..0023: Enum.ToString
    // followed by the unvalidated gameMode assignment; Create(key), IL_0000..0007 uses zero.
    const std::vector<std::pair<LeaderboardKey, std::string>> keys = {
        {LeaderboardKey::BestScoreLifeTime, "BestScoreLifeTime"},
        {LeaderboardKey::BestScoreRecent, "BestScoreRecent"},
        {LeaderboardKey::BestTimeLifeTime, "BestTimeLifeTime"},
        {LeaderboardKey::BestTimeRecent, "BestTimeRecent"}
    };
    for (const auto& [key, name] : keys) {
        EXPECT_EQ(name, LeaderboardIdentity::Create(key).getKeyProperty());
        EXPECT_EQ(0, LeaderboardIdentity::Create(key).getGameModeProperty());
        for (int mode : {std::numeric_limits<int>::min(), -1, std::numeric_limits<int>::max()}) {
            const auto id = LeaderboardIdentity::Create(key, mode);
            EXPECT_EQ(name, id.getKeyProperty());
            EXPECT_EQ(mode, id.getGameModeProperty());
        }
    }
}

TEST(LeaderboardIdentityTest, UndefinedKeysUseTheirNumericEnumString) {
    // XNA Create(key, gameMode), IL_000a..0015 boxes the enum and calls ToString:
    // undefined non-flags enum values format as signed decimal, with no range validation.
    for (const auto& [value, text] : std::vector<std::pair<int, std::string>>{
             {-1, "-1"}, {4, "4"}, {std::numeric_limits<int>::min(), "-2147483648"},
             {std::numeric_limits<int>::max(), "2147483647"}}) {
        SCOPED_TRACE(value);
        EXPECT_EQ(text, LeaderboardIdentity::Create(static_cast<LeaderboardKey>(value)).getKeyProperty());
        const auto id = LeaderboardIdentity::Create(static_cast<LeaderboardKey>(value), -7);
        EXPECT_EQ(text, id.getKeyProperty());
        EXPECT_EQ(-7, id.getGameModeProperty());
    }
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

TEST(PropertyDictionaryTest, CopyToCopiesEveryPairFromTheIndex) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("a", 1);
    dict.SetValue("b", std::string("two"));
    std::vector<std::pair<std::string, std::any>> array(3);
    dict.CopyTo(array, 1);
    EXPECT_TRUE(array[0].first.empty());
    EXPECT_EQ("a", array[1].first);
    EXPECT_EQ(1, std::any_cast<int>(array[1].second));
    EXPECT_EQ("b", array[2].first);
    EXPECT_EQ("two", std::any_cast<std::string>(array[2].second));
    EXPECT_THROW(dict.CopyTo(array, 2), System::ArgumentException);
    EXPECT_THROW(dict.CopyTo(array, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(dict.CopyTo(array, 4), System::ArgumentOutOfRangeException);
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

TEST(GamerPresenceTest, ModeAndValuePreserveTheirFullIntegerRangeIndependently) {
    // XNA GamerPresence.set_PresenceMode and set_PresenceValue IL_0000..0017:
    // compare and store, with no enum/range validation and no change to the other property.
    auto presence = GamerPresence::CreateInternal();
    EXPECT_EQ(0, presence.getPresenceValueProperty());
    for (int value : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 0}) {
        presence.setPresenceValueProperty(value);
        presence.setPresenceModeProperty(static_cast<GamerPresenceMode>(-1));
        EXPECT_EQ(value, presence.getPresenceValueProperty());
        EXPECT_EQ(-1, static_cast<int>(presence.getPresenceModeProperty()));
        presence.setPresenceModeProperty(GamerPresenceMode::None);
        EXPECT_EQ(value, presence.getPresenceValueProperty());
        EXPECT_EQ(GamerPresenceMode::None, presence.getPresenceModeProperty());
    }
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
    // XNA IL: a trial never plays online sessions (IsTrialMode is true until gamer services update).
    EXPECT_EQ(!Guide::getIsTrialModeProperty(), priv.getAllowOnlineSessionsProperty());
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
    EXPECT_FALSE(a.getEarnedOnlineProperty());
    EXPECT_EQ(0, a.getGamerScoreProperty());
    EXPECT_EQ("", a.getHowToEarnProperty());
}

// A synthetic value has no authenticated service award to establish online provenance.
TEST(AchievementTest, SyntheticValuesDoNotInventEarnedOnlineProvenance) {
    for (const bool earned : {false, true}) {
        const System::DateTime date(earned ? 1234 : 0);
        const auto value = Achievement::CreateInternal("key", "Name", "Description", true, earned, date);
        EXPECT_EQ(earned, value.getIsEarnedProperty());
        EXPECT_FALSE(value.getEarnedOnlineProperty());
        EXPECT_EQ(date, value.getEarnedDateTimeProperty());
    }
}

// An achievement without a picture reports that as the service path does, as documented.
TEST(AchievementTest, GetPictureThrows) {
    System::DateTime dt;
    auto a = Achievement::CreateInternal("k", "n", "d", false, true, dt);
    EXPECT_THROW(a.GetPicture(), GamerServicesNotAvailableException);
}

// ---------------------------------------------------------------------------
// XNA-MISSING-018: the ICollection<KeyValuePair<string,object>> pair operations.
//
// Microsoft's shipped Windows PropertyDictionary throws NotSupportedException from every member,
// including Count, the indexer and each GetValue* -- the whole type is a "Pro feature" stub there
// because the live property store was an Xbox 360 service
// (xna4-decomp/.../Microsoft.Xna.Framework.GamerServices/PropertyDictionary.cs). CNA instead
// implements the documented dictionary contract locally, which is what makes the type usable, so
// the three pair operations follow that same local contract: .NET's own
// ICollection<KeyValuePair<...>> semantics over a Dictionary, where Contains and Remove match on
// key *and* value.
// ---------------------------------------------------------------------------

namespace {
    using PropertyPair = System::Collections::Generic::KeyValuePair<std::string, std::any>;
}

TEST(PropertyDictionaryTest, PairAddInsertsTheKeyAndValue) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add(PropertyPair("score", std::any(42)));

    EXPECT_EQ(dict.getCountProperty(), 1);
    EXPECT_TRUE(dict.ContainsKey("score"));
    EXPECT_EQ(dict.GetValueInt32("score"), 42);
}

TEST(PropertyDictionaryTest, PairAddRejectsADuplicateKey) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add(PropertyPair("score", std::any(1)));

    // Same as Add(key, value): a duplicate key throws rather than overwriting.
    EXPECT_THROW(dict.Add(PropertyPair("score", std::any(2))), System::ArgumentException);
    EXPECT_THROW(dict.Add(PropertyPair("score", std::any(1))), System::ArgumentException);
    EXPECT_EQ(dict.GetValueInt32("score"), 1);
}

TEST(PropertyDictionaryTest, PairContainsMatchesKeyAndValue) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);
    dict.SetValue("name", std::string("Ada"));

    EXPECT_TRUE(dict.Contains(PropertyPair("score", std::any(42))));
    EXPECT_TRUE(dict.Contains(PropertyPair("name", std::any(std::string("Ada")))));

    // A present key with a different value is not a match -- this is what separates Contains
    // from ContainsKey.
    EXPECT_TRUE(dict.ContainsKey("score"));
    EXPECT_FALSE(dict.Contains(PropertyPair("score", std::any(43))));
    EXPECT_FALSE(dict.Contains(PropertyPair("name", std::any(std::string("Grace")))));

    // An absent key never matches, whatever the value.
    EXPECT_FALSE(dict.Contains(PropertyPair("missing", std::any(42))));
}

TEST(PropertyDictionaryTest, PairContainsSeparatesValueTypes) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);

    // Boxed values of different types are never equal, as in the CLR: the stored Int32 42 does
    // not match a boxed Int64, Single or Double 42.
    EXPECT_FALSE(dict.Contains(PropertyPair("score", std::any(static_cast<long long>(42)))));
    EXPECT_FALSE(dict.Contains(PropertyPair("score", std::any(42.0f))));
    EXPECT_FALSE(dict.Contains(PropertyPair("score", std::any(42.0))));
    EXPECT_TRUE(dict.Contains(PropertyPair("score", std::any(42))));
}

TEST(PropertyDictionaryTest, FloatingPairsUseDotNetEquals) {
    const auto check = []<typename T>()
    {
        SCOPED_TRACE(sizeof(T));
        const T nan = std::numeric_limits<T>::quiet_NaN();
        const T infinity = std::numeric_limits<T>::infinity();
        auto dict = PropertyDictionary::CreateInternal({});
        dict.SetValue("nan", nan);
        EXPECT_TRUE(dict.Contains(PropertyPair("nan", std::any(nan))));
        EXPECT_TRUE(dict.Contains(PropertyPair("nan", std::any(-nan))));
        EXPECT_FALSE(dict.Contains(PropertyPair("nan", std::any(T{0}))));
        EXPECT_FALSE(dict.Contains(PropertyPair("nan", std::any(infinity))));
        EXPECT_FALSE(dict.Remove(PropertyPair("nan", std::any(T{1}))));
        EXPECT_TRUE(dict.ContainsKey("nan"));
        EXPECT_TRUE(dict.Remove(PropertyPair("nan", std::any(-nan))));
        EXPECT_FALSE(dict.ContainsKey("nan"));

        dict.SetValue("zero", T{0});
        EXPECT_TRUE(dict.Contains(PropertyPair("zero", std::any(-T{0}))));
        EXPECT_TRUE(dict.Remove(PropertyPair("zero", std::any(-T{0}))));
        dict.SetValue("infinity", infinity);
        EXPECT_TRUE(dict.Contains(PropertyPair("infinity", std::any(infinity))));
        EXPECT_FALSE(dict.Contains(PropertyPair("infinity", std::any(-infinity))));
        EXPECT_FALSE(dict.Contains(PropertyPair("infinity", std::any(nan))));
        EXPECT_TRUE(dict.Remove(PropertyPair("infinity", std::any(infinity))));
    };
    check.operator()<float>();
    check.operator()<double>();

    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("single", std::numeric_limits<float>::quiet_NaN());
    EXPECT_FALSE(dict.Contains(PropertyPair("single", std::any(std::numeric_limits<double>::quiet_NaN()))));
    EXPECT_FALSE(dict.Remove(PropertyPair("single", std::any(std::numeric_limits<double>::quiet_NaN()))));
    EXPECT_TRUE(dict.ContainsKey("single"));
}

TEST(PropertyDictionaryTest, PairContainsHandlesEveryDefinedValueType) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("i32", 7);
    dict.SetValue("i64", static_cast<long long>(8));
    dict.SetValue("single", 1.5f);
    dict.SetValue("double", 2.5);
    dict.SetValue("text", std::string("Ada"));
    dict.SetValue("when", System::DateTime(2026, 9, 20));
    dict.SetValue("span", System::TimeSpan::FromSeconds(30));
    dict.SetValue("outcome", LeaderboardOutcome::Win);

    EXPECT_TRUE(dict.Contains(PropertyPair("i32", std::any(7))));
    EXPECT_TRUE(dict.Contains(PropertyPair("i64", std::any(static_cast<long long>(8)))));
    EXPECT_TRUE(dict.Contains(PropertyPair("single", std::any(1.5f))));
    EXPECT_TRUE(dict.Contains(PropertyPair("double", std::any(2.5))));
    EXPECT_TRUE(dict.Contains(PropertyPair("text", std::any(std::string("Ada")))));
    EXPECT_TRUE(dict.Contains(PropertyPair("when", std::any(System::DateTime(2026, 9, 20)))));
    EXPECT_TRUE(dict.Contains(PropertyPair("span", std::any(System::TimeSpan::FromSeconds(30)))));
    EXPECT_TRUE(dict.Contains(PropertyPair("outcome", std::any(LeaderboardOutcome::Win))));

    EXPECT_FALSE(dict.Contains(PropertyPair("i64", std::any(static_cast<long long>(9)))));
    EXPECT_FALSE(dict.Contains(PropertyPair("outcome", std::any(LeaderboardOutcome::Loss))));
}

TEST(PropertyDictionaryTest, PairContainsRefusesAnUncomparableBoxedType) {
    // Add(key, std::any) accepts any boxed type, but C++ has no Object.Equals to compare an
    // arbitrary one with, so the comparison is refused rather than reported as unequal.
    struct Opaque { int value; };
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add("opaque", std::any(Opaque{1}));

    EXPECT_THROW((void)dict.Contains(PropertyPair("opaque", std::any(Opaque{1}))),
                 System::NotSupportedException);

    // A different type on either side is still decided without comparing: types differ.
    EXPECT_FALSE(dict.Contains(PropertyPair("opaque", std::any(1))));
}

TEST(PropertyDictionaryTest, PairContainsTreatsEmptyValuesAsNullReferences) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.Add("nothing", std::any());

    EXPECT_TRUE(dict.Contains(PropertyPair("nothing", std::any())));
    EXPECT_FALSE(dict.Contains(PropertyPair("nothing", std::any(0))));
}

TEST(PropertyDictionaryTest, PairRemoveOnlyRemovesAFullMatch) {
    auto dict = PropertyDictionary::CreateInternal({});
    dict.SetValue("score", 42);
    dict.SetValue("name", std::string("Ada"));

    // A matching key with the wrong value leaves the entry alone.
    EXPECT_FALSE(dict.Remove(PropertyPair("score", std::any(43))));
    EXPECT_EQ(dict.getCountProperty(), 2);
    EXPECT_TRUE(dict.ContainsKey("score"));

    EXPECT_TRUE(dict.Remove(PropertyPair("score", std::any(42))));
    EXPECT_EQ(dict.getCountProperty(), 1);
    EXPECT_FALSE(dict.ContainsKey("score"));

    // Removing it again finds nothing.
    EXPECT_FALSE(dict.Remove(PropertyPair("score", std::any(42))));
    EXPECT_EQ(dict.getCountProperty(), 1);

    // An absent key is simply not found.
    EXPECT_FALSE(dict.Remove(PropertyPair("missing", std::any(0))));
    EXPECT_EQ(dict.getCountProperty(), 1);
}

TEST(PropertyDictionaryTest, PairOperationsRoundTrip) {
    auto dict = PropertyDictionary::CreateInternal({});
    const PropertyPair pair("level", std::any(std::string("boss")));

    EXPECT_FALSE(dict.Contains(pair));
    dict.Add(pair);
    EXPECT_TRUE(dict.Contains(pair));
    EXPECT_TRUE(dict.Remove(pair));
    EXPECT_FALSE(dict.Contains(pair));
    EXPECT_EQ(dict.getCountProperty(), 0);
}
