// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimationPreset.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarEye.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarEyebrow.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarMouth.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp"
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

TEST(AvatarBodyTypeTest, ValuesExist) {
    EXPECT_EQ(static_cast<int>(AvatarBodyType::Female), 0);
    EXPECT_EQ(static_cast<int>(AvatarBodyType::Male),   1);
    EXPECT_NE(AvatarBodyType::Female, AvatarBodyType::Male);
}

TEST(AvatarRendererStateTest, ValuesExist) {
    EXPECT_EQ(static_cast<int>(AvatarRendererState::Loading),     0);
    EXPECT_EQ(static_cast<int>(AvatarRendererState::Ready),       1);
    EXPECT_EQ(static_cast<int>(AvatarRendererState::Unavailable), 2);
}

TEST(AvatarMouthTest, ValuesExist) {
    EXPECT_EQ(static_cast<int>(AvatarMouth::Neutral),    0);
    EXPECT_EQ(static_cast<int>(AvatarMouth::Sad),        1);
    EXPECT_EQ(static_cast<int>(AvatarMouth::Angry),      2);
    EXPECT_EQ(static_cast<int>(AvatarMouth::Confused),   3);
    EXPECT_EQ(static_cast<int>(AvatarMouth::Laughing),   4);
    EXPECT_EQ(static_cast<int>(AvatarMouth::Shocked),    5);
    EXPECT_EQ(static_cast<int>(AvatarMouth::Happy),      6);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticO),  7);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticAi), 8);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticEe), 9);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticFv), 10);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticW),  11);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticL),  12);
    EXPECT_EQ(static_cast<int>(AvatarMouth::PhoneticDth),13);
}

TEST(AvatarEyeTest, ValuesExist) {
    EXPECT_EQ(static_cast<int>(AvatarEye::Neutral),  0);
    EXPECT_EQ(static_cast<int>(AvatarEye::Sad),      1);
    EXPECT_EQ(static_cast<int>(AvatarEye::Angry),    2);
    EXPECT_EQ(static_cast<int>(AvatarEye::Confused), 3);
    EXPECT_EQ(static_cast<int>(AvatarEye::Laughing), 4);
    EXPECT_EQ(static_cast<int>(AvatarEye::Shocked),  5);
    EXPECT_EQ(static_cast<int>(AvatarEye::Happy),    6);
    EXPECT_EQ(static_cast<int>(AvatarEye::Yawning),  7);
    EXPECT_EQ(static_cast<int>(AvatarEye::Sleeping), 8);
    EXPECT_EQ(static_cast<int>(AvatarEye::LookUp),   9);
    EXPECT_EQ(static_cast<int>(AvatarEye::LookDown), 10);
    EXPECT_EQ(static_cast<int>(AvatarEye::LookLeft), 11);
    EXPECT_EQ(static_cast<int>(AvatarEye::LookRight),12);
    EXPECT_EQ(static_cast<int>(AvatarEye::Blink),    13);
}

TEST(AvatarEyebrowTest, ValuesExist) {
    EXPECT_EQ(static_cast<int>(AvatarEyebrow::Neutral),  0);
    EXPECT_EQ(static_cast<int>(AvatarEyebrow::Sad),      1);
    EXPECT_EQ(static_cast<int>(AvatarEyebrow::Angry),    2);
    EXPECT_EQ(static_cast<int>(AvatarEyebrow::Confused), 3);
    EXPECT_EQ(static_cast<int>(AvatarEyebrow::Raised),   4);
}

TEST(AvatarAnimationPresetTest, FirstAndLastValue) {
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::Stand0),    0);
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::MaleYawn),  30);
    EXPECT_NE(AvatarAnimationPreset::Stand0, AvatarAnimationPreset::MaleYawn);
}

TEST(AvatarAnimationPresetTest, RepresentativeValues) {
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::Clap),                  8);
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::Wave),                  9);
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::Celebrate),             10);
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::FemaleIdleCheckNails),  11);
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::MaleIdleLookAround),    21);
    EXPECT_EQ(static_cast<int>(AvatarAnimationPreset::MaleSurprised),         29);
}

TEST(AvatarBoneTest, RootAndFirstFewValues) {
    EXPECT_EQ(static_cast<int>(AvatarBone::Root),      0);
    EXPECT_EQ(static_cast<int>(AvatarBone::BackLower),  1);
    EXPECT_EQ(static_cast<int>(AvatarBone::HipLeft),    2);
    EXPECT_EQ(static_cast<int>(AvatarBone::HipRight),   3);
    // Gap at 4 (unnamed) - BackUpper is 5, not 4.
    EXPECT_EQ(static_cast<int>(AvatarBone::BackUpper), 5);
}

TEST(AvatarBoneTest, GapsArePreservedExactly) {
    // These explicit values (with gaps at unnamed slots) come directly from the real XNA
    // reference assembly, not a contiguous 0..70 sequence - see AvatarBone.hpp's own doc comment.
    EXPECT_EQ(static_cast<int>(AvatarBone::KneeLeft),    6);
    EXPECT_EQ(static_cast<int>(AvatarBone::KneeRight),   8);
    EXPECT_EQ(static_cast<int>(AvatarBone::AnkleLeft),   11);
    EXPECT_EQ(static_cast<int>(AvatarBone::CollarLeft),  12);
    EXPECT_EQ(static_cast<int>(AvatarBone::Neck),        14);
    EXPECT_EQ(static_cast<int>(AvatarBone::AnkleRight),  15);
    EXPECT_EQ(static_cast<int>(AvatarBone::CollarRight), 16);
    EXPECT_EQ(static_cast<int>(AvatarBone::Head),        19);
}

TEST(AvatarBoneTest, LastValue) {
    EXPECT_EQ(static_cast<int>(AvatarBone::FingerThumb3Right), 70);
}
