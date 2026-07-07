// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "System/NotImplementedException.hpp"

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
