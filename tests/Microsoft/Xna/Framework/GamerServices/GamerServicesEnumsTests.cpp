// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/ControllerSensitivity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDifficulty.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresenceMode.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeSetting.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerZone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardOutcome.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp"
#include "Microsoft/Xna/Framework/GamerServices/RacingCameraAngle.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

TEST(ControllerSensitivityTest, ValuesExist) {
    EXPECT_EQ(ControllerSensitivity::Low,    ControllerSensitivity::Low);
    EXPECT_EQ(ControllerSensitivity::Medium, ControllerSensitivity::Medium);
    EXPECT_EQ(ControllerSensitivity::High,   ControllerSensitivity::High);
    EXPECT_NE(ControllerSensitivity::Low,    ControllerSensitivity::High);
}

TEST(GameDifficultyTest, ValuesExist) {
    EXPECT_EQ(GameDifficulty::Easy,   GameDifficulty::Easy);
    EXPECT_EQ(GameDifficulty::Normal, GameDifficulty::Normal);
    EXPECT_EQ(GameDifficulty::Hard,   GameDifficulty::Hard);
    EXPECT_NE(GameDifficulty::Easy,   GameDifficulty::Hard);
}

TEST(GamerPresenceModeTest, FirstAndLastValue) {
    EXPECT_EQ(GamerPresenceMode::None,           GamerPresenceMode::None);
    EXPECT_EQ(GamerPresenceMode::CornflowerBlue, GamerPresenceMode::CornflowerBlue);
    EXPECT_NE(GamerPresenceMode::None,           GamerPresenceMode::CornflowerBlue);
}

TEST(GamerPresenceModeTest, RepresentativeValues) {
    EXPECT_EQ(GamerPresenceMode::SinglePlayer,   GamerPresenceMode::SinglePlayer);
    EXPECT_EQ(GamerPresenceMode::OnlineCoOp,     GamerPresenceMode::OnlineCoOp);
    EXPECT_EQ(GamerPresenceMode::WaitingInLobby, GamerPresenceMode::WaitingInLobby);
    EXPECT_NE(GamerPresenceMode::Winning,        GamerPresenceMode::Losing);
}

TEST(GamerPrivilegeSettingTest, ValuesExist) {
    EXPECT_EQ(GamerPrivilegeSetting::Blocked,     GamerPrivilegeSetting::Blocked);
    EXPECT_EQ(GamerPrivilegeSetting::FriendsOnly, GamerPrivilegeSetting::FriendsOnly);
    EXPECT_EQ(GamerPrivilegeSetting::Everyone,    GamerPrivilegeSetting::Everyone);
    EXPECT_NE(GamerPrivilegeSetting::Blocked,     GamerPrivilegeSetting::Everyone);
}

TEST(GamerZoneTest, ValuesExist) {
    EXPECT_EQ(GamerZone::Unknown,    GamerZone::Unknown);
    EXPECT_EQ(GamerZone::Recreation, GamerZone::Recreation);
    EXPECT_EQ(GamerZone::Pro,        GamerZone::Pro);
    EXPECT_EQ(GamerZone::Family,     GamerZone::Family);
    EXPECT_EQ(GamerZone::Underground,GamerZone::Underground);
    EXPECT_NE(GamerZone::Pro,        GamerZone::Family);
}

TEST(LeaderboardKeyTest, ValuesExist) {
    EXPECT_EQ(LeaderboardKey::BestScoreLifeTime, LeaderboardKey::BestScoreLifeTime);
    EXPECT_EQ(LeaderboardKey::BestScoreRecent,   LeaderboardKey::BestScoreRecent);
    EXPECT_EQ(LeaderboardKey::BestTimeLifeTime,  LeaderboardKey::BestTimeLifeTime);
    EXPECT_EQ(LeaderboardKey::BestTimeRecent,    LeaderboardKey::BestTimeRecent);
    EXPECT_NE(LeaderboardKey::BestScoreLifeTime, LeaderboardKey::BestTimeLifeTime);
}

TEST(LeaderboardOutcomeTest, ValuesExist) {
    EXPECT_EQ(LeaderboardOutcome::None, LeaderboardOutcome::None);
    EXPECT_EQ(LeaderboardOutcome::Win,  LeaderboardOutcome::Win);
    EXPECT_EQ(LeaderboardOutcome::Loss, LeaderboardOutcome::Loss);
    EXPECT_EQ(LeaderboardOutcome::Tie,  LeaderboardOutcome::Tie);
    EXPECT_NE(LeaderboardOutcome::Win,  LeaderboardOutcome::Loss);
}

TEST(MessageBoxIconTest, ValuesExist) {
    EXPECT_EQ(MessageBoxIcon::None,    MessageBoxIcon::None);
    EXPECT_EQ(MessageBoxIcon::Error,   MessageBoxIcon::Error);
    EXPECT_EQ(MessageBoxIcon::Warning, MessageBoxIcon::Warning);
    EXPECT_EQ(MessageBoxIcon::Alert,   MessageBoxIcon::Alert);
    EXPECT_NE(MessageBoxIcon::Error,   MessageBoxIcon::Warning);
}

TEST(NotificationPositionTest, ValuesExist) {
    EXPECT_EQ(NotificationPosition::TopLeft,      NotificationPosition::TopLeft);
    EXPECT_EQ(NotificationPosition::TopCenter,    NotificationPosition::TopCenter);
    EXPECT_EQ(NotificationPosition::TopRight,     NotificationPosition::TopRight);
    EXPECT_EQ(NotificationPosition::CenterLeft,   NotificationPosition::CenterLeft);
    EXPECT_EQ(NotificationPosition::Center,       NotificationPosition::Center);
    EXPECT_EQ(NotificationPosition::CenterRight,  NotificationPosition::CenterRight);
    EXPECT_EQ(NotificationPosition::BottomLeft,   NotificationPosition::BottomLeft);
    EXPECT_EQ(NotificationPosition::BottomCenter, NotificationPosition::BottomCenter);
    EXPECT_EQ(NotificationPosition::BottomRight,  NotificationPosition::BottomRight);
    EXPECT_NE(NotificationPosition::TopLeft,      NotificationPosition::BottomRight);
}

TEST(RacingCameraAngleTest, ValuesExist) {
    EXPECT_EQ(RacingCameraAngle::Back,   RacingCameraAngle::Back);
    EXPECT_EQ(RacingCameraAngle::Front,  RacingCameraAngle::Front);
    EXPECT_EQ(RacingCameraAngle::Inside, RacingCameraAngle::Inside);
    EXPECT_NE(RacingCameraAngle::Back,   RacingCameraAngle::Inside);
}
