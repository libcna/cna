// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GAMER_SERVICES_H
#define CNA_C_GAMER_SERVICES_H

#include "CNA/C/input.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The gamer and guide identities. Each is a fixed-width `uint32_t` with one macro per canonical
   value at its canonical ordinal and a `_MAXIMUM`, so an identity survives a canonical enum gaining
   a value at the end without moving anything a C caller already compiled against. */

/** @brief Fixed-width identity for what a gamer is currently doing. */
typedef uint32_t CNA_GamerPresenceMode;

/** @brief No presence mode set. */
#define CNA_GAMER_PRESENCE_MODE_NONE UINT32_C(0)
/** @brief Playing in single-player mode. */
#define CNA_GAMER_PRESENCE_MODE_SINGLE_PLAYER UINT32_C(1)
/** @brief Playing in multiplayer mode. */
#define CNA_GAMER_PRESENCE_MODE_MULTIPLAYER UINT32_C(2)
/** @brief Playing local co-op. */
#define CNA_GAMER_PRESENCE_MODE_LOCAL_CO_OP UINT32_C(3)
/** @brief Playing local versus. */
#define CNA_GAMER_PRESENCE_MODE_LOCAL_VERSUS UINT32_C(4)
/** @brief Playing online co-op. */
#define CNA_GAMER_PRESENCE_MODE_ONLINE_CO_OP UINT32_C(5)
/** @brief Playing online versus. */
#define CNA_GAMER_PRESENCE_MODE_ONLINE_VERSUS UINT32_C(6)
/** @brief Playing against the computer. */
#define CNA_GAMER_PRESENCE_MODE_VERSUS_COMPUTER UINT32_C(7)
/** @brief On a particular stage. */
#define CNA_GAMER_PRESENCE_MODE_STAGE UINT32_C(8)
/** @brief On a particular level. */
#define CNA_GAMER_PRESENCE_MODE_LEVEL UINT32_C(9)
/** @brief Playing a co-op stage. */
#define CNA_GAMER_PRESENCE_MODE_CO_OP_STAGE UINT32_C(10)
/** @brief Playing a co-op level. */
#define CNA_GAMER_PRESENCE_MODE_CO_OP_LEVEL UINT32_C(11)
/** @brief Playing in arcade mode. */
#define CNA_GAMER_PRESENCE_MODE_ARCADE_MODE UINT32_C(12)
/** @brief Playing in campaign mode. */
#define CNA_GAMER_PRESENCE_MODE_CAMPAIGN_MODE UINT32_C(13)
/** @brief Playing in challenge mode. */
#define CNA_GAMER_PRESENCE_MODE_CHALLENGE_MODE UINT32_C(14)
/** @brief Playing in exploration mode. */
#define CNA_GAMER_PRESENCE_MODE_EXPLORATION_MODE UINT32_C(15)
/** @brief Playing in practice mode. */
#define CNA_GAMER_PRESENCE_MODE_PRACTICE_MODE UINT32_C(16)
/** @brief Playing in puzzle mode. */
#define CNA_GAMER_PRESENCE_MODE_PUZZLE_MODE UINT32_C(17)
/** @brief Playing in scenario mode. */
#define CNA_GAMER_PRESENCE_MODE_SCENARIO_MODE UINT32_C(18)
/** @brief Playing in story mode. */
#define CNA_GAMER_PRESENCE_MODE_STORY_MODE UINT32_C(19)
/** @brief Playing in survival mode. */
#define CNA_GAMER_PRESENCE_MODE_SURVIVAL_MODE UINT32_C(20)
/** @brief Playing in tutorial mode. */
#define CNA_GAMER_PRESENCE_MODE_TUTORIAL_MODE UINT32_C(21)
/** @brief Difficulty set to easy. */
#define CNA_GAMER_PRESENCE_MODE_DIFFICULTY_EASY UINT32_C(22)
/** @brief Difficulty set to medium. */
#define CNA_GAMER_PRESENCE_MODE_DIFFICULTY_MEDIUM UINT32_C(23)
/** @brief Difficulty set to hard. */
#define CNA_GAMER_PRESENCE_MODE_DIFFICULTY_HARD UINT32_C(24)
/** @brief Difficulty set to extreme. */
#define CNA_GAMER_PRESENCE_MODE_DIFFICULTY_EXTREME UINT32_C(25)
/** @brief Displaying a score. */
#define CNA_GAMER_PRESENCE_MODE_SCORE UINT32_C(26)
/** @brief Displaying a versus score. */
#define CNA_GAMER_PRESENCE_MODE_VERSUS_SCORE UINT32_C(27)
/** @brief Currently winning. */
#define CNA_GAMER_PRESENCE_MODE_WINNING UINT32_C(28)
/** @brief Currently losing. */
#define CNA_GAMER_PRESENCE_MODE_LOSING UINT32_C(29)
/** @brief Score is tied. */
#define CNA_GAMER_PRESENCE_MODE_SCORE_IS_TIED UINT32_C(30)
/** @brief Outnumbered by opponents. */
#define CNA_GAMER_PRESENCE_MODE_OUTNUMBERED UINT32_C(31)
/** @brief On a roll. */
#define CNA_GAMER_PRESENCE_MODE_ON_A_ROLL UINT32_C(32)
/** @brief In combat. */
#define CNA_GAMER_PRESENCE_MODE_IN_COMBAT UINT32_C(33)
/** @brief Battling a boss. */
#define CNA_GAMER_PRESENCE_MODE_BATTLING_BOSS UINT32_C(34)
/** @brief In time attack mode. */
#define CNA_GAMER_PRESENCE_MODE_TIME_ATTACK UINT32_C(35)
/** @brief Trying for a record. */
#define CNA_GAMER_PRESENCE_MODE_TRYING_FOR_RECORD UINT32_C(36)
/** @brief In free play. */
#define CNA_GAMER_PRESENCE_MODE_FREE_PLAY UINT32_C(37)
/** @brief Wasting time. */
#define CNA_GAMER_PRESENCE_MODE_WASTING_TIME UINT32_C(38)
/** @brief Stuck on a hard section. */
#define CNA_GAMER_PRESENCE_MODE_STUCK_ON_A_HARD_BIT UINT32_C(39)
/** @brief Nearly finished. */
#define CNA_GAMER_PRESENCE_MODE_NEARLY_FINISHED UINT32_C(40)
/** @brief Looking for games. */
#define CNA_GAMER_PRESENCE_MODE_LOOKING_FOR_GAMES UINT32_C(41)
/** @brief Waiting for players. */
#define CNA_GAMER_PRESENCE_MODE_WAITING_FOR_PLAYERS UINT32_C(42)
/** @brief Waiting in the lobby. */
#define CNA_GAMER_PRESENCE_MODE_WAITING_IN_LOBBY UINT32_C(43)
/** @brief Setting up a match. */
#define CNA_GAMER_PRESENCE_MODE_SETTING_UP_MATCH UINT32_C(44)
/** @brief Playing with friends. */
#define CNA_GAMER_PRESENCE_MODE_PLAYING_WITH_FRIENDS UINT32_C(45)
/** @brief At the main menu. */
#define CNA_GAMER_PRESENCE_MODE_AT_MENU UINT32_C(46)
/** @brief Starting a game. */
#define CNA_GAMER_PRESENCE_MODE_STARTING_GAME UINT32_C(47)
/** @brief Game is paused. */
#define CNA_GAMER_PRESENCE_MODE_PAUSED UINT32_C(48)
/** @brief Game over. */
#define CNA_GAMER_PRESENCE_MODE_GAME_OVER UINT32_C(49)
/** @brief Won the game. */
#define CNA_GAMER_PRESENCE_MODE_WON_THE_GAME UINT32_C(50)
/** @brief Configuring settings. */
#define CNA_GAMER_PRESENCE_MODE_CONFIGURING_SETTINGS UINT32_C(51)
/** @brief Customizing their player. */
#define CNA_GAMER_PRESENCE_MODE_CUSTOMIZING_PLAYER UINT32_C(52)
/** @brief Editing a level. */
#define CNA_GAMER_PRESENCE_MODE_EDITING_LEVEL UINT32_C(53)
/** @brief Browsing the in-game store. */
#define CNA_GAMER_PRESENCE_MODE_IN_GAME_STORE UINT32_C(54)
/** @brief Watching a cutscene. */
#define CNA_GAMER_PRESENCE_MODE_WATCHING_CUTSCENE UINT32_C(55)
/** @brief Watching the credits. */
#define CNA_GAMER_PRESENCE_MODE_WATCHING_CREDITS UINT32_C(56)
/** @brief Playing a minigame. */
#define CNA_GAMER_PRESENCE_MODE_PLAYING_MINIGAME UINT32_C(57)
/** @brief Found a secret. */
#define CNA_GAMER_PRESENCE_MODE_FOUND_SECRET UINT32_C(58)
/** @brief Cornflower blue. */
#define CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE UINT32_C(59)
/** @brief Highest defined `CNA_GamerPresenceMode` identity. */
#define CNA_GAMER_PRESENCE_MODE_MAXIMUM CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE

/** @brief Fixed-width identity for where the guide draws its notifications. */
typedef uint32_t CNA_NotificationPosition;

/** @brief Top-left corner. */
#define CNA_NOTIFICATION_POSITION_TOP_LEFT UINT32_C(0)
/** @brief Top-center. */
#define CNA_NOTIFICATION_POSITION_TOP_CENTER UINT32_C(1)
/** @brief Top-right corner. */
#define CNA_NOTIFICATION_POSITION_TOP_RIGHT UINT32_C(2)
/** @brief Center-left. */
#define CNA_NOTIFICATION_POSITION_CENTER_LEFT UINT32_C(3)
/** @brief Screen center. */
#define CNA_NOTIFICATION_POSITION_CENTER UINT32_C(4)
/** @brief Center-right. */
#define CNA_NOTIFICATION_POSITION_CENTER_RIGHT UINT32_C(5)
/** @brief Bottom-left corner. */
#define CNA_NOTIFICATION_POSITION_BOTTOM_LEFT UINT32_C(6)
/** @brief Bottom-center. */
#define CNA_NOTIFICATION_POSITION_BOTTOM_CENTER UINT32_C(7)
/** @brief Bottom-right corner. */
#define CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT UINT32_C(8)
/** @brief Highest defined `CNA_NotificationPosition` identity. */
#define CNA_NOTIFICATION_POSITION_MAXIMUM CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT

/** @brief Fixed-width identity for the zone a gamer plays in. */
typedef uint32_t CNA_GamerZone;

/** @brief Zone is unknown. */
#define CNA_GAMER_ZONE_UNKNOWN UINT32_C(0)
/** @brief Recreation zone — casual gaming. */
#define CNA_GAMER_ZONE_RECREATION UINT32_C(1)
/** @brief Pro zone — competitive gaming. */
#define CNA_GAMER_ZONE_PRO UINT32_C(2)
/** @brief Family zone — family-friendly gaming. */
#define CNA_GAMER_ZONE_FAMILY UINT32_C(3)
/** @brief Underground zone — unrated content. */
#define CNA_GAMER_ZONE_UNDERGROUND UINT32_C(4)
/** @brief Highest defined `CNA_GamerZone` identity. */
#define CNA_GAMER_ZONE_MAXIMUM CNA_GAMER_ZONE_UNDERGROUND

/** @brief Fixed-width identity for which leaderboard a read or write addresses. */
typedef uint32_t CNA_LeaderboardKey;

/** @brief Best score over the lifetime of the title. */
#define CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME UINT32_C(0)
/** @brief Best score over a recent time window. */
#define CNA_LEADERBOARD_KEY_BEST_SCORE_RECENT UINT32_C(1)
/** @brief Best time over the lifetime of the title. */
#define CNA_LEADERBOARD_KEY_BEST_TIME_LIFE_TIME UINT32_C(2)
/** @brief Best time over a recent time window. */
#define CNA_LEADERBOARD_KEY_BEST_TIME_RECENT UINT32_C(3)
/** @brief Highest defined `CNA_LeaderboardKey` identity. */
#define CNA_LEADERBOARD_KEY_MAXIMUM CNA_LEADERBOARD_KEY_BEST_TIME_RECENT

/** @brief Fixed-width identity for how a session ended for one gamer. */
typedef uint32_t CNA_LeaderboardOutcome;

/** @brief No outcome recorded. */
#define CNA_LEADERBOARD_OUTCOME_NONE UINT32_C(0)
/** @brief The player won the match. */
#define CNA_LEADERBOARD_OUTCOME_WIN UINT32_C(1)
/** @brief The player lost the match. */
#define CNA_LEADERBOARD_OUTCOME_LOSS UINT32_C(2)
/** @brief The match ended in a tie. */
#define CNA_LEADERBOARD_OUTCOME_TIE UINT32_C(3)
/** @brief Highest defined `CNA_LeaderboardOutcome` identity. */
#define CNA_LEADERBOARD_OUTCOME_MAXIMUM CNA_LEADERBOARD_OUTCOME_TIE

/** @brief Fixed-width identity for the icon a guide message box shows. */
typedef uint32_t CNA_MessageBoxIcon;

/** @brief No icon. */
#define CNA_MESSAGE_BOX_ICON_NONE UINT32_C(0)
/** @brief Error icon. */
#define CNA_MESSAGE_BOX_ICON_ERROR UINT32_C(1)
/** @brief Warning icon. */
#define CNA_MESSAGE_BOX_ICON_WARNING UINT32_C(2)
/** @brief Alert icon. */
#define CNA_MESSAGE_BOX_ICON_ALERT UINT32_C(3)
/** @brief Highest defined `CNA_MessageBoxIcon` identity. */
#define CNA_MESSAGE_BOX_ICON_MAXIMUM CNA_MESSAGE_BOX_ICON_ALERT

/** @brief Fixed-width identity for a game-defaults controller sensitivity. */
typedef uint32_t CNA_ControllerSensitivity;

/** @brief Low sensitivity. */
#define CNA_CONTROLLER_SENSITIVITY_LOW UINT32_C(0)
/** @brief Medium sensitivity. */
#define CNA_CONTROLLER_SENSITIVITY_MEDIUM UINT32_C(1)
/** @brief High sensitivity. */
#define CNA_CONTROLLER_SENSITIVITY_HIGH UINT32_C(2)
/** @brief Highest defined `CNA_ControllerSensitivity` identity. */
#define CNA_CONTROLLER_SENSITIVITY_MAXIMUM CNA_CONTROLLER_SENSITIVITY_HIGH

/** @brief Fixed-width identity for a game-defaults difficulty. */
typedef uint32_t CNA_GameDifficulty;

/** @brief Easy difficulty. */
#define CNA_GAME_DIFFICULTY_EASY UINT32_C(0)
/** @brief Normal difficulty. */
#define CNA_GAME_DIFFICULTY_NORMAL UINT32_C(1)
/** @brief Hard difficulty. */
#define CNA_GAME_DIFFICULTY_HARD UINT32_C(2)
/** @brief Highest defined `CNA_GameDifficulty` identity. */
#define CNA_GAME_DIFFICULTY_MAXIMUM CNA_GAME_DIFFICULTY_HARD

/** @brief Fixed-width identity for how widely a privilege is granted. */
typedef uint32_t CNA_GamerPrivilegeSetting;

/** @brief Access is blocked for everyone. */
#define CNA_GAMER_PRIVILEGE_SETTING_BLOCKED UINT32_C(0)
/** @brief Access is allowed only for friends. */
#define CNA_GAMER_PRIVILEGE_SETTING_FRIENDS_ONLY UINT32_C(1)
/** @brief Access is allowed for everyone. */
#define CNA_GAMER_PRIVILEGE_SETTING_EVERYONE UINT32_C(2)
/** @brief Highest defined `CNA_GamerPrivilegeSetting` identity. */
#define CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM CNA_GAMER_PRIVILEGE_SETTING_EVERYONE

/** @brief Fixed-width identity for a game-defaults racing camera angle. */
typedef uint32_t CNA_RacingCameraAngle;

/** @brief Behind-the-car (chase) camera. */
#define CNA_RACING_CAMERA_ANGLE_BACK UINT32_C(0)
/** @brief Front-mounted camera. */
#define CNA_RACING_CAMERA_ANGLE_FRONT UINT32_C(1)
/** @brief Inside-the-cockpit camera. */
#define CNA_RACING_CAMERA_ANGLE_INSIDE UINT32_C(2)
/** @brief Highest defined `CNA_RacingCameraAngle` identity. */
#define CNA_RACING_CAMERA_ANGLE_MAXIMUM CNA_RACING_CAMERA_ANGLE_INSIDE

/** @brief Fixed-width identity for an avatar body type. */
typedef uint32_t CNA_AvatarBodyType;

/** @brief A female body type. */
#define CNA_AVATAR_BODY_TYPE_FEMALE UINT32_C(0)
/** @brief A male body type. */
#define CNA_AVATAR_BODY_TYPE_MALE UINT32_C(1)
/** @brief Highest defined `CNA_AvatarBodyType` identity. */
#define CNA_AVATAR_BODY_TYPE_MAXIMUM CNA_AVATAR_BODY_TYPE_MALE

/** @brief Fixed-width identity for whether an avatar renderer is ready to draw. */
typedef uint32_t CNA_AvatarRendererState;

/** @brief The avatar's assets are loading. */
#define CNA_AVATAR_RENDERER_STATE_LOADING UINT32_C(0)
/** @brief The avatar is ready to render. */
#define CNA_AVATAR_RENDERER_STATE_READY UINT32_C(1)
/** @brief The avatar is unavailable. */
#define CNA_AVATAR_RENDERER_STATE_UNAVAILABLE UINT32_C(2)
/** @brief Highest defined `CNA_AvatarRendererState` identity. */
#define CNA_AVATAR_RENDERER_STATE_MAXIMUM CNA_AVATAR_RENDERER_STATE_UNAVAILABLE

/** @brief Fixed-width identity for an avatar eyebrow expression. */
typedef uint32_t CNA_AvatarEyebrow;

/** @brief A neutral eyebrow shape. */
#define CNA_AVATAR_EYEBROW_NEUTRAL UINT32_C(0)
/** @brief A sad eyebrow shape. */
#define CNA_AVATAR_EYEBROW_SAD UINT32_C(1)
/** @brief An angry eyebrow shape. */
#define CNA_AVATAR_EYEBROW_ANGRY UINT32_C(2)
/** @brief A confused eyebrow shape. */
#define CNA_AVATAR_EYEBROW_CONFUSED UINT32_C(3)
/** @brief A raised eyebrow shape. */
#define CNA_AVATAR_EYEBROW_RAISED UINT32_C(4)
/** @brief Highest defined `CNA_AvatarEyebrow` identity. */
#define CNA_AVATAR_EYEBROW_MAXIMUM CNA_AVATAR_EYEBROW_RAISED

/** @brief Fixed-width identity for an avatar eye expression. */
typedef uint32_t CNA_AvatarEye;

/** @brief A neutral eye shape. */
#define CNA_AVATAR_EYE_NEUTRAL UINT32_C(0)
/** @brief A sad eye shape. */
#define CNA_AVATAR_EYE_SAD UINT32_C(1)
/** @brief An angry eye shape. */
#define CNA_AVATAR_EYE_ANGRY UINT32_C(2)
/** @brief A confused eye shape. */
#define CNA_AVATAR_EYE_CONFUSED UINT32_C(3)
/** @brief A laughing eye shape. */
#define CNA_AVATAR_EYE_LAUGHING UINT32_C(4)
/** @brief A shocked eye shape. */
#define CNA_AVATAR_EYE_SHOCKED UINT32_C(5)
/** @brief A happy eye shape. */
#define CNA_AVATAR_EYE_HAPPY UINT32_C(6)
/** @brief A yawning eye shape. */
#define CNA_AVATAR_EYE_YAWNING UINT32_C(7)
/** @brief A sleeping eye shape. */
#define CNA_AVATAR_EYE_SLEEPING UINT32_C(8)
/** @brief Looking up. */
#define CNA_AVATAR_EYE_LOOK_UP UINT32_C(9)
/** @brief Looking down. */
#define CNA_AVATAR_EYE_LOOK_DOWN UINT32_C(10)
/** @brief Looking left. */
#define CNA_AVATAR_EYE_LOOK_LEFT UINT32_C(11)
/** @brief Looking right. */
#define CNA_AVATAR_EYE_LOOK_RIGHT UINT32_C(12)
/** @brief Blinking. */
#define CNA_AVATAR_EYE_BLINK UINT32_C(13)
/** @brief Highest defined `CNA_AvatarEye` identity. */
#define CNA_AVATAR_EYE_MAXIMUM CNA_AVATAR_EYE_BLINK

/** @brief Fixed-width identity for an avatar mouth expression. */
typedef uint32_t CNA_AvatarMouth;

/** @brief A neutral mouth shape. */
#define CNA_AVATAR_MOUTH_NEUTRAL UINT32_C(0)
/** @brief A sad mouth shape. */
#define CNA_AVATAR_MOUTH_SAD UINT32_C(1)
/** @brief An angry mouth shape. */
#define CNA_AVATAR_MOUTH_ANGRY UINT32_C(2)
/** @brief A confused mouth shape. */
#define CNA_AVATAR_MOUTH_CONFUSED UINT32_C(3)
/** @brief A laughing mouth shape. */
#define CNA_AVATAR_MOUTH_LAUGHING UINT32_C(4)
/** @brief A shocked mouth shape. */
#define CNA_AVATAR_MOUTH_SHOCKED UINT32_C(5)
/** @brief A happy mouth shape. */
#define CNA_AVATAR_MOUTH_HAPPY UINT32_C(6)
/** @brief The phonetic "o" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_O UINT32_C(7)
/** @brief The phonetic "ai" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_AI UINT32_C(8)
/** @brief The phonetic "ee" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_EE UINT32_C(9)
/** @brief The phonetic "f"/"v" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_FV UINT32_C(10)
/** @brief The phonetic "w" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_W UINT32_C(11)
/** @brief The phonetic "l" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_L UINT32_C(12)
/** @brief The phonetic "th" mouth shape. */
#define CNA_AVATAR_MOUTH_PHONETIC_DTH UINT32_C(13)
/** @brief Highest defined `CNA_AvatarMouth` identity. */
#define CNA_AVATAR_MOUTH_MAXIMUM CNA_AVATAR_MOUTH_PHONETIC_DTH

/** @brief Fixed-width identity for a built-in avatar animation. */
typedef uint32_t CNA_AvatarAnimationPreset;

/** @brief Standing idle animation, variant 0. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_0 UINT32_C(0)
/** @brief Standing idle animation, variant 1. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_1 UINT32_C(1)
/** @brief Standing idle animation, variant 2. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_2 UINT32_C(2)
/** @brief Standing idle animation, variant 3. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_3 UINT32_C(3)
/** @brief Standing idle animation, variant 4. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_4 UINT32_C(4)
/** @brief Standing idle animation, variant 5. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_5 UINT32_C(5)
/** @brief Standing idle animation, variant 6. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_6 UINT32_C(6)
/** @brief Standing idle animation, variant 7. */
#define CNA_AVATAR_ANIMATION_PRESET_STAND_7 UINT32_C(7)
/** @brief Clapping animation. */
#define CNA_AVATAR_ANIMATION_PRESET_CLAP UINT32_C(8)
/** @brief Waving animation. */
#define CNA_AVATAR_ANIMATION_PRESET_WAVE UINT32_C(9)
/** @brief Celebrating animation. */
#define CNA_AVATAR_ANIMATION_PRESET_CELEBRATE UINT32_C(10)
/** @brief Female idle: checking nails. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_CHECK_NAILS UINT32_C(11)
/** @brief Female idle: looking around. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_LOOK_AROUND UINT32_C(12)
/** @brief Female idle: shifting weight. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_SHIFT_WEIGHT UINT32_C(13)
/** @brief Female idle: fixing shoe. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_IDLE_FIX_SHOE UINT32_C(14)
/** @brief Female angry animation. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_ANGRY UINT32_C(15)
/** @brief Female confused animation. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_CONFUSED UINT32_C(16)
/** @brief Female laugh animation. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_LAUGH UINT32_C(17)
/** @brief Female cry animation. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_CRY UINT32_C(18)
/** @brief Female shocked animation. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_SHOCKED UINT32_C(19)
/** @brief Female yawn animation. */
#define CNA_AVATAR_ANIMATION_PRESET_FEMALE_YAWN UINT32_C(20)
/** @brief Male idle: looking around. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_LOOK_AROUND UINT32_C(21)
/** @brief Male idle: stretching. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_STRETCH UINT32_C(22)
/** @brief Male idle: shifting weight. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_SHIFT_WEIGHT UINT32_C(23)
/** @brief Male idle: checking hand. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_IDLE_CHECK_HAND UINT32_C(24)
/** @brief Male angry animation. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_ANGRY UINT32_C(25)
/** @brief Male confused animation. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_CONFUSED UINT32_C(26)
/** @brief Male laugh animation. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_LAUGH UINT32_C(27)
/** @brief Male cry animation. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_CRY UINT32_C(28)
/** @brief Male surprised animation. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_SURPRISED UINT32_C(29)
/** @brief Male yawn animation. */
#define CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN UINT32_C(30)
/** @brief Highest defined `CNA_AvatarAnimationPreset` identity. */
#define CNA_AVATAR_ANIMATION_PRESET_MAXIMUM CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN

/** @brief Fixed-width identity for one bone of the canonical avatar skeleton. */
 /* The canonical skeleton numbers its bones sparsely: fifty-five bones spread over ordinals
    0 to 70, with gaps. The gaps are the canonical skeleton's, so they are preserved here
    rather than renumbered -- a bone index is what an avatar animation stores. */
typedef uint32_t CNA_AvatarBone;

/** @brief The root bone. */
#define CNA_AVATAR_BONE_ROOT UINT32_C(0)
/** @brief The lower back bone. */
#define CNA_AVATAR_BONE_BACK_LOWER UINT32_C(1)
/** @brief The left hip bone. */
#define CNA_AVATAR_BONE_HIP_LEFT UINT32_C(2)
/** @brief The right hip bone. */
#define CNA_AVATAR_BONE_HIP_RIGHT UINT32_C(3)
/** @brief The upper back bone. */
#define CNA_AVATAR_BONE_BACK_UPPER UINT32_C(5)
/** @brief The left knee bone. */
#define CNA_AVATAR_BONE_KNEE_LEFT UINT32_C(6)
/** @brief The right knee bone. */
#define CNA_AVATAR_BONE_KNEE_RIGHT UINT32_C(8)
/** @brief The left ankle bone. */
#define CNA_AVATAR_BONE_ANKLE_LEFT UINT32_C(11)
/** @brief The left collar bone. */
#define CNA_AVATAR_BONE_COLLAR_LEFT UINT32_C(12)
/** @brief The neck bone. */
#define CNA_AVATAR_BONE_NECK UINT32_C(14)
/** @brief The right ankle bone. */
#define CNA_AVATAR_BONE_ANKLE_RIGHT UINT32_C(15)
/** @brief The right collar bone. */
#define CNA_AVATAR_BONE_COLLAR_RIGHT UINT32_C(16)
/** @brief The head bone. */
#define CNA_AVATAR_BONE_HEAD UINT32_C(19)
/** @brief The left shoulder bone. */
#define CNA_AVATAR_BONE_SHOULDER_LEFT UINT32_C(20)
/** @brief The left toe bone. */
#define CNA_AVATAR_BONE_TOE_LEFT UINT32_C(21)
/** @brief The right shoulder bone. */
#define CNA_AVATAR_BONE_SHOULDER_RIGHT UINT32_C(22)
/** @brief The right toe bone. */
#define CNA_AVATAR_BONE_TOE_RIGHT UINT32_C(23)
/** @brief The left elbow bone. */
#define CNA_AVATAR_BONE_ELBOW_LEFT UINT32_C(25)
/** @brief The right elbow bone. */
#define CNA_AVATAR_BONE_ELBOW_RIGHT UINT32_C(28)
/** @brief The left wrist bone. */
#define CNA_AVATAR_BONE_WRIST_LEFT UINT32_C(33)
/** @brief The right wrist bone. */
#define CNA_AVATAR_BONE_WRIST_RIGHT UINT32_C(36)
/** @brief The left index finger bone. */
#define CNA_AVATAR_BONE_FINGER_INDEX_LEFT UINT32_C(37)
/** @brief The left middle finger bone. */
#define CNA_AVATAR_BONE_FINGER_MIDDLE_LEFT UINT32_C(38)
/** @brief The left ring finger bone. */
#define CNA_AVATAR_BONE_FINGER_RING_LEFT UINT32_C(39)
/** @brief The left small finger bone. */
#define CNA_AVATAR_BONE_FINGER_SMALL_LEFT UINT32_C(40)
/** @brief The left prop bone. */
#define CNA_AVATAR_BONE_PROP_LEFT UINT32_C(41)
/** @brief The left special bone. */
#define CNA_AVATAR_BONE_SPECIAL_LEFT UINT32_C(42)
/** @brief The left thumb bone. */
#define CNA_AVATAR_BONE_FINGER_THUMB_LEFT UINT32_C(43)
/** @brief The right index finger bone. */
#define CNA_AVATAR_BONE_FINGER_INDEX_RIGHT UINT32_C(44)
/** @brief The right middle finger bone. */
#define CNA_AVATAR_BONE_FINGER_MIDDLE_RIGHT UINT32_C(45)
/** @brief The right ring finger bone. */
#define CNA_AVATAR_BONE_FINGER_RING_RIGHT UINT32_C(46)
/** @brief The right small finger bone. */
#define CNA_AVATAR_BONE_FINGER_SMALL_RIGHT UINT32_C(47)
/** @brief The right prop bone. */
#define CNA_AVATAR_BONE_PROP_RIGHT UINT32_C(48)
/** @brief The right special bone. */
#define CNA_AVATAR_BONE_SPECIAL_RIGHT UINT32_C(49)
/** @brief The right thumb bone. */
#define CNA_AVATAR_BONE_FINGER_THUMB_RIGHT UINT32_C(50)
/** @brief The left index finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_INDEX_2_LEFT UINT32_C(51)
/** @brief The left middle finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_MIDDLE_2_LEFT UINT32_C(52)
/** @brief The left ring finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_RING_2_LEFT UINT32_C(53)
/** @brief The left small finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_SMALL_2_LEFT UINT32_C(54)
/** @brief The left thumb, second segment. */
#define CNA_AVATAR_BONE_FINGER_THUMB_2_LEFT UINT32_C(55)
/** @brief The right index finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_INDEX_2_RIGHT UINT32_C(56)
/** @brief The right middle finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_MIDDLE_2_RIGHT UINT32_C(57)
/** @brief The right ring finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_RING_2_RIGHT UINT32_C(58)
/** @brief The right small finger, second segment. */
#define CNA_AVATAR_BONE_FINGER_SMALL_2_RIGHT UINT32_C(59)
/** @brief The right thumb, second segment. */
#define CNA_AVATAR_BONE_FINGER_THUMB_2_RIGHT UINT32_C(60)
/** @brief The left index finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_INDEX_3_LEFT UINT32_C(61)
/** @brief The left middle finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_MIDDLE_3_LEFT UINT32_C(62)
/** @brief The left ring finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_RING_3_LEFT UINT32_C(63)
/** @brief The left small finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_SMALL_3_LEFT UINT32_C(64)
/** @brief The left thumb, third segment. */
#define CNA_AVATAR_BONE_FINGER_THUMB_3_LEFT UINT32_C(65)
/** @brief The right index finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_INDEX_3_RIGHT UINT32_C(66)
/** @brief The right middle finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_MIDDLE_3_RIGHT UINT32_C(67)
/** @brief The right ring finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_RING_3_RIGHT UINT32_C(68)
/** @brief The right small finger, third segment. */
#define CNA_AVATAR_BONE_FINGER_SMALL_3_RIGHT UINT32_C(69)
/** @brief The right thumb, third segment. */
#define CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT UINT32_C(70)
/** @brief Highest defined `CNA_AvatarBone` identity. */
#define CNA_AVATAR_BONE_MAXIMUM CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT

/**
 * @brief Reports the byte length of the clip name an animation preset maps to.
 *
 * @param preset One of the `CNA_AVATAR_ANIMATION_PRESET_*` identities.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or a null
 *         output.
 *
 * CNAEXT: the clip name is how the avatar extension finds the matching animation inside a loaded
 * skinned model. This is a pure value operation — no gamer, no handle, no thread affinity.
 */
CNA_C_API CNA_Result cna_avatar_animation_preset_get_clip_name_size_ext(
    CNA_AvatarAnimationPreset preset,
    uint64_t* out_bytes);

/**
 * @brief Copies the clip name an animation preset maps to.
 *
 * @param preset One of the `CNA_AVATAR_ANIMATION_PRESET_*` identities.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or
 *         `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or an invalid output.
 *
 * CNAEXT: the name is the identity's own canonical spelling.
 */
CNA_C_API CNA_Result cna_avatar_animation_preset_copy_clip_name_ext(
    CNA_AvatarAnimationPreset preset,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of the content asset name a body type maps to.
 *
 * @param body_type One of the `CNA_AVATAR_BODY_TYPE_*` identities.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or a null
 *         output.
 *
 * CNAEXT: an avatar description never carries real body-type data in this runtime, so the body type
 * is whatever the caller already knows and this is how it becomes a loadable asset name. A pure value
 * operation — no gamer, no handle, no thread affinity.
 */
CNA_C_API CNA_Result cna_avatar_body_type_get_content_name_size_ext(
    CNA_AvatarBodyType body_type,
    uint64_t* out_bytes);

/**
 * @brief Copies the content asset name a body type maps to.
 *
 * @param body_type One of the `CNA_AVATAR_BODY_TYPE_*` identities.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or
 *         `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or an invalid output.
 *
 * CNAEXT: the name is a content path, not the identity's spelling.
 */
CNA_C_API CNA_Result cna_avatar_body_type_copy_content_name_ext(
    CNA_AvatarBodyType body_type,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Owned handle for a signed-in gamer. */
typedef CNA_Handle CNA_SignedInGamerHandle;

/**
 * @brief Creates an owned signed-in gamer.
 *
 * @param gamertag UTF-8 gamertag copied during this call.
 * @param is_signed_in_to_live `CNA_TRUE` when the gamer is signed in to the online service.
 * @param is_guest `CNA_TRUE` when the gamer is a guest.
 * @param player_index One of the `CNA_PLAYER_INDEX_*` identities.
 * @param out_gamer Receives an owned signed-in gamer handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/thread/native failure.
 *
 * CNAEXT: the canonical factory exists so a platform layer can publish a signed-in gamer. This is
 * the minimum gamer-services surface a network session needs, because the canonical session
 * constructor requires at least one signed-in gamer; the rest of the gamer-services API is a later
 * coverage task.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_create_ext(
    CNA_StringView gamertag,
    CNA_Bool is_signed_in_to_live,
    CNA_Bool is_guest,
    CNA_PlayerIndex player_index,
    CNA_SignedInGamerHandle* out_gamer);

/**
 * @brief Gets the UTF-8 byte count of a signed-in gamer's gamertag.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_gamertag_size(
    CNA_SignedInGamerHandle gamer,
    uint64_t* out_bytes);

/**
 * @brief Copies a signed-in gamer's gamertag without a terminator.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial value is written.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_copy_gamertag(
    CNA_SignedInGamerHandle gamer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Describes an accepted-invite event.
 */
typedef struct CNA_InviteAcceptedEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The gamer that accepted the invite, or `CNA_INVALID_HANDLE`. */
    CNA_SignedInGamerHandle gamer;

    /** @brief `CNA_TRUE` when the invite names the session already in progress. */
    CNA_Bool is_current_session;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];
} CNA_InviteAcceptedEventInfo;

/**
 * @brief Initializes an accepted-invite event description.
 *
 * @param gamer The gamer that accepted the invite, or `CNA_INVALID_HANDLE`.
 * @param is_current_session `CNA_TRUE` when the invite names the session already in progress.
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is not a live
 * signed-in gamer, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_invite_accepted_event_info_init(
    CNA_SignedInGamerHandle gamer,
    CNA_Bool is_current_session,
    CNA_InviteAcceptedEventInfo* out_info);

/**
 * @brief Releases an owned signed-in gamer handle.
 *
 * @param gamer Owned signed-in gamer handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while the process-wide signed-in
 * collection still references it, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_destroy(CNA_SignedInGamerHandle gamer);

/**
 * @brief Replaces the process-wide collection of signed-in gamers.
 *
 * @param gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p gamers.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * CNAEXT: the canonical collection holds non-owning pointers, so the C layer retains each handle's
 * resource for as long as the collection references it and releases the previous retention here.
 */
CNA_C_API CNA_Result cna_gamer_set_signed_in_gamers_ext(
    const CNA_SignedInGamerHandle* gamers,
    uint64_t count);

/**
 * @brief Gets how many gamers are in the process-wide signed-in collection.
 *
 * @param out_count Receives the gamer count.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * Element access is through the handles the caller created and published; a complete collection
 * view arrives with the gamer-services coverage task.
 */
CNA_C_API CNA_Result cna_gamer_get_signed_in_gamer_count(int32_t* out_count);

/* ---- Gamers, their values and their collections ---- */

/** @brief Owned handle for a gamer that is not the local signed-in gamer. */
typedef CNA_Handle CNA_GamerHandle;

/** @brief Owned handle for a gamer profile. */
typedef CNA_Handle CNA_GamerProfileHandle;

/** @brief Owned handle for a collection of gamers. */
typedef CNA_Handle CNA_GamerCollectionHandle;

/** @brief Owned handle for a cursor over a gamer collection. */
typedef CNA_Handle CNA_GamerEnumeratorHandle;

/**
 * @brief What a gamer is currently doing, as the presence service would report it.
 */
typedef struct CNA_GamerPresence {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_GAMER_PRESENCE_MODE_*` identities. */
    CNA_GamerPresenceMode presence_mode;

    /** @brief Number the presence mode displays, where the mode uses one. */
    int32_t presence_value;
} CNA_GamerPresence;

/**
 * @brief What a gamer is permitted to do.
 *
 * Three privileges are graded and four are yes-or-no, which is the canonical shape rather than a
 * simplification: communication, profile viewing and user-created content each answer *how widely*
 * they are allowed.
 */
typedef struct CNA_GamerPrivileges {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief How widely this gamer may communicate. */
    CNA_GamerPrivilegeSetting allow_communication;

    /** @brief How widely this gamer's profile may be viewed. */
    CNA_GamerPrivilegeSetting allow_profile_viewing;

    /** @brief How widely this gamer may see user-created content. */
    CNA_GamerPrivilegeSetting allow_user_created_content;

    /** @brief Non-zero when this gamer may join online sessions. */
    CNA_Bool allow_online_sessions;

    /** @brief Non-zero when this gamer may use premium content. */
    CNA_Bool allow_premium_content;

    /** @brief Non-zero when this gamer may purchase content. */
    CNA_Bool allow_purchase_content;

    /** @brief Non-zero when this gamer may trade content. */
    CNA_Bool allow_trade_content;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[4];
} CNA_GamerPrivileges;

/**
 * @brief The numeric part of a gamer profile.
 *
 * The profile's two strings are count/copy routes rather than fields, because they are owned by the
 * profile and no fixed size would be honest.
 */
typedef struct CNA_GamerProfileInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The gamer's accumulated score. */
    int32_t gamer_score;

    /** @brief One of the `CNA_GAMER_ZONE_*` identities. */
    CNA_GamerZone gamer_zone;

    /** @brief How many titles this gamer has played. */
    int32_t titles_played;

    /** @brief How many achievements this gamer has earned in total. */
    int32_t total_achievements;

    /** @brief The gamer's reputation. */
    float reputation;

    /** @brief Non-zero once the profile has been disposed. */
    CNA_Bool is_disposed;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[3];
} CNA_GamerProfileInfo;

/**
 * @brief Everything a friend's entry reports about that friendship.
 *
 * Twelve independent predicates, answered from one observation so a caller cannot see a combination
 * that never existed.
 */
typedef struct CNA_FriendGamerInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Non-zero when this friend has sent the local gamer a friend request. */
    CNA_Bool friend_request_received_from;

    /** @brief Non-zero when the local gamer has sent this friend a friend request. */
    CNA_Bool friend_request_sent_to;

    /** @brief Non-zero when this friend has voice hardware. */
    CNA_Bool has_voice;

    /** @brief Non-zero when this friend accepted a game invitation. */
    CNA_Bool invite_accepted;

    /** @brief Non-zero when this friend has sent a game invitation. */
    CNA_Bool invite_received_from;

    /** @brief Non-zero when this friend declined a game invitation. */
    CNA_Bool invite_rejected;

    /** @brief Non-zero when a game invitation has been sent to this friend. */
    CNA_Bool invite_sent_to;

    /** @brief Non-zero when this friend is away. */
    CNA_Bool is_away;

    /** @brief Non-zero when this friend is busy. */
    CNA_Bool is_busy;

    /** @brief Non-zero when this friend's session can be joined. */
    CNA_Bool is_joinable;

    /** @brief Non-zero when this friend is online. */
    CNA_Bool is_online;

    /** @brief Non-zero when this friend is playing. */
    CNA_Bool is_playing;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[4];
} CNA_FriendGamerInfo;

/**
 * @brief Description of a gamer signing in or out.
 */
typedef struct CNA_SignedInGamerEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Reserved; must be zero. */
    uint32_t reserved;

    /** @brief Borrowed gamer handle, valid only for the duration of the callback. */
    CNA_SignedInGamerHandle gamer;
} CNA_SignedInGamerEventInfo;

/**
 * @brief Callback receiving a gamer sign-in or sign-out.
 *
 * @param context Caller context supplied at subscription.
 * @param info Description of the gamer, valid only for the duration of this call.
 */
typedef void (*CNA_SignedInGamerEventCallback)(
    void* context,
    const CNA_SignedInGamerEventInfo* info);

/**
 * @brief Callback invoked when a gamer operation completes.
 *
 * @param context Caller context supplied when the operation began.
 *
 * The callback receives only the caller's context: no operation object crosses this ABI, so there is
 * nothing else to hand back.
 */
typedef void (*CNA_GamerAsyncCallback)(void* context);

/**
 * @brief Initializes a presence value to its canonical default.
 *
 * @param out_presence Receives a presence with no mode set and a zero value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamer_presence_init(CNA_GamerPresence* out_presence);

/**
 * @brief Releases a gamer handle.
 *
 * @param gamer Owned gamer handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_destroy(CNA_GamerHandle gamer);

/**
 * @brief Reports the byte length of a gamer's display name.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Every `cna_gamer_*` route accepts either handle kind, because the canonical surface belongs to the
 * gamer base a signed-in gamer and a friend both derive from.
 */
CNA_C_API CNA_Result cna_gamer_get_display_name_size(CNA_GamerHandle gamer, uint64_t* out_bytes);

/**
 * @brief Copies a gamer's display name.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_copy_display_name(
    CNA_GamerHandle gamer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Sets a gamer's display name.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param display_name New display name, copied during the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_set_display_name(
    CNA_GamerHandle gamer,
    CNA_StringView display_name);

/**
 * @brief Reports the byte length of a gamer's gamertag.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_get_gamertag_size(CNA_GamerHandle gamer, uint64_t* out_bytes);

/**
 * @brief Copies a gamer's gamertag.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_copy_gamertag(
    CNA_GamerHandle gamer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of a gamer's text form.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A gamer's text form is its display name, not its gamertag.
 */
CNA_C_API CNA_Result cna_gamer_get_text_size(CNA_GamerHandle gamer, uint64_t* out_bytes);

/**
 * @brief Copies a gamer's text form.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_copy_text(
    CNA_GamerHandle gamer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a gamer has been disposed.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param out_is_disposed Receives non-zero when the gamer has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_get_is_disposed(CNA_GamerHandle gamer, CNA_Bool* out_is_disposed);

/**
 * @brief Reads the caller-owned tag attached to a gamer.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param out_tag Receives the tag, zero if none was set.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical tag holds any boxed value at all. A C caller gets a 64-bit integer it owns the
 * meaning of, which is the same choice the graphics resources already made — a pointer-sized value
 * a caller can key its own table with, rather than an opaque box C cannot open.
 */
CNA_C_API CNA_Result cna_gamer_get_tag(CNA_GamerHandle gamer, uint64_t* out_tag);

/**
 * @brief Attaches a caller-owned tag to a gamer.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param tag Value to store.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_set_tag(CNA_GamerHandle gamer, uint64_t tag);

/**
 * @brief Reads a gamer's profile.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param out_profile Receives an owned profile handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_get_profile(
    CNA_GamerHandle gamer,
    CNA_GamerProfileHandle* out_profile);

/**
 * @brief Reads a gamer's profile and reports completion through a callback.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param callback Callback invoked once the read completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_profile Receives an owned profile handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **This is one synchronous call that still invokes the callback**, which is what the canonical
 * begin/end pair already does — the canonical operation completes before `Begin` returns. The two
 * canonical halves are one route here because no operation object crosses this ABI.
 */
CNA_C_API CNA_Result cna_gamer_begin_get_profile(
    CNA_GamerHandle gamer,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_GamerProfileHandle* out_profile);

/**
 * @brief Looks a gamer up by gamertag.
 *
 * @param gamertag Gamertag to look up, borrowed for the duration of the call.
 * @param out_gamer Receives an owned gamer handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_NOT_SUPPORTED` on this runtime, or `CNA_RESULT_INVALID_ARGUMENT` for a null
 *         output.
 *
 * **No runtime this ABI builds on can look a gamer up by tag**, so the honest answer is a refusal
 * rather than an empty result. The route exists because the canonical API does, and because the
 * answer may differ on a platform that has a directory service.
 */
CNA_C_API CNA_Result cna_gamer_get_from_gamertag(
    CNA_StringView gamertag,
    CNA_GamerHandle* out_gamer);

/**
 * @brief Looks a gamer up by gamertag and reports completion through a callback.
 *
 * @param gamertag Gamertag to look up, borrowed for the duration of the call.
 * @param callback Callback invoked once the lookup completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_gamer Receives an owned gamer handle, or `CNA_INVALID_HANDLE` on failure.
 * @return The same answers as @ref cna_gamer_get_from_gamertag. The callback does not run when the
 *         lookup is refused.
 */
CNA_C_API CNA_Result cna_gamer_begin_get_from_gamertag(
    CNA_StringView gamertag,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_GamerHandle* out_gamer);

/**
 * @brief Reports the byte length of a partner token.
 *
 * @param audience_uri Audience the token is for, borrowed for the duration of the call.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_NOT_SUPPORTED` on this runtime, or `CNA_RESULT_INVALID_ARGUMENT` for a null
 *         output.
 *
 * **No runtime this ABI builds on issues partner tokens**, so both routes refuse rather than answer
 * an empty token.
 */
CNA_C_API CNA_Result cna_gamer_get_partner_token_size(
    CNA_StringView audience_uri,
    uint64_t* out_bytes);

/**
 * @brief Copies a partner token.
 *
 * @param audience_uri Audience the token is for, borrowed for the duration of the call.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes.
 * @return `CNA_RESULT_NOT_SUPPORTED` on this runtime, or `CNA_RESULT_INVALID_ARGUMENT` for an
 *         invalid output.
 */
CNA_C_API CNA_Result cna_gamer_copy_partner_token(
    CNA_StringView audience_uri,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Requests a partner token and reports completion through a callback.
 *
 * @param audience_uri Audience the token is for, borrowed for the duration of the call.
 * @param callback Callback invoked once the request completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes.
 * @return `CNA_RESULT_NOT_SUPPORTED` on this runtime. The callback does not run when the request is
 *         refused.
 */
CNA_C_API CNA_Result cna_gamer_begin_get_partner_token(
    CNA_StringView audience_uri,
    CNA_GamerAsyncCallback callback,
    void* context,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reads the signed-in gamer at a player index.
 *
 * @param player_index One of the `CNA_PLAYER_INDEX_*` identities.
 * @param out_has_gamer Receives non-zero when a gamer is signed in at that index.
 * @param out_gamer Receives a borrowed gamer handle when @p out_has_gamer is non-zero, and is left
 *        exactly as the caller set it otherwise.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 *
 * Availability is separate from the answer: no gamer at that index is an ordinary success with the
 * flag clear, not a failure.
 *
 * **The lookup is positional.** The canonical indexer reads the signed-in collection at that index
 * rather than searching for the gamer whose own player index matches, so a single signed-in gamer
 * answers at `CNA_PLAYER_INDEX_ONE` whatever player index it was created with. That is the canonical
 * behavior, reported rather than corrected.
 */
/**
 * @brief Gets the signed-in gamer at a position in the process-wide collection.
 *
 * @param index Zero-based position, which is **not** a player index.
 * @param out_gamer Receives an owned handle borrowing the collection's gamer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a position outside the
 *         collection or a null output, or a documented handle/thread failure.
 *
 * The canonical collection is indexed two ways -- by position and by `PlayerIndex` -- and they are
 * different questions with different answers, so they are different routes. Position is what an
 * iteration uses; @ref cna_gamer_get_signed_in_gamer_at_player_index is what a controller lookup
 * uses.
 */
CNA_C_API CNA_Result cna_gamer_get_signed_in_gamer_at(
    int32_t index,
    CNA_SignedInGamerHandle* out_gamer);

/**
 * @brief Finds a signed-in gamer's position in the process-wide collection.
 *
 * @param gamer Signed-in gamer handle to look for.
 * @param out_index Receives the zero-based position, or -1 when the gamer is not in the collection.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output, or a documented
 *         handle/thread failure.
 *
 * Not finding the gamer is an answer, not a failure: the route succeeds and reports -1, exactly as
 * the canonical `IndexOf` returns a negative position.
 */
CNA_C_API CNA_Result cna_gamer_signed_in_index_of(
    CNA_SignedInGamerHandle gamer,
    int32_t* out_index);

/**
 * @brief Reports whether a signed-in gamer is in the process-wide collection.
 *
 * @param gamer Signed-in gamer handle to look for.
 * @param out_contains Receives `CNA_TRUE` when the collection holds the gamer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_signed_in_contains(
    CNA_SignedInGamerHandle gamer,
    CNA_Bool* out_contains);

CNA_C_API CNA_Result cna_gamer_get_signed_in_gamer_at_player_index(
    CNA_PlayerIndex player_index,
    CNA_Bool* out_has_gamer,
    CNA_SignedInGamerHandle* out_gamer);

/**
 * @brief Reports whether a signed-in gamer is a guest.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_is_guest Receives non-zero for a guest.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_is_guest(
    CNA_SignedInGamerHandle gamer,
    CNA_Bool* out_is_guest);

/**
 * @brief Reports whether a signed-in gamer is signed in to the online service.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_is_signed_in_to_live Receives non-zero when signed in to the online service.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_is_signed_in_to_live(
    CNA_SignedInGamerHandle gamer,
    CNA_Bool* out_is_signed_in_to_live);

/**
 * @brief Reads a signed-in gamer's party size.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_party_size Receives the party size.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_party_size(
    CNA_SignedInGamerHandle gamer,
    int32_t* out_party_size);

/**
 * @brief Sets a signed-in gamer's party size.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param party_size New party size.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_set_party_size(
    CNA_SignedInGamerHandle gamer,
    int32_t party_size);

/**
 * @brief Reads which player a signed-in gamer is.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_player_index Receives one of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_player_index(
    CNA_SignedInGamerHandle gamer,
    CNA_PlayerIndex* out_player_index);

/**
 * @brief Reads a signed-in gamer's presence.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_presence Caller-initialized structure receiving the presence.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_presence(
    CNA_SignedInGamerHandle gamer,
    CNA_GamerPresence* out_presence);

/**
 * @brief Writes a signed-in gamer's presence.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param presence Presence to publish.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical API hands out a mutable presence object rather than taking a new one; writing the
 * whole value back is the C form of the same thing.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_set_presence(
    CNA_SignedInGamerHandle gamer,
    const CNA_GamerPresence* presence);

/**
 * @brief Sets a signed-in gamer's presence from a free-text mode.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param mode Mode text, borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * CNAEXT, and **currently a no-op**: the canonical extension accepts the text and stores nothing.
 * The route exists so the surface is complete and so the behavior is recorded rather than guessed.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_set_presence_mode_string_ext(
    CNA_SignedInGamerHandle gamer,
    CNA_StringView mode);

/**
 * @brief Reads a signed-in gamer's privileges.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_privileges Caller-initialized structure receiving the privileges.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_privileges(
    CNA_SignedInGamerHandle gamer,
    CNA_GamerPrivileges* out_privileges);

/**
 * @brief Reports whether another gamer is a friend of a signed-in gamer.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param other Owned gamer or signed-in gamer handle to test.
 * @param out_is_friend Receives non-zero when the two are friends.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **On this runtime the answer is always negative**, because no friend list exists to consult. That
 * is the real answer rather than a gap in the binding.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_is_friend(
    CNA_SignedInGamerHandle gamer,
    CNA_GamerHandle other,
    CNA_Bool* out_is_friend);

/**
 * @brief Reports whether a capture device belongs to a signed-in gamer's headset.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param microphone_index Zero-based microphone index, as `cna_microphone_get_count` enumerates.
 * @param out_is_headset Receives non-zero when that microphone is a headset.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown index, or a documented
 *         handle/thread failure.
 *
 * The microphone is addressed by index for the same reason the capture surface is: the canonical
 * list hands out pointers the runtime owns and never transfers.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_is_headset(
    CNA_SignedInGamerHandle gamer,
    uint64_t microphone_index,
    CNA_Bool* out_is_headset);

/**
 * @brief Reads a signed-in gamer's friends.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_friends Receives an owned collection handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **On this runtime the collection is always empty**, because there is no friend service. An empty
 * collection is the real answer, and it is a success rather than a refusal.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_friends(
    CNA_SignedInGamerHandle gamer,
    CNA_GamerCollectionHandle* out_friends);

/**
 * @brief Awards an achievement to a signed-in gamer.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param achievement_key Achievement key, borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS`, or a documented argument/handle/thread/IO failure.
 *
 * This **persists locally**: an achievement earned in one process run is still earned in the next.
 * The canonical API carries no catalog metadata here — only the key, the fact of earning it and when.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_award_achievement(
    CNA_SignedInGamerHandle gamer,
    CNA_StringView achievement_key);

/**
 * @brief Awards an achievement and reports completion through a callback.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param achievement_key Achievement key, borrowed for the duration of the call.
 * @param callback Callback invoked once the award completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @return The same answers as @ref cna_signed_in_gamer_award_achievement.
 *
 * One synchronous call that still invokes the callback, like every other fake-async pair here.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_begin_award_achievement(
    CNA_SignedInGamerHandle gamer,
    CNA_StringView achievement_key,
    CNA_GamerAsyncCallback callback,
    void* context);

/**
 * @brief Subscribes to gamers signing in.
 *
 * @param callback Callback invoked synchronously when a gamer signs in.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_gamer_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure. A refused call routes the
 *         output handle to `CNA_INVALID_HANDLE` before it validates anything else.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_subscribe_signed_in_ext(
    CNA_SignedInGamerEventCallback callback,
    void* context,
    CNA_Handle* out_registration);

/**
 * @brief Subscribes to gamers signing out.
 *
 * @param callback Callback invoked synchronously when a gamer signs out.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_gamer_unsubscribe_ext`.
 * @return The same answers as @ref cna_signed_in_gamer_subscribe_signed_in_ext.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_subscribe_signed_out_ext(
    CNA_SignedInGamerEventCallback callback,
    void* context,
    CNA_Handle* out_registration);

/**
 * @brief Releases a gamer-services event registration.
 *
 * @param registration Owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_unsubscribe_ext(CNA_Handle registration);

/**
 * @brief Reads the numeric part of a gamer profile.
 *
 * @param profile Owned profile handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_profile_get_info(
    CNA_GamerProfileHandle profile,
    CNA_GamerProfileInfo* out_info);

/**
 * @brief Reports the byte length of a profile's motto.
 *
 * @param profile Owned profile handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_profile_get_motto_size(
    CNA_GamerProfileHandle profile,
    uint64_t* out_bytes);

/**
 * @brief Copies a profile's motto.
 *
 * @param profile Owned profile handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_profile_copy_motto(
    CNA_GamerProfileHandle profile,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of a profile's region name.
 *
 * @param profile Owned profile handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical property is a region object; C receives its name, which is the part of it a caller
 * can act on without a second type crossing the ABI.
 */
CNA_C_API CNA_Result cna_gamer_profile_get_region_name_size(
    CNA_GamerProfileHandle profile,
    uint64_t* out_bytes);

/**
 * @brief Copies a profile's region name.
 *
 * @param profile Owned profile handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_profile_copy_region_name(
    CNA_GamerProfileHandle profile,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a profile carries a gamer picture, and how large it is.
 *
 * @param profile Owned profile handle.
 * @param out_has_picture Receives non-zero when a picture is available.
 * @param out_bytes Receives the picture size in bytes, zero when there is none.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **No runtime this ABI builds on carries gamer pictures**, so the flag is always clear. Availability
 * is separate from the answer: no picture is an ordinary success, not a failure.
 */
CNA_C_API CNA_Result cna_gamer_profile_get_picture_size(
    CNA_GamerProfileHandle profile,
    CNA_Bool* out_has_picture,
    uint64_t* out_bytes);

/**
 * @brief Releases a gamer profile.
 *
 * @param profile Owned profile handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_profile_destroy(CNA_GamerProfileHandle profile);

/**
 * @brief Reads everything a friend's entry reports.
 *
 * @param gamer Owned gamer handle that names a friend.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the handle is not a friend, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_friend_gamer_get_info(
    CNA_GamerHandle gamer,
    CNA_FriendGamerInfo* out_info);

/**
 * @brief Reports the byte length of a friend's presence text.
 *
 * @param gamer Owned gamer handle that names a friend.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the handle is not a friend, or a
 *         documented argument/handle/thread failure.
 *
 * A friend's presence is **free text**, while a signed-in gamer's is a mode and a value. That
 * asymmetry is canonical and is preserved rather than evened out.
 */
CNA_C_API CNA_Result cna_friend_gamer_get_presence_size(
    CNA_GamerHandle gamer,
    uint64_t* out_bytes);

/**
 * @brief Copies a friend's presence text.
 *
 * @param gamer Owned gamer handle that names a friend.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written,
 *         `CNA_RESULT_INVALID_STATE` when the handle is not a friend, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_friend_gamer_copy_presence(
    CNA_GamerHandle gamer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports how many gamers a collection holds.
 *
 * @param collection Owned collection handle.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_get_count(
    CNA_GamerCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Reads the gamer at an index.
 *
 * @param collection Owned collection handle.
 * @param index Zero-based index.
 * @param out_gamer Receives a borrowed gamer handle valid while the collection lives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the collection,
 *         or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_get_at(
    CNA_GamerCollectionHandle collection,
    int32_t index,
    CNA_GamerHandle* out_gamer);

/**
 * @brief Reports where a gamer sits in a collection.
 *
 * @param collection Owned collection handle.
 * @param gamer Owned or borrowed gamer handle to look for.
 * @param out_index Receives the index, or -1 when the gamer is not present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_index_of(
    CNA_GamerCollectionHandle collection,
    CNA_GamerHandle gamer,
    int32_t* out_index);

/**
 * @brief Reports whether a collection holds a gamer.
 *
 * @param collection Owned collection handle.
 * @param gamer Owned or borrowed gamer handle to look for.
 * @param out_contains Receives non-zero when the gamer is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_contains(
    CNA_GamerCollectionHandle collection,
    CNA_GamerHandle gamer,
    CNA_Bool* out_contains);

/**
 * @brief Copies a collection's gamers into a caller array.
 *
 * @param collection Owned collection handle.
 * @param destination Array receiving borrowed gamer handles; may be null when @p capacity is zero.
 * @param capacity Destination capacity in handles.
 * @param index Index within @p destination to start writing at.
 * @param out_count Receives the number of gamers whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_copy_to(
    CNA_GamerCollectionHandle collection,
    CNA_GamerHandle* destination,
    uint64_t capacity,
    int32_t index,
    uint64_t* out_count);

/**
 * @brief Adds a gamer to a collection.
 *
 * @param collection Owned collection handle.
 * @param gamer Owned or borrowed gamer handle to add.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The collection holds the gamer without taking ownership of it, which is what the canonical
 * collection does; the caller keeps the handle alive.
 */
CNA_C_API CNA_Result cna_gamer_collection_add(
    CNA_GamerCollectionHandle collection,
    CNA_GamerHandle gamer);

/**
 * @brief Removes a gamer from a collection.
 *
 * @param collection Owned collection handle.
 * @param gamer Owned or borrowed gamer handle to remove.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Removing a gamer the collection does not hold is a no-op that reports success, which is what the
 * canonical operation does — it answers nothing at all.
 */
CNA_C_API CNA_Result cna_gamer_collection_remove(
    CNA_GamerCollectionHandle collection,
    CNA_GamerHandle gamer);

/**
 * @brief Empties a collection.
 *
 * @param collection Owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_clear(CNA_GamerCollectionHandle collection);

/**
 * @brief Opens a cursor over a collection.
 *
 * @param collection Owned collection handle.
 * @param out_enumerator Receives an owned enumerator handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The cursor is the C form of the canonical iteration. The canonical `begin`/`end` pair has no C
 * form at all — a C++ iterator is not expressible here — so this and the index routes are how a C
 * caller walks a collection.
 */
CNA_C_API CNA_Result cna_gamer_collection_create_enumerator(
    CNA_GamerCollectionHandle collection,
    CNA_GamerEnumeratorHandle* out_enumerator);

/**
 * @brief Advances a cursor.
 *
 * @param enumerator Owned enumerator handle.
 * @param out_has_current Receives non-zero when the cursor now names a gamer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A cursor starts before the first element, so this must be called once before the first read.
 */
CNA_C_API CNA_Result cna_gamer_enumerator_move_next(
    CNA_GamerEnumeratorHandle enumerator,
    CNA_Bool* out_has_current);

/**
 * @brief Reads the gamer a cursor names.
 *
 * @param enumerator Owned enumerator handle.
 * @param out_gamer Receives a borrowed gamer handle valid while the collection lives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the cursor names nothing, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_enumerator_get_current(
    CNA_GamerEnumeratorHandle enumerator,
    CNA_GamerHandle* out_gamer);

/**
 * @brief Returns a cursor to before the first element.
 *
 * @param enumerator Owned enumerator handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_enumerator_reset(CNA_GamerEnumeratorHandle enumerator);

/**
 * @brief Releases a cursor.
 *
 * @param enumerator Owned enumerator handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_enumerator_destroy(CNA_GamerEnumeratorHandle enumerator);

/**
 * @brief Reports whether a friend collection has been disposed.
 *
 * @param collection Owned collection handle.
 * @param out_is_disposed Receives non-zero when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the handle does not name a friend
 *         collection, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_friend_collection_get_is_disposed(
    CNA_GamerCollectionHandle collection,
    CNA_Bool* out_is_disposed);

/**
 * @brief Creates a friend entry.
 *
 * @param gamertag Gamertag, copied during the call.
 * @param display_name Display name, copied during the call.
 * @param is_online Non-zero when the friend is online.
 * @param is_playing Non-zero when the friend is playing.
 * @param is_away Non-zero when the friend is away.
 * @param is_busy Non-zero when the friend is busy.
 * @param friend_request_sent_to Non-zero when the local gamer has requested this friendship.
 * @param friend_request_received_from Non-zero when this friend has requested the friendship.
 * @param out_gamer Receives an owned gamer handle naming a friend.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 *
 * CNAEXT: the canonical factory exists so a platform layer can publish a friend list. No runtime this
 * ABI builds on has a friend service, so this is also the only way a C caller obtains a friend to
 * exercise the friend surface against.
 */
CNA_C_API CNA_Result cna_friend_gamer_create_ext(
    CNA_StringView gamertag,
    CNA_StringView display_name,
    CNA_Bool is_online,
    CNA_Bool is_playing,
    CNA_Bool is_away,
    CNA_Bool is_busy,
    CNA_Bool friend_request_sent_to,
    CNA_Bool friend_request_received_from,
    CNA_GamerHandle* out_gamer);

/**
 * @brief Creates a friend collection from friend handles.
 *
 * @param friends Array of @p count gamer handles naming friends, borrowed for the duration of the
 *        call; may be null when @p count is zero.
 * @param count Number of friends.
 * @param out_collection Receives an owned collection handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a handle does not name a friend, or
 *         a documented argument/thread failure.
 *
 * CNAEXT, and the counterpart of `cna_friend_gamer_create_ext`. The collection keeps every friend
 * handle alive for as long as it holds it, because the canonical collection stores pointers it does
 * not own.
 */
CNA_C_API CNA_Result cna_friend_collection_create_ext(
    const CNA_GamerHandle* friends,
    uint64_t count,
    CNA_GamerCollectionHandle* out_collection);

/**
 * @brief Disposes a collection and releases its handle.
 *
 * @param collection Owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_collection_destroy(CNA_GamerCollectionHandle collection);

/* ---- The guide, its dispatcher and its component ---- */

/**
 * @brief Reports whether the platform screen saver is enabled.
 *
 * @param out_is_enabled Receives non-zero when the screen saver is enabled.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_guide_get_is_screen_saver_enabled(CNA_Bool* out_is_enabled);

/**
 * @brief Enables or disables the platform screen saver.
 *
 * @param is_enabled Non-zero to enable it.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_set_is_screen_saver_enabled(CNA_Bool is_enabled);

/**
 * @brief Reports whether the title is running in trial mode.
 *
 * @param out_is_trial_mode Receives non-zero in trial mode.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_guide_get_is_trial_mode(CNA_Bool* out_is_trial_mode);

/**
 * @brief Sets whether the title is running in trial mode.
 *
 * @param is_trial_mode Non-zero for trial mode.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_set_is_trial_mode(CNA_Bool is_trial_mode);

/**
 * @brief Reports whether a guide screen is currently up.
 *
 * @param out_is_visible Receives non-zero when the guide is visible.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_guide_get_is_visible(CNA_Bool* out_is_visible);

/**
 * @brief Shows or hides the guide.
 *
 * @param is_visible Non-zero to show it.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_set_is_visible(CNA_Bool is_visible);

/**
 * @brief Reads where the guide draws its notifications.
 *
 * @param out_position Receives one of the `CNA_NOTIFICATION_POSITION_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_guide_get_notification_position(CNA_NotificationPosition* out_position);

/**
 * @brief Sets where the guide draws its notifications.
 *
 * @param position One of the `CNA_NOTIFICATION_POSITION_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_set_notification_position(CNA_NotificationPosition position);

/**
 * @brief Reports whether trial mode is being simulated.
 *
 * @param out_simulate Receives non-zero when trial mode is simulated.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_guide_get_simulate_trial_mode(CNA_Bool* out_simulate);

/**
 * @brief Sets whether trial mode is simulated.
 *
 * @param simulate Non-zero to simulate trial mode.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_set_simulate_trial_mode(CNA_Bool simulate);

/**
 * @brief Opens the on-screen keyboard.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param title Title text, borrowed for the duration of the call.
 * @param description Description text, borrowed for the duration of the call.
 * @param default_text Text the input starts with, borrowed for the duration of the call.
 * @param use_password_mode Non-zero to mask the text as it is typed.
 * @param callback Callback invoked when the input completes or is cancelled; may be null.
 * @param context Caller context passed back to @p callback.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when an input is already pending, or a
 *         documented argument/thread failure.
 *
 * **This one really is asynchronous**, unlike every other begin/end pair in this ABI: the input stays
 * pending until the user confirms or cancels it, and only then does @p callback run. Poll
 * `cna_guide_get_has_pending_keyboard_input_ext` or wait for the callback, then read the answer with
 * `cna_guide_end_show_keyboard_input_*`. Only one input may be pending at a time.
 *
 * The canonical API has a second overload without the password flag; passing `CNA_FALSE` here is
 * exactly that overload.
 */
CNA_C_API CNA_Result cna_guide_begin_show_keyboard_input(
    CNA_PlayerIndex player,
    CNA_StringView title,
    CNA_StringView description,
    CNA_StringView default_text,
    CNA_Bool use_password_mode,
    CNA_GamerAsyncCallback callback,
    void* context);

/**
 * @brief Reports the byte length of the text the completed keyboard input produced.
 *
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no input has been started or it has
 *         not completed yet, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_guide_end_show_keyboard_input_size(uint64_t* out_bytes);

/**
 * @brief Copies the text the completed keyboard input produced.
 *
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written,
 *         `CNA_RESULT_INVALID_STATE` when no completed input is available, or a documented argument
 *         failure.
 */
CNA_C_API CNA_Result cna_guide_end_show_keyboard_input(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a keyboard input is waiting for the user.
 *
 * @param out_has_pending Receives non-zero while an input is pending.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * CNAEXT: the canonical API has no way to ask, because a real platform draws the keyboard itself.
 * This runtime draws it, so a game needs to know when to.
 */
CNA_C_API CNA_Result cna_guide_get_has_pending_keyboard_input_ext(CNA_Bool* out_has_pending);

/**
 * @brief Reports whether the completed keyboard input was cancelled.
 *
 * @param out_was_canceled Receives non-zero when the user cancelled.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no input has been started, or a
 *         documented argument failure.
 *
 * CNAEXT. **A cancelled input produces no text at all** — the canonical implementation clears what
 * was typed — so this flag is the only way to tell a cancellation from a caller who confirmed an
 * empty string.
 */
CNA_C_API CNA_Result cna_guide_was_keyboard_input_canceled_ext(CNA_Bool* out_was_canceled);

/**
 * @brief Reports the byte length of the pending input's title.
 *
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when nothing is pending, or a documented
 *         argument failure.
 *
 * CNAEXT: the pending text is what a game draws, so it has to be readable.
 */
CNA_C_API CNA_Result cna_guide_get_pending_keyboard_input_title_size_ext(uint64_t* out_bytes);

/**
 * @brief Copies the pending input's title.
 *
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written,
 *         `CNA_RESULT_INVALID_STATE` when nothing is pending, or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_copy_pending_keyboard_input_title_ext(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of the pending input's description.
 *
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return The same answers as @ref cna_guide_get_pending_keyboard_input_title_size_ext.
 */
CNA_C_API CNA_Result cna_guide_get_pending_keyboard_input_description_size_ext(uint64_t* out_bytes);

/**
 * @brief Copies the pending input's description.
 *
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_guide_copy_pending_keyboard_input_title_ext.
 */
CNA_C_API CNA_Result cna_guide_copy_pending_keyboard_input_description_ext(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of what the pending input currently displays.
 *
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return The same answers as @ref cna_guide_get_pending_keyboard_input_title_size_ext.
 *
 * CNAEXT: in password mode this is the masked form, not what was typed.
 */
CNA_C_API CNA_Result cna_guide_get_pending_keyboard_input_display_text_size_ext(uint64_t* out_bytes);

/**
 * @brief Copies what the pending input currently displays.
 *
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_guide_copy_pending_keyboard_input_title_ext.
 */
CNA_C_API CNA_Result cna_guide_copy_pending_keyboard_input_display_text_ext(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Draws the pending keyboard input.
 *
 * @param device Graphics device handle.
 * @param sprite_batch Sprite batch handle.
 * @param font Sprite font handle.
 * @param white_pixel Single-white-pixel texture handle used for the panels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure. Drawing when nothing
 *         is pending is a no-op that reports success.
 *
 * CNAEXT: no real platform makes a game draw its own on-screen keyboard. This runtime has no system
 * overlay, so it draws one and the game supplies the surfaces.
 */
CNA_C_API CNA_Result cna_guide_render_pending_keyboard_input_ext(
    CNA_Handle device,
    CNA_Handle sprite_batch,
    CNA_Handle font,
    CNA_Handle white_pixel);

/**
 * @brief Cancels the pending keyboard input as though the user had.
 *
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_STATE` when nothing is pending.
 *
 * CNAEXT: the completion callback runs, exactly as it would for a real cancellation.
 */
CNA_C_API CNA_Result cna_guide_simulate_keyboard_input_cancel_ext(void);

/**
 * @brief Discards the pending keyboard input without completing it.
 *
 * @return `CNA_RESULT_SUCCESS`; discarding nothing is not a failure.
 *
 * CNAEXT: this is the reset a test uses between cases. **No callback runs** — the input never
 * completed, it was thrown away.
 */
CNA_C_API CNA_Result cna_guide_reset_pending_keyboard_input_ext(void);

/**
 * @brief Opens a message box.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param title Title text, borrowed for the duration of the call.
 * @param text Body text, borrowed for the duration of the call.
 * @param buttons Array of @p button_count button captions, borrowed for the duration of the call.
 * @param button_count Number of buttons; must be at least one.
 * @param focus_button Index of the button that starts focused.
 * @param icon One of the `CNA_MESSAGE_BOX_ICON_*` identities.
 * @param callback Callback invoked when a button is chosen; may be null.
 * @param context Caller context passed back to @p callback.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an empty button list or an
 *         undefined icon, `CNA_RESULT_INVALID_STATE` when a message box is already pending, or a
 *         documented argument/thread failure.
 *
 * Asynchronous in the same real sense the keyboard input is: it stays pending until a button is
 * chosen. The canonical API has a second overload without the player; both reach the same
 * implementation, which ignores the player entirely.
 */
CNA_C_API CNA_Result cna_guide_begin_show_message_box(
    CNA_PlayerIndex player,
    CNA_StringView title,
    CNA_StringView text,
    const CNA_StringView* buttons,
    uint64_t button_count,
    int32_t focus_button,
    CNA_MessageBoxIcon icon,
    CNA_GamerAsyncCallback callback,
    void* context);

/**
 * @brief Reads which button answered the message box.
 *
 * @param out_has_choice Receives non-zero when a button was chosen.
 * @param out_button_index Receives the chosen button index when @p out_has_choice is non-zero, and
 *        is left exactly as the caller set it otherwise.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no message box has been started or
 *         it has not been answered yet, or a documented argument failure.
 *
 * The canonical answer is optional, so availability is separate from the answer here too: a message
 * box that completed without a choice is an ordinary success with the flag clear.
 */
CNA_C_API CNA_Result cna_guide_end_show_message_box(
    CNA_Bool* out_has_choice,
    int32_t* out_button_index);

/**
 * @brief Reports whether a message box is waiting for the user.
 *
 * @param out_has_pending Receives non-zero while one is pending.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * CNAEXT, for the same reason the keyboard input has one: this runtime draws the box itself.
 */
CNA_C_API CNA_Result cna_guide_get_has_pending_message_box_ext(CNA_Bool* out_has_pending);

/**
 * @brief Reports which button the pending message box has focused.
 *
 * @param out_focus_button Receives the focused button index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when nothing is pending, or a documented
 *         argument failure.
 */
CNA_C_API CNA_Result cna_guide_get_pending_message_box_focus_button_ext(int32_t* out_focus_button);

/**
 * @brief Draws the pending message box.
 *
 * @param device Graphics device handle.
 * @param sprite_batch Sprite batch handle.
 * @param font Sprite font handle.
 * @param white_pixel Single-white-pixel texture handle used for the panels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure. Drawing when nothing
 *         is pending is a no-op that reports success.
 *
 * CNAEXT, for the same reason the keyboard input's renderer is.
 */
CNA_C_API CNA_Result cna_guide_render_pending_message_box_ext(
    CNA_Handle device,
    CNA_Handle sprite_batch,
    CNA_Handle font,
    CNA_Handle white_pixel);

/**
 * @brief Chooses a button on the pending message box as though the user had.
 *
 * @param button_index Index of the button to choose.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the button list,
 *         or `CNA_RESULT_INVALID_STATE` when nothing is pending.
 *
 * CNAEXT: the completion callback runs, exactly as it would for a real click.
 */
CNA_C_API CNA_Result cna_guide_simulate_message_box_click_ext(int32_t button_index);

/**
 * @brief Discards the pending message box without answering it.
 *
 * @return `CNA_RESULT_SUCCESS`; discarding nothing is not a failure.
 *
 * CNAEXT. **No callback runs** — the box never completed, it was thrown away.
 */
CNA_C_API CNA_Result cna_guide_reset_pending_message_box_ext(void);

/**
 * @brief Delays guide notifications.
 *
 * @param delay_ticks Delay in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS`.
 *
 * **A no-op on this runtime**, like every guide screen below: there is no notification system to
 * delay. The route exists because the canonical API does.
 */
CNA_C_API CNA_Result cna_guide_delay_notifications(int64_t delay_ticks);

/**
 * @brief Opens the compose-message screen.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param text Message text, borrowed for the duration of the call.
 * @param recipients Array of @p recipient_count gamer handles, borrowed for the duration of the
 *        call; may be null when @p recipient_count is zero.
 * @param recipient_count Number of recipients.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **A no-op on this runtime.** Every `cna_guide_show_*` route below is: this runtime has no guide
 * UI for any of these screens, so they accept their arguments, validate them and do nothing. Only
 * the keyboard input and the message box are real, and those two are real because this ABI draws
 * them.
 */
CNA_C_API CNA_Result cna_guide_show_compose_message(
    CNA_PlayerIndex player,
    CNA_StringView text,
    const CNA_GamerHandle* recipients,
    uint64_t recipient_count);

/**
 * @brief Opens the friend-request screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param gamer Gamer handle the request is for.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_guide_show_friend_request(CNA_PlayerIndex player, CNA_GamerHandle gamer);

/**
 * @brief Opens the friends screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_show_friends(CNA_PlayerIndex player);

/**
 * @brief Opens the game-invite screen for a set of recipients. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param recipients Array of @p recipient_count gamer handles, borrowed for the duration of the
 *        call; may be null when @p recipient_count is zero.
 * @param recipient_count Number of recipients.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_guide_show_game_invite(
    CNA_PlayerIndex player,
    const CNA_GamerHandle* recipients,
    uint64_t recipient_count);

/**
 * @brief Opens the game-invite screen for a session. A no-op on this runtime.
 *
 * @param session_id Session identifier, borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_show_game_invite_for_session(CNA_StringView session_id);

/**
 * @brief Opens a gamer card. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param gamer Gamer handle whose card to show.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_guide_show_gamer_card(CNA_PlayerIndex player, CNA_GamerHandle gamer);

/**
 * @brief Opens the marketplace. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_show_marketplace(CNA_PlayerIndex player);

/**
 * @brief Opens the messages screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_show_messages(CNA_PlayerIndex player);

/**
 * @brief Opens the party screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_show_party(CNA_PlayerIndex player);

/**
 * @brief Opens the party-sessions screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_show_party_sessions(CNA_PlayerIndex player);

/**
 * @brief Opens the player-review screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param gamer Gamer handle to review.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_guide_show_player_review(CNA_PlayerIndex player, CNA_GamerHandle gamer);

/**
 * @brief Opens the players screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_guide_show_players(CNA_PlayerIndex player);

/**
 * @brief Opens the sign-in screen. A no-op on this runtime.
 *
 * @param pane_count How many sign-in panes to show.
 * @param online_only Non-zero to require an online sign-in.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_guide_show_sign_in(int32_t pane_count, CNA_Bool online_only);

/**
 * @brief Opens the achievements screen. A no-op on this runtime.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 *
 * CNAEXT: the canonical guide has no achievements screen of its own.
 */
CNA_C_API CNA_Result cna_guide_show_achievements_ext(CNA_PlayerIndex player);

/**
 * @brief Reports whether the gamer-services dispatcher has been initialized.
 *
 * @param out_is_initialized Receives non-zero once it has.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_get_is_initialized(
    CNA_Bool* out_is_initialized);

/**
 * @brief Reads the window handle the dispatcher was given.
 *
 * @param out_window_handle Receives the platform window handle as an integer, zero if none was set.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The value is whatever the caller stored: this ABI passes it through without interpreting it, and
 * nothing in this runtime reads it.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_get_window_handle(uint64_t* out_window_handle);

/**
 * @brief Sets the window handle the dispatcher reports.
 *
 * @param window_handle Platform window handle as an integer.
 * @return `CNA_RESULT_SUCCESS`.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_set_window_handle(uint64_t window_handle);

/**
 * @brief Initializes the dispatcher against a game's services.
 *
 * @param game Owning game handle, whose service container the dispatcher takes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_initialize(CNA_Handle game);

/**
 * @brief Pumps the dispatcher once.
 *
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_update(void);

/**
 * @brief Pumps one asynchronous step of the dispatcher.
 *
 * @param out_did_work Receives non-zero when there was work left to do.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_update_async(CNA_Bool* out_did_work);

/**
 * @brief Reports how many gamers the dispatcher has released.
 *
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * CNAEXT: a counter the canonical implementation keeps for its own tests, exposed because it is the
 * only way to observe the dispatcher having done anything.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_get_freed_gamer_count_ext(uint64_t* out_count);

/**
 * @brief Subscribes to the dispatcher's title-update notification.
 *
 * @param callback Callback invoked synchronously when a title update begins installing.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_gamer_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 */
CNA_C_API CNA_Result cna_gamer_services_dispatcher_subscribe_installing_title_update_ext(
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_Handle* out_registration);

/**
 * @brief Creates the canonical gamer-services game component.
 *
 * @param game Owning game handle.
 * @param out_component Receives an owned component handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The handle is an ordinary game-component handle, so every `cna_game_component_*` route accepts it.
 * Unlike the components a C caller builds from a callback set, this one is a **canonical** component:
 * its initialize and update behavior belong to the runtime, and it is what pumps the dispatcher for a
 * game that adds it to its component collection.
 */
CNA_C_API CNA_Result cna_gamer_services_component_create(CNA_Handle game, CNA_Handle* out_component);

/* ---- Achievements ---- */

/** @brief Owned handle for one achievement. */
typedef CNA_Handle CNA_AchievementHandle;

/** @brief Owned handle for a collection of achievements. */
typedef CNA_Handle CNA_AchievementCollectionHandle;

/**
 * @brief Everything an achievement reports that is not text.
 */
typedef struct CNA_AchievementInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Score this achievement is worth. */
    int32_t gamer_score;

    /** @brief Non-zero when the achievement is shown before it has been earned. */
    CNA_Bool display_before_earned;

    /** @brief Non-zero when the achievement was earned while online. */
    CNA_Bool earned_online;

    /** @brief Non-zero once the achievement has been earned. */
    CNA_Bool is_earned;

    /** @brief Reserved; must be zero. */
    uint8_t reserved;

    /** @brief When the achievement was earned, in 100-nanosecond ticks. */
    int64_t earned_date_time_ticks;
} CNA_AchievementInfo;

/**
 * @brief Creates an achievement.
 *
 * @param key Stable key the service identifies the achievement by, copied during the call.
 * @param name Display name, copied during the call.
 * @param description Description, copied during the call.
 * @param display_before_earned Non-zero to show the achievement before it is earned.
 * @param is_earned Non-zero when it has been earned.
 * @param earned_date_time_ticks When it was earned, in 100-nanosecond ticks.
 * @param out_achievement Receives an owned achievement handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 *
 * CNAEXT: the canonical factory exists so a platform layer can publish an achievement catalog. It is
 * also how a C caller builds a collection to work with, since nothing here downloads one.
 */
CNA_C_API CNA_Result cna_achievement_create_ext(
    CNA_StringView key,
    CNA_StringView name,
    CNA_StringView description,
    CNA_Bool display_before_earned,
    CNA_Bool is_earned,
    int64_t earned_date_time_ticks,
    CNA_AchievementHandle* out_achievement);

/**
 * @brief Releases an achievement handle.
 *
 * @param achievement Owned achievement handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_destroy(CNA_AchievementHandle achievement);

/**
 * @brief Reads everything an achievement reports that is not text.
 *
 * @param achievement Owned achievement handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_get_info(
    CNA_AchievementHandle achievement,
    CNA_AchievementInfo* out_info);

/**
 * @brief Reports the byte length of an achievement's key.
 *
 * @param achievement Owned achievement handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_get_key_size(
    CNA_AchievementHandle achievement,
    uint64_t* out_bytes);

/**
 * @brief Copies an achievement's key.
 *
 * @param achievement Owned achievement handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_copy_key(
    CNA_AchievementHandle achievement,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of an achievement's display name.
 *
 * @param achievement Owned achievement handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_get_name_size(
    CNA_AchievementHandle achievement,
    uint64_t* out_bytes);

/**
 * @brief Copies an achievement's display name.
 *
 * @param achievement Owned achievement handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_achievement_copy_key.
 */
CNA_C_API CNA_Result cna_achievement_copy_name(
    CNA_AchievementHandle achievement,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of an achievement's description.
 *
 * @param achievement Owned achievement handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_get_description_size(
    CNA_AchievementHandle achievement,
    uint64_t* out_bytes);

/**
 * @brief Copies an achievement's description.
 *
 * @param achievement Owned achievement handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_achievement_copy_key.
 */
CNA_C_API CNA_Result cna_achievement_copy_description(
    CNA_AchievementHandle achievement,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of an achievement's how-to-earn text.
 *
 * @param achievement Owned achievement handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_get_how_to_earn_size(
    CNA_AchievementHandle achievement,
    uint64_t* out_bytes);

/**
 * @brief Copies an achievement's how-to-earn text.
 *
 * @param achievement Owned achievement handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return The same answers as @ref cna_achievement_copy_key.
 */
CNA_C_API CNA_Result cna_achievement_copy_how_to_earn(
    CNA_AchievementHandle achievement,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the size of an achievement's picture.
 *
 * @param achievement Owned achievement handle.
 * @param out_bytes Receives the picture size in bytes.
 * @return `CNA_RESULT_NOT_SUPPORTED` on this runtime, or a documented argument/handle/thread
 *         failure.
 *
 * **No runtime this ABI builds on carries achievement pictures**: the canonical accessor is not
 * implemented and says so, and this route reports that rather than answering an empty picture. That
 * is a different answer from the gamer picture, which is *absent* rather than unimplemented.
 */
CNA_C_API CNA_Result cna_achievement_get_picture_size(
    CNA_AchievementHandle achievement,
    uint64_t* out_bytes);

/**
 * @brief Reports whether two achievements are equal.
 *
 * @param achievement Owned achievement handle.
 * @param other Owned achievement handle to compare with.
 * @param out_equals Receives non-zero when the two are equal.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Achievements compare **by value across every field**, not by identity, which is what makes a handle
 * answered by a collection comparable with one a caller built. This one route is what both canonical
 * equality operators map to; inequality is its negation.
 */
CNA_C_API CNA_Result cna_achievement_equals(
    CNA_AchievementHandle achievement,
    CNA_AchievementHandle other,
    CNA_Bool* out_equals);

/**
 * @brief Creates a collection from achievement handles.
 *
 * @param achievements Array of @p count achievement handles, borrowed for the duration of the call;
 *        may be null when @p count is zero.
 * @param count Number of achievements.
 * @param out_collection Receives an owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * CNAEXT. **The collection copies each achievement**, so the caller's handles stay its own and
 * releasing one afterwards does not disturb the collection.
 */
CNA_C_API CNA_Result cna_achievement_collection_create_ext(
    const CNA_AchievementHandle* achievements,
    uint64_t count,
    CNA_AchievementCollectionHandle* out_collection);

/**
 * @brief Disposes a collection and releases its handle.
 *
 * @param collection Owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_destroy(
    CNA_AchievementCollectionHandle collection);

/**
 * @brief Reports how many achievements a collection holds.
 *
 * @param collection Owned collection handle.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_get_count(
    CNA_AchievementCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Reports whether a collection has been disposed.
 *
 * @param collection Owned collection handle.
 * @param out_is_disposed Receives non-zero when it has.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_get_is_disposed(
    CNA_AchievementCollectionHandle collection,
    CNA_Bool* out_is_disposed);

/**
 * @brief Reports whether a collection refuses to be modified.
 *
 * @param collection Owned collection handle.
 * @param out_is_read_only Receives non-zero when it is read-only.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_get_is_read_only(
    CNA_AchievementCollectionHandle collection,
    CNA_Bool* out_is_read_only);

/**
 * @brief Reads the achievement at an index.
 *
 * @param collection Owned collection handle.
 * @param index Zero-based index.
 * @param out_achievement Receives an owned achievement handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the collection, or
 *         a documented handle/thread failure.
 *
 * **The handle is a copy, not a view into the collection.** The canonical indexer answers a reference
 * into storage that inserting or removing would invalidate; an achievement is a value, so copying it
 * is safe and loses nothing — equality is by value, so the copy still compares equal to what the
 * collection holds.
 */
CNA_C_API CNA_Result cna_achievement_collection_get_at(
    CNA_AchievementCollectionHandle collection,
    int32_t index,
    CNA_AchievementHandle* out_achievement);

/**
 * @brief Reads the achievement with a key.
 *
 * @param collection Owned collection handle.
 * @param key Achievement key, borrowed for the duration of the call.
 * @param out_achievement Receives an owned achievement handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when no achievement has that key, or a
 *         documented handle/thread failure.
 *
 * The handle is a copy, for the same reason the index route's is.
 */
CNA_C_API CNA_Result cna_achievement_collection_get_by_key(
    CNA_AchievementCollectionHandle collection,
    CNA_StringView key,
    CNA_AchievementHandle* out_achievement);

/**
 * @brief Reports where an achievement sits in a collection.
 *
 * @param collection Owned collection handle.
 * @param achievement Owned achievement handle to look for.
 * @param out_index Receives the index, or -1 when it is not present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_index_of(
    CNA_AchievementCollectionHandle collection,
    CNA_AchievementHandle achievement,
    int32_t* out_index);

/**
 * @brief Reports whether a collection holds an achievement.
 *
 * @param collection Owned collection handle.
 * @param achievement Owned achievement handle to look for.
 * @param out_contains Receives non-zero when it is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_contains(
    CNA_AchievementCollectionHandle collection,
    CNA_AchievementHandle achievement,
    CNA_Bool* out_contains);

/**
 * @brief Appends an achievement to a collection.
 *
 * @param collection Owned collection handle.
 * @param achievement Owned achievement handle to copy in.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_add(
    CNA_AchievementCollectionHandle collection,
    CNA_AchievementHandle achievement);

/**
 * @brief Inserts an achievement at an index.
 *
 * @param collection Owned collection handle.
 * @param index Index to insert at.
 * @param achievement Owned achievement handle to copy in.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the collection, or
 *         a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_insert(
    CNA_AchievementCollectionHandle collection,
    int32_t index,
    CNA_AchievementHandle achievement);

/**
 * @brief Removes the achievement at an index.
 *
 * @param collection Owned collection handle.
 * @param index Index to remove.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the collection, or
 *         a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_remove_at(
    CNA_AchievementCollectionHandle collection,
    int32_t index);

/**
 * @brief Removes an achievement equal to the one given.
 *
 * @param collection Owned collection handle.
 * @param achievement Owned achievement handle to remove.
 * @param out_removed Receives non-zero when one was found and removed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Unlike the gamer collection's remove, this one **answers whether anything was removed**, because
 * the canonical operation does.
 */
CNA_C_API CNA_Result cna_achievement_collection_remove(
    CNA_AchievementCollectionHandle collection,
    CNA_AchievementHandle achievement,
    CNA_Bool* out_removed);

/**
 * @brief Empties a collection.
 *
 * @param collection Owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_clear(
    CNA_AchievementCollectionHandle collection);

/**
 * @brief Copies a collection's achievements into a caller array.
 *
 * @param collection Owned collection handle.
 * @param destination Array receiving owned achievement handles; may be null when @p capacity is
 *        zero. Every handle written is the caller's to release.
 * @param capacity Destination capacity in handles.
 * @param index Index within @p destination to start writing at.
 * @param out_count Receives the number of achievements whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_achievement_collection_copy_to(
    CNA_AchievementCollectionHandle collection,
    CNA_AchievementHandle* destination,
    uint64_t capacity,
    int32_t index,
    uint64_t* out_count);

/**
 * @brief Reads a signed-in gamer's earned achievements.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_achievements Receives an owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/IO failure.
 *
 * This reads what `cna_signed_in_gamer_award_achievement` persisted, so an achievement earned in one
 * process run is still there in the next. The achievements it answers carry **only a key, an earned
 * flag and a timestamp**: no catalog exists here to supply a name, a description or a score.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_achievements(
    CNA_SignedInGamerHandle gamer,
    CNA_AchievementCollectionHandle* out_achievements);

/**
 * @brief Reads a signed-in gamer's earned achievements and reports completion through a callback.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param callback Callback invoked once the read completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_achievements Receives an owned collection handle.
 * @return The same answers as @ref cna_signed_in_gamer_get_achievements.
 *
 * One synchronous call that still invokes the callback: the canonical read marks itself complete
 * before `Begin` returns, precisely so a game does not spin waiting for work that is already done.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_begin_get_achievements(
    CNA_SignedInGamerHandle gamer,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_AchievementCollectionHandle* out_achievements);

/* ---- Property storage and game defaults ---- */

/** @brief Owned handle for a string-keyed property dictionary. */
typedef CNA_Handle CNA_PropertyDictionaryHandle;

/**
 * @brief Fixed-width identity for what kind of value a property slot holds.
 *
 * CNAEXT: the canonical dictionary stores a boxed value a C caller cannot open, so this is how a
 * caller reading a value it did not write learns which typed getter to use.
 */
typedef uint32_t CNA_PropertyValueKind;

/** @brief The slot holds a value none of the typed getters understands. */
#define CNA_PROPERTY_VALUE_KIND_UNKNOWN UINT32_C(0)
/** @brief The slot holds a date and time. */
#define CNA_PROPERTY_VALUE_KIND_DATE_TIME UINT32_C(1)
/** @brief The slot holds a double-precision number. */
#define CNA_PROPERTY_VALUE_KIND_DOUBLE UINT32_C(2)
/** @brief The slot holds a 32-bit integer. */
#define CNA_PROPERTY_VALUE_KIND_INT32 UINT32_C(3)
/** @brief The slot holds a 64-bit integer. */
#define CNA_PROPERTY_VALUE_KIND_INT64 UINT32_C(4)
/** @brief The slot holds a leaderboard outcome. */
#define CNA_PROPERTY_VALUE_KIND_OUTCOME UINT32_C(5)
/** @brief The slot holds a single-precision number. */
#define CNA_PROPERTY_VALUE_KIND_SINGLE UINT32_C(6)
/** @brief The slot holds a stream. */
#define CNA_PROPERTY_VALUE_KIND_STREAM UINT32_C(7)
/** @brief The slot holds text. */
#define CNA_PROPERTY_VALUE_KIND_STRING UINT32_C(8)
/** @brief The slot holds a time span. */
#define CNA_PROPERTY_VALUE_KIND_TIME_SPAN UINT32_C(9)
/** @brief Highest defined `CNA_PropertyValueKind` identity. */
#define CNA_PROPERTY_VALUE_KIND_MAXIMUM CNA_PROPERTY_VALUE_KIND_TIME_SPAN

/**
 * @brief A gamer's game-wide preferences.
 *
 * The two colors are **optional**: their flags say whether the gamer chose one at all, which is a
 * different thing from having chosen black.
 */
typedef struct CNA_GameDefaults {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_GAME_DIFFICULTY_*` identities. */
    CNA_GameDifficulty game_difficulty;

    /** @brief One of the `CNA_CONTROLLER_SENSITIVITY_*` identities. */
    CNA_ControllerSensitivity controller_sensitivity;

    /** @brief One of the `CNA_RACING_CAMERA_ANGLE_*` identities. */
    CNA_RacingCameraAngle racing_camera_angle;

    /** @brief Non-zero when the gamer chose a primary color. */
    CNA_Bool has_primary_color;

    /** @brief Non-zero when the gamer chose a secondary color. */
    CNA_Bool has_secondary_color;

    /** @brief Non-zero when aiming assistance is on. */
    CNA_Bool auto_aim;

    /** @brief Non-zero when auto-centering is on. */
    CNA_Bool auto_center;

    /** @brief Non-zero when movement uses the right thumbstick. */
    CNA_Bool move_with_right_thumb_stick;

    /** @brief Non-zero when the vertical axis is inverted. */
    CNA_Bool invert_y_axis;

    /** @brief Non-zero when the gamer prefers manual transmission. */
    CNA_Bool manual_transmission;

    /** @brief Non-zero when acceleration is on a button rather than a trigger. */
    CNA_Bool accelerate_with_buttons;

    /** @brief Non-zero when braking is on a button rather than a trigger. */
    CNA_Bool brake_with_buttons;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[3];

    /** @brief Primary color, meaningful only when @ref has_primary_color is non-zero. */
    CNA_Color primary_color;

    /** @brief Secondary color, meaningful only when @ref has_secondary_color is non-zero. */
    CNA_Color secondary_color;
} CNA_GameDefaults;

/**
 * @brief Initializes game defaults to the canonical default.
 *
 * @param out_defaults Receives the defaults a gamer starts with.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_game_defaults_init(CNA_GameDefaults* out_defaults);

/**
 * @brief Reads a signed-in gamer's game defaults.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_defaults Caller-initialized structure receiving the defaults.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_game_defaults(
    CNA_SignedInGamerHandle gamer,
    CNA_GameDefaults* out_defaults);

/**
 * @brief Creates an empty property dictionary.
 *
 * @param out_dictionary Receives an owned dictionary handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 *
 * CNAEXT. The canonical factory takes a map of boxed values, which C cannot express; an empty
 * dictionary plus the typed setters reaches every state that map could hold, because every value the
 * canonical getters understand is one of the nine kinds.
 */
CNA_C_API CNA_Result cna_property_dictionary_create_ext(
    CNA_PropertyDictionaryHandle* out_dictionary);

/**
 * @brief Releases a property dictionary handle.
 *
 * @param dictionary Owned dictionary handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_destroy(CNA_PropertyDictionaryHandle dictionary);

/**
 * @brief Reports how many properties a dictionary holds.
 *
 * @param dictionary Owned dictionary handle.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_count(
    CNA_PropertyDictionaryHandle dictionary,
    int32_t* out_count);

/**
 * @brief Reports whether a dictionary describes itself as read-only.
 *
 * @param dictionary Owned dictionary handle.
 * @param out_is_read_only Receives the canonical answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **The canonical answer is always yes, and the dictionary is nonetheless writable.** Setting,
 * removing and clearing all work. That contradiction is canonical and is reported rather than
 * corrected, because a caller that trusts the flag would otherwise be surprised by the routes.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_is_read_only(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_Bool* out_is_read_only);

/**
 * @brief Reports whether a dictionary holds a key.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_contains Receives non-zero when the key is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_contains_key(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_Bool* out_contains);

/**
 * @brief Reports whether a key is present and what kind of value it holds.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_found Receives non-zero when the key is present.
 * @param out_kind Receives one of the `CNA_PROPERTY_VALUE_KIND_*` identities, or
 *        `CNA_PROPERTY_VALUE_KIND_UNKNOWN` when the key is absent or holds something no typed getter
 *        understands.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * This is what replaces reading the raw boxed value: **ask what is in the slot, then use the matching
 * typed getter.** Availability is separate from the answer here too — a key that is not present is an
 * ordinary success with the flag clear.
 */
CNA_C_API CNA_Result cna_property_dictionary_try_get_value_kind_ext(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_Bool* out_found,
    CNA_PropertyValueKind* out_kind);

/**
 * @brief Reads a date-and-time property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_ticks Receives the value in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown key,
 *         `CNA_RESULT_INVALID_STATE` when the slot holds a different kind, or a documented
 *         handle/thread failure.
 *
 * Every typed getter checks the slot's kind **at the boundary** and refuses a mismatch, rather than
 * letting the canonical unboxing fail into a generic internal error a caller could not act on.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_date_time_ticks(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int64_t* out_ticks);

/**
 * @brief Reads a double-precision property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_value Receives the value.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_double(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    double* out_value);

/**
 * @brief Reads a 32-bit integer property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_value Receives the value.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_int32(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int32_t* out_value);

/**
 * @brief Reads a 64-bit integer property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_value Receives the value.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_int64(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int64_t* out_value);

/**
 * @brief Reads a leaderboard-outcome property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_outcome Receives one of the `CNA_LEADERBOARD_OUTCOME_*` identities.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_outcome(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_LeaderboardOutcome* out_outcome);

/**
 * @brief Reads a single-precision property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_value Receives the value.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_single(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    float* out_value);

/**
 * @brief Reports the size of a stream property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_has_stream Receives non-zero when the slot holds a stream that is not null.
 * @param out_bytes Receives the stream length in bytes, zero when there is none.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 *
 * CNAEXT: a stream object never crosses this ABI, so what a C caller can learn about one is whether
 * it is there and how long it is.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_stream_size_ext(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_Bool* out_has_stream,
    uint64_t* out_bytes);

/**
 * @brief Reports the byte length of a text property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_string_size(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    uint64_t* out_bytes);

/**
 * @brief Copies a text property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or the same
 *         refusals as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_copy_string(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reads a time-span property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_ticks Receives the value in 100-nanosecond ticks.
 * @return The same answers as @ref cna_property_dictionary_get_date_time_ticks.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_time_span_ticks(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int64_t* out_ticks);

/**
 * @brief Writes a date-and-time property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param ticks Value in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Writing replaces whatever the slot held, including a value of a different kind.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_date_time_ticks(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int64_t ticks);

/**
 * @brief Writes a double-precision property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param value Value to store.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_double(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    double value);

/**
 * @brief Writes a 32-bit integer property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param value Value to store.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_int32(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int32_t value);

/**
 * @brief Writes a 64-bit integer property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param value Value to store.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_int64(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int64_t value);

/**
 * @brief Writes a leaderboard-outcome property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param outcome One of the `CNA_LEADERBOARD_OUTCOME_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_outcome(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_LeaderboardOutcome outcome);

/**
 * @brief Writes a single-precision property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param value Value to store.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_single(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    float value);

/**
 * @brief Writes a text property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param value Text to store, copied during the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_string(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_StringView value);

/**
 * @brief Writes a time-span property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param ticks Value in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_set_time_span_ticks(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    int64_t ticks);

/**
 * @brief Removes a property.
 *
 * @param dictionary Owned dictionary handle.
 * @param key Property key, borrowed for the duration of the call.
 * @param out_removed Receives non-zero when the key was present and removed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_remove(
    CNA_PropertyDictionaryHandle dictionary,
    CNA_StringView key,
    CNA_Bool* out_removed);

/**
 * @brief Empties a dictionary.
 *
 * @param dictionary Owned dictionary handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_clear(CNA_PropertyDictionaryHandle dictionary);

/**
 * @brief Reports the byte length of the key at an index.
 *
 * @param dictionary Owned dictionary handle.
 * @param index Zero-based index into the dictionary's keys, which are in ascending key order.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the dictionary, or
 *         a documented handle/thread failure.
 *
 * Walking the keys by index is how a C caller enumerates a dictionary: the canonical key list and the
 * canonical bulk copy both answer containers C cannot receive.
 */
CNA_C_API CNA_Result cna_property_dictionary_get_key_size_at(
    CNA_PropertyDictionaryHandle dictionary,
    int32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies the key at an index.
 *
 * @param dictionary Owned dictionary handle.
 * @param index Zero-based index into the dictionary's keys, which are in ascending key order.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written,
 *         `CNA_RESULT_INVALID_ARGUMENT` for an index outside the dictionary, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_property_dictionary_copy_key_at(
    CNA_PropertyDictionaryHandle dictionary,
    int32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Leaderboards ---- */

/** @brief Owned handle for a leaderboard reader. */
typedef CNA_Handle CNA_LeaderboardReaderHandle;

/** @brief Owned handle for one leaderboard entry. */
typedef CNA_Handle CNA_LeaderboardEntryHandle;

/** @brief Capacity of a leaderboard identity's inline key, including no terminator. */
#define CNA_LEADERBOARD_IDENTITY_KEY_CAPACITY UINT32_C(64)

/**
 * @brief Names one leaderboard.
 *
 * The key is carried inline rather than as a pointer, so an identity is a value a caller can build on
 * the stack and pass by address. The canonical keys are short names like `"BestScoreLifeTime"`; a key
 * that does not fit is **refused rather than truncated**.
 */
typedef struct CNA_LeaderboardIdentity {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Game mode this leaderboard is scoped to. */
    int32_t game_mode;

    /** @brief Key, NUL-padded; write it directly to name a leaderboard this ABI has no identity for. */
    char key[64];
} CNA_LeaderboardIdentity;

/**
 * @brief What a leaderboard reader reports about its page.
 */
typedef struct CNA_LeaderboardReaderInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Index of the first entry on this page within the whole leaderboard. */
    int32_t page_start;

    /** @brief How many entries the whole leaderboard has. */
    int32_t total_leaderboard_size;

    /** @brief How many entries this page holds. */
    int32_t entry_count;

    /** @brief Non-zero once the reader has been disposed. */
    CNA_Bool is_disposed;

    /** @brief Non-zero when there is a further page below. */
    CNA_Bool can_page_down;

    /** @brief Non-zero when there is a further page above. */
    CNA_Bool can_page_up;

    /** @brief Reserved; must be zero. */
    uint8_t reserved;
} CNA_LeaderboardReaderInfo;

/**
 * @brief What one leaderboard entry reports.
 */
typedef struct CNA_LeaderboardEntryInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Where the entry sits in the whole leaderboard. */
    int32_t ranking;

    /** @brief Non-zero when the entry names a gamer. */
    CNA_Bool has_gamer;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[3];

    /** @brief The entry's score. */
    int64_t rating;
} CNA_LeaderboardEntryInfo;

/**
 * @brief Initializes a leaderboard identity from a canonical key.
 *
 * @param key One of the `CNA_LEADERBOARD_KEY_*` identities.
 * @param game_mode Game mode to scope the leaderboard to.
 * @param out_identity Receives the identity.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an undefined key or a null
 *         output.
 *
 * This is both canonical creation routes: the one without a game mode is this one with zero.
 */
CNA_C_API CNA_Result cna_leaderboard_identity_init(
    CNA_LeaderboardKey key,
    int32_t game_mode,
    CNA_LeaderboardIdentity* out_identity);

/**
 * @brief Reads a page of a leaderboard.
 *
 * @param identity Which leaderboard to read.
 * @param page_start Index of the first entry to return.
 * @param page_size How many entries to return.
 * @param out_reader Receives an owned reader handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread/IO failure.
 *
 * **The read is complete when this returns.** The canonical asynchronous form completes before its
 * own begin route returns, so there is nothing here to wait for; the callback form below exists
 * because the canonical API has one, not because anything defers.
 *
 * One deviation is worth knowing: the canonical synchronous read **leaks the operation it creates**,
 * unlike every other route of its shape. This one does the same work through the same two public
 * halves and releases the operation afterwards.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_read(
    const CNA_LeaderboardIdentity* identity,
    int32_t page_start,
    int32_t page_size,
    CNA_LeaderboardReaderHandle* out_reader);

/**
 * @brief Reads a page of a leaderboard centred on one gamer.
 *
 * @param identity Which leaderboard to read.
 * @param pivot_gamer Gamer handle to centre the page on; may be `CNA_INVALID_HANDLE` for none.
 * @param page_size How many entries to return.
 * @param out_reader Receives an owned reader handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/IO failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_read_from_pivot(
    const CNA_LeaderboardIdentity* identity,
    CNA_GamerHandle pivot_gamer,
    int32_t page_size,
    CNA_LeaderboardReaderHandle* out_reader);

/**
 * @brief Reads a page of a leaderboard restricted to a set of gamers.
 *
 * @param identity Which leaderboard to read.
 * @param gamers Array of @p gamer_count gamer handles, borrowed for the duration of the call; may be
 *        null when @p gamer_count is zero.
 * @param gamer_count Number of gamers.
 * @param pivot_gamer Gamer handle to centre the page on; may be `CNA_INVALID_HANDLE` for none.
 * @param page_size How many entries to return.
 * @param out_reader Receives an owned reader handle, or `CNA_INVALID_HANDLE` on failure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/IO failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_read_from_gamers(
    const CNA_LeaderboardIdentity* identity,
    const CNA_GamerHandle* gamers,
    uint64_t gamer_count,
    CNA_GamerHandle pivot_gamer,
    int32_t page_size,
    CNA_LeaderboardReaderHandle* out_reader);

/**
 * @brief Reads a page and reports completion through a callback.
 *
 * @param identity Which leaderboard to read.
 * @param page_start Index of the first entry to return.
 * @param page_size How many entries to return.
 * @param callback Callback invoked once the read completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_reader Receives an owned reader handle, or `CNA_INVALID_HANDLE` on failure.
 * @return The same answers as @ref cna_leaderboard_reader_read.
 *
 * One synchronous call that still invokes the callback, like every other completed-on-begin pair here.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_begin_read(
    const CNA_LeaderboardIdentity* identity,
    int32_t page_start,
    int32_t page_size,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_LeaderboardReaderHandle* out_reader);

/**
 * @brief Reads a page centred on one gamer and reports completion through a callback.
 *
 * @param identity Which leaderboard to read.
 * @param pivot_gamer Gamer handle to centre the page on; may be `CNA_INVALID_HANDLE` for none.
 * @param page_size How many entries to return.
 * @param callback Callback invoked once the read completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_reader Receives an owned reader handle, or `CNA_INVALID_HANDLE` on failure.
 * @return The same answers as @ref cna_leaderboard_reader_read_from_pivot.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_begin_read_from_pivot(
    const CNA_LeaderboardIdentity* identity,
    CNA_GamerHandle pivot_gamer,
    int32_t page_size,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_LeaderboardReaderHandle* out_reader);

/**
 * @brief Reads a page restricted to a set of gamers and reports completion through a callback.
 *
 * @param identity Which leaderboard to read.
 * @param gamers Array of @p gamer_count gamer handles, borrowed for the duration of the call; may be
 *        null when @p gamer_count is zero.
 * @param gamer_count Number of gamers.
 * @param pivot_gamer Gamer handle to centre the page on; may be `CNA_INVALID_HANDLE` for none.
 * @param page_size How many entries to return.
 * @param callback Callback invoked once the read completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_reader Receives an owned reader handle, or `CNA_INVALID_HANDLE` on failure.
 * @return The same answers as @ref cna_leaderboard_reader_read_from_gamers.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_begin_read_from_gamers(
    const CNA_LeaderboardIdentity* identity,
    const CNA_GamerHandle* gamers,
    uint64_t gamer_count,
    CNA_GamerHandle pivot_gamer,
    int32_t page_size,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_LeaderboardReaderHandle* out_reader);

/**
 * @brief Disposes a reader and releases its handle.
 *
 * @param reader Owned reader handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_destroy(CNA_LeaderboardReaderHandle reader);

/**
 * @brief Reads what a reader reports about its page.
 *
 * @param reader Owned reader handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_get_info(
    CNA_LeaderboardReaderHandle reader,
    CNA_LeaderboardReaderInfo* out_info);

/**
 * @brief Reads which leaderboard a reader is reading.
 *
 * @param reader Owned reader handle.
 * @param out_identity Caller-initialized structure receiving the identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_get_identity(
    CNA_LeaderboardReaderHandle reader,
    CNA_LeaderboardIdentity* out_identity);

/**
 * @brief Reads the entry at an index on the current page.
 *
 * @param reader Owned reader handle.
 * @param index Zero-based index into this page.
 * @param out_entry Receives an owned entry handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the page, or a
 *         documented handle/thread failure.
 *
 * **The handle is a copy, and every leaderboard entry this ABI hands out is.** The canonical entry
 * list is answered by value, so an entry read here is a snapshot: writing its rating changes the
 * snapshot, not the leaderboard. The canonical writer is the only thing that writes back, and it is
 * not reachable from this ABI — see `docs/c-api/GAMER_SERVICES.md` for why.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_get_entry_at(
    CNA_LeaderboardReaderHandle reader,
    int32_t index,
    CNA_LeaderboardEntryHandle* out_entry);

/**
 * @brief Moves a reader to the next page.
 *
 * @param reader Owned reader handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when there is no page below, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_page_down(CNA_LeaderboardReaderHandle reader);

/**
 * @brief Moves a reader to the previous page.
 *
 * @param reader Owned reader handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when there is no page above, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_page_up(CNA_LeaderboardReaderHandle reader);

/**
 * @brief Moves a reader to the next page and reports completion through a callback.
 *
 * @param reader Owned reader handle.
 * @param callback Callback invoked once the page has moved; may be null.
 * @param context Caller context passed back to @p callback.
 * @return The same answers as @ref cna_leaderboard_reader_page_down.
 *
 * Paging reslices entries the reader already holds, so there is no second read to wait for.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_begin_page_down(
    CNA_LeaderboardReaderHandle reader,
    CNA_GamerAsyncCallback callback,
    void* context);

/**
 * @brief Moves a reader to the previous page and reports completion through a callback.
 *
 * @param reader Owned reader handle.
 * @param callback Callback invoked once the page has moved; may be null.
 * @param context Caller context passed back to @p callback.
 * @return The same answers as @ref cna_leaderboard_reader_page_up.
 */
CNA_C_API CNA_Result cna_leaderboard_reader_begin_page_up(
    CNA_LeaderboardReaderHandle reader,
    CNA_GamerAsyncCallback callback,
    void* context);

/**
 * @brief Creates a leaderboard entry.
 *
 * @param gamer Gamer the entry belongs to; may be `CNA_INVALID_HANDLE` for none.
 * @param rating The entry's score.
 * @param ranking Where the entry sits in the whole leaderboard.
 * @param out_entry Receives an owned entry handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * CNAEXT: the canonical factory exists so a platform layer can publish a leaderboard page.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_create_ext(
    CNA_GamerHandle gamer,
    int64_t rating,
    int32_t ranking,
    CNA_LeaderboardEntryHandle* out_entry);

/**
 * @brief Releases a leaderboard entry handle.
 *
 * @param entry Owned entry handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_destroy(CNA_LeaderboardEntryHandle entry);

/**
 * @brief Reads what an entry reports.
 *
 * @param entry Owned entry handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_get_info(
    CNA_LeaderboardEntryHandle entry,
    CNA_LeaderboardEntryInfo* out_info);

/**
 * @brief Writes an entry's score.
 *
 * @param entry Owned entry handle.
 * @param rating New score.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A rating-changed hook, if one is attached, runs before this returns.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_set_rating(
    CNA_LeaderboardEntryHandle entry,
    int64_t rating);

/**
 * @brief Reads which gamer an entry belongs to.
 *
 * @param entry Owned entry handle.
 * @param out_has_gamer Receives non-zero when the entry names a gamer.
 * @param out_gamer Receives a borrowed gamer handle when @p out_has_gamer is non-zero, and is left
 *        exactly as the caller set it otherwise.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Availability is separate from the answer: an entry with no gamer is an ordinary success with the
 * flag clear.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_get_gamer(
    CNA_LeaderboardEntryHandle entry,
    CNA_Bool* out_has_gamer,
    CNA_GamerHandle* out_gamer);

/**
 * @brief Reads an entry's columns.
 *
 * @param entry Owned entry handle.
 * @param out_columns Receives an owned dictionary handle over the entry's own columns.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The dictionary is the entry's own, not a copy: writing through it changes the entry. The canonical
 * API has a const and a non-const accessor; C gets one handle that does both, because a C caller
 * cannot express constness of a handle anyway.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_get_columns(
    CNA_LeaderboardEntryHandle entry,
    CNA_PropertyDictionaryHandle* out_columns);

/**
 * @brief Attaches a callback that runs whenever an entry's score changes.
 *
 * @param entry Owned entry handle.
 * @param callback Callback invoked after each score write; pass null to detach.
 * @param context Caller context passed back to @p callback.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * CNAEXT. **The hook is not a subscription**: there is one per entry, attaching replaces whatever was
 * there, and it lives as long as the entry rather than as long as a registration handle.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_set_rating_changed_hook_ext(
    CNA_LeaderboardEntryHandle entry,
    CNA_GamerAsyncCallback callback,
    void* context);

/**
 * @brief Reports whether two entries are equal.
 *
 * @param entry Owned entry handle.
 * @param other Owned entry handle to compare with.
 * @param out_equals Receives non-zero when the two are equal.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Entries compare by **gamer, rating and ranking** — not by their columns. This one route is what both
 * canonical equality operators map to; inequality is its negation.
 */
CNA_C_API CNA_Result cna_leaderboard_entry_equals(
    CNA_LeaderboardEntryHandle entry,
    CNA_LeaderboardEntryHandle other,
    CNA_Bool* out_equals);

/* ---- Avatars ---- */

/** @brief Owned handle for an avatar description. */
typedef CNA_Handle CNA_AvatarDescriptionHandle;

/** @brief Owned handle for an avatar animation. */
typedef CNA_Handle CNA_AvatarAnimationHandle;

/** @brief Owned handle for an avatar renderer. */
typedef CNA_Handle CNA_AvatarRendererHandle;

/** @brief How many bones an avatar skeleton has. */
#define CNA_AVATAR_RENDERER_BONE_COUNT INT32_C(71)

/** @brief Exactly how many bytes an avatar description occupies. */
#define CNA_AVATAR_DESCRIPTION_BYTE_COUNT UINT64_C(1021)

/**
 * @brief The face an avatar is wearing.
 */
typedef struct CNA_AvatarExpression {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_AVATAR_MOUTH_*` identities. */
    CNA_AvatarMouth mouth;

    /** @brief One of the `CNA_AVATAR_EYE_*` identities. */
    CNA_AvatarEye left_eye;

    /** @brief One of the `CNA_AVATAR_EYE_*` identities. */
    CNA_AvatarEye right_eye;

    /** @brief One of the `CNA_AVATAR_EYEBROW_*` identities. */
    CNA_AvatarEyebrow left_eyebrow;

    /** @brief One of the `CNA_AVATAR_EYEBROW_*` identities. */
    CNA_AvatarEyebrow right_eyebrow;
} CNA_AvatarExpression;

/**
 * @brief The colors an avatar is drawn in.
 *
 * CNAEXT: the canonical avatar description carries no colors this runtime can read, so an appearance
 * is what a game supplies instead.
 */
typedef struct CNA_AvatarAppearanceEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Skin color. */
    CNA_Color skin_color;

    /** @brief Hair color. */
    CNA_Color hair_color;

    /** @brief Shirt color. */
    CNA_Color shirt_color;

    /** @brief Trouser color. */
    CNA_Color pants_color;

    /** @brief Shoe color. */
    CNA_Color shoes_color;
} CNA_AvatarAppearanceEXT;

/**
 * @brief What an avatar description reports.
 *
 * **Two fields are constant on this runtime**: the height is always zero and the body type is always
 * female, because the canonical description format carries neither and the implementation says so
 * rather than guessing. Only validity and the bytes themselves vary.
 */
typedef struct CNA_AvatarDescriptionInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_AVATAR_BODY_TYPE_*` identities. */
    CNA_AvatarBodyType body_type;

    /** @brief The avatar's height. */
    float height;

    /** @brief How many bytes the description occupies. */
    uint64_t description_byte_count;

    /** @brief Non-zero when the description is usable. */
    CNA_Bool is_valid;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[7];
} CNA_AvatarDescriptionInfo;

/**
 * @brief What an avatar animation reports.
 *
 * **A built-in preset carries the whole skeleton but no timeline**: its length is zero until a real
 * clip is loaded, so advancing it moves nothing. The bone transforms are still the full skeleton.
 */
typedef struct CNA_AvatarAnimationInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief How many bone transforms the animation carries. */
    int32_t bone_transform_count;

    /** @brief Non-zero once the animation has been disposed. */
    CNA_Bool is_disposed;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[3];

    /** @brief Where the animation is, in 100-nanosecond ticks. */
    int64_t current_position_ticks;

    /** @brief How long the animation is, in 100-nanosecond ticks. */
    int64_t length_ticks;
} CNA_AvatarAnimationInfo;

/**
 * @brief What an avatar renderer reports.
 */
typedef struct CNA_AvatarRendererInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_AVATAR_RENDERER_STATE_*` identities. */
    CNA_AvatarRendererState state;

    /** @brief Non-zero once the renderer has been disposed. */
    CNA_Bool is_disposed;

    /** @brief Non-zero when real rendering has been enabled. */
    CNA_Bool is_real_rendering_enabled;

    /** @brief Reserved; must be zero. */
    uint8_t reserved[2];
} CNA_AvatarRendererInfo;

/**
 * @brief Initializes an expression to the canonical default.
 *
 * @param out_expression Receives a neutral face.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_avatar_expression_init(CNA_AvatarExpression* out_expression);

/**
 * @brief Initializes an appearance to the canonical default.
 *
 * @param out_appearance Receives the default colors.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * CNAEXT.
 */
CNA_C_API CNA_Result cna_avatar_appearance_init_ext(CNA_AvatarAppearanceEXT* out_appearance);

/**
 * @brief Creates an avatar description from its bytes.
 *
 * @param description Description bytes, borrowed for the duration of the call; may be null when
 *        @p byte_count is zero.
 * @param byte_count Number of bytes.
 * @param out_description Receives an owned description handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 *
 * The canonical constructor takes **exactly `CNA_AVATAR_DESCRIPTION_BYTE_COUNT` bytes** and refuses
 * anything else, so this route does too. Whether the description is *valid* is a separate question
 * decided by its first byte, which `cna_avatar_description_get_info` answers.
 */
CNA_C_API CNA_Result cna_avatar_description_create(
    const uint8_t* description,
    uint64_t byte_count,
    CNA_AvatarDescriptionHandle* out_description);

/**
 * @brief Creates a random avatar description.
 *
 * @param out_description Receives an owned description handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_description_create_random(
    CNA_AvatarDescriptionHandle* out_description);

/**
 * @brief Creates a random avatar description of one body type.
 *
 * @param body_type One of the `CNA_AVATAR_BODY_TYPE_*` identities.
 * @param out_description Receives an owned description handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented thread failure.
 */
CNA_C_API CNA_Result cna_avatar_description_create_random_for_body_type(
    CNA_AvatarBodyType body_type,
    CNA_AvatarDescriptionHandle* out_description);

/**
 * @brief Reads a gamer's avatar description.
 *
 * @param gamer Owned gamer or signed-in gamer handle.
 * @param callback Callback invoked once the read completes; may be null.
 * @param context Caller context passed back to @p callback.
 * @param out_description Receives an owned description handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed gamer, or a documented
 *         argument/handle/thread failure.
 *
 * One synchronous call that still invokes the callback: the canonical read completes before its own
 * begin route returns.
 */
CNA_C_API CNA_Result cna_avatar_description_get_from_gamer(
    CNA_GamerHandle gamer,
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_AvatarDescriptionHandle* out_description);

/**
 * @brief Releases an avatar description handle.
 *
 * @param description Owned description handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_description_destroy(CNA_AvatarDescriptionHandle description);

/**
 * @brief Reads what an avatar description reports.
 *
 * @param description Owned description handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_description_get_info(
    CNA_AvatarDescriptionHandle description,
    CNA_AvatarDescriptionInfo* out_info);

/**
 * @brief Copies an avatar description's bytes.
 *
 * @param description Owned description handle.
 * @param destination Buffer receiving the bytes; may be null when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the byte count whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_description_copy_description(
    CNA_AvatarDescriptionHandle description,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to avatar descriptions changing.
 *
 * @param callback Callback invoked synchronously when a description changes.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle, released with
 *        `cna_gamer_unsubscribe_ext`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread failure.
 *
 * The canonical event is **static**: it is about descriptions in general, not about one of them, so
 * this route takes no description handle. **Nothing in this runtime raises it.**
 */
CNA_C_API CNA_Result cna_avatar_description_subscribe_changed_ext(
    CNA_GamerAsyncCallback callback,
    void* context,
    CNA_Handle* out_registration);

/**
 * @brief Creates an avatar animation from a built-in preset.
 *
 * @param preset One of the `CNA_AVATAR_ANIMATION_PRESET_*` identities.
 * @param out_animation Receives an owned animation handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_create(
    CNA_AvatarAnimationPreset preset,
    CNA_AvatarAnimationHandle* out_animation);

/**
 * @brief Disposes an animation and releases its handle.
 *
 * @param animation Owned animation handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_destroy(CNA_AvatarAnimationHandle animation);

/**
 * @brief Reads what an animation reports.
 *
 * @param animation Owned animation handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_get_info(
    CNA_AvatarAnimationHandle animation,
    CNA_AvatarAnimationInfo* out_info);

/**
 * @brief Moves an animation to a position.
 *
 * @param animation Owned animation handle.
 * @param position_ticks Position in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_set_current_position(
    CNA_AvatarAnimationHandle animation,
    int64_t position_ticks);

/**
 * @brief Reads the face an animation is currently showing.
 *
 * @param animation Owned animation handle.
 * @param out_expression Caller-initialized structure receiving the expression.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_get_expression(
    CNA_AvatarAnimationHandle animation,
    CNA_AvatarExpression* out_expression);

/**
 * @brief Advances an animation.
 *
 * @param animation Owned animation handle.
 * @param elapsed_ticks How much time has passed, in 100-nanosecond ticks.
 * @param loop Non-zero to wrap around at the end rather than stopping.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_update(
    CNA_AvatarAnimationHandle animation,
    int64_t elapsed_ticks,
    CNA_Bool loop);

/**
 * @brief Reads one of an animation's bone transforms.
 *
 * @param animation Owned animation handle.
 * @param index Zero-based bone index.
 * @param out_transform Receives the transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the skeleton, or a
 *         documented handle/thread failure.
 *
 * The bone transforms are read one at a time because the canonical collection is answered by value;
 * `cna_avatar_animation_get_info` reports how many there are.
 */
CNA_C_API CNA_Result cna_avatar_animation_get_bone_transform_at(
    CNA_AvatarAnimationHandle animation,
    int32_t index,
    CNA_Matrix* out_transform);

/**
 * @brief Reports the byte length of an animation's real clip name.
 *
 * @param animation Owned animation handle.
 * @param out_bytes Receives the length in bytes, with no terminator counted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * CNAEXT: the clip name is what the real-rendering path looks up inside a loaded skinned model.
 */
CNA_C_API CNA_Result cna_avatar_animation_get_real_clip_name_size_ext(
    CNA_AvatarAnimationHandle animation,
    uint64_t* out_bytes);

/**
 * @brief Copies an animation's real clip name.
 *
 * @param animation Owned animation handle.
 * @param destination Buffer receiving UTF-8 bytes with no terminator; may be null when
 *        @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the length in bytes whether or not the copy succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with nothing written, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_copy_real_clip_name_ext(
    CNA_AvatarAnimationHandle animation,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Sets an animation's real clip name.
 *
 * @param animation Owned animation handle.
 * @param clip_name Clip name, copied during the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_animation_set_real_clip_name_ext(
    CNA_AvatarAnimationHandle animation,
    CNA_StringView clip_name);

/**
 * @brief Creates an avatar renderer for a description.
 *
 * @param description Owned description handle the renderer draws.
 * @param use_loading_effect Non-zero to draw the loading effect while assets arrive.
 * @param out_renderer Receives an owned renderer handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The renderer keeps the description handle alive for as long as it draws it. The canonical
 * constructor without the loading flag is this one with it clear.
 */
CNA_C_API CNA_Result cna_avatar_renderer_create(
    CNA_AvatarDescriptionHandle description,
    CNA_Bool use_loading_effect,
    CNA_AvatarRendererHandle* out_renderer);

/**
 * @brief Disposes a renderer and releases its handle.
 *
 * @param renderer Owned renderer handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_destroy(CNA_AvatarRendererHandle renderer);

/**
 * @brief Reads what a renderer reports.
 *
 * @param renderer Owned renderer handle.
 * @param out_info Caller-initialized structure receiving the snapshot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_get_info(
    CNA_AvatarRendererHandle renderer,
    CNA_AvatarRendererInfo* out_info);

/**
 * @brief Reads a renderer's world, view and projection transforms.
 *
 * @param renderer Owned renderer handle.
 * @param out_world Receives the world transform; may be null.
 * @param out_view Receives the view transform; may be null.
 * @param out_projection Receives the projection transform; may be null.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The three canonical properties are read together because a caller that wants one almost always
 * wants the set, and each is optional so a caller can ask for only what it needs.
 */
CNA_C_API CNA_Result cna_avatar_renderer_get_transforms(
    CNA_AvatarRendererHandle renderer,
    CNA_Matrix* out_world,
    CNA_Matrix* out_view,
    CNA_Matrix* out_projection);

/**
 * @brief Writes a renderer's world, view and projection transforms.
 *
 * @param renderer Owned renderer handle.
 * @param world New world transform; may be null to leave it alone.
 * @param view New view transform; may be null to leave it alone.
 * @param projection New projection transform; may be null to leave it alone.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_set_transforms(
    CNA_AvatarRendererHandle renderer,
    const CNA_Matrix* world,
    const CNA_Matrix* view,
    const CNA_Matrix* projection);

/**
 * @brief Reads a renderer's lighting.
 *
 * @param renderer Owned renderer handle.
 * @param out_light_color Receives the light color; may be null.
 * @param out_light_direction Receives the light direction; may be null.
 * @param out_ambient_light_color Receives the ambient light color; may be null.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_get_lighting(
    CNA_AvatarRendererHandle renderer,
    CNA_Vector3* out_light_color,
    CNA_Vector3* out_light_direction,
    CNA_Vector3* out_ambient_light_color);

/**
 * @brief Writes a renderer's lighting.
 *
 * @param renderer Owned renderer handle.
 * @param light_color New light color; may be null to leave it alone.
 * @param light_direction New light direction; may be null to leave it alone.
 * @param ambient_light_color New ambient light color; may be null to leave it alone.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-finite component, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_set_lighting(
    CNA_AvatarRendererHandle renderer,
    const CNA_Vector3* light_color,
    const CNA_Vector3* light_direction,
    const CNA_Vector3* ambient_light_color);

/**
 * @brief Reads the index of a bone's parent.
 *
 * @param renderer Owned renderer handle.
 * @param index Zero-based bone index.
 * @param out_parent_index Receives the parent's index, or -1 for a root bone.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the skeleton, or a
 *         documented handle/thread failure.
 *
 * The skeleton always has `CNA_AVATAR_RENDERER_BONE_COUNT` bones.
 */
CNA_C_API CNA_Result cna_avatar_renderer_get_parent_bone_at(
    CNA_AvatarRendererHandle renderer,
    int32_t index,
    int32_t* out_parent_index);

/**
 * @brief Reads a bone's bind-pose transform.
 *
 * @param renderer Owned renderer handle.
 * @param index Zero-based bone index.
 * @param out_transform Receives the transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the skeleton,
 *         `CNA_RESULT_INVALID_STATE` while the avatar's assets are unavailable or the renderer has
 *         been disposed, or a documented handle/thread failure.
 *
 * **On this runtime the state is always unavailable**, so this route always refuses — the parent-bone
 * hierarchy is readable and the bind pose is not. That is the renderer's real state rather than a gap
 * in the binding.
 */
CNA_C_API CNA_Result cna_avatar_renderer_get_bind_pose_at(
    CNA_AvatarRendererHandle renderer,
    int32_t index,
    CNA_Matrix* out_transform);

/**
 * @brief Draws an avatar in an animation's current pose.
 *
 * @param renderer Owned renderer handle.
 * @param animation Owned animation handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed renderer, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_draw_animation(
    CNA_AvatarRendererHandle renderer,
    CNA_AvatarAnimationHandle animation);

/**
 * @brief Draws an avatar in a pose the caller supplies.
 *
 * @param renderer Owned renderer handle.
 * @param bones Array of @p bone_count transforms, borrowed for the duration of the call.
 * @param bone_count Number of transforms; must be `CNA_AVATAR_RENDERER_BONE_COUNT`.
 * @param expression The face to draw.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a wrong bone count or an invalid
 *         expression, `CNA_RESULT_INVALID_STATE` for a disposed renderer, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_avatar_renderer_draw_bones(
    CNA_AvatarRendererHandle renderer,
    const CNA_Matrix* bones,
    uint64_t bone_count,
    const CNA_AvatarExpression* expression);

/**
 * @brief Enables drawing a real skinned model rather than the placeholder.
 *
 * @param renderer Owned renderer handle.
 * @param device Graphics device handle.
 * @param model Skinned model handle to draw.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed renderer, or a documented
 *         argument/handle/thread failure.
 *
 * CNAEXT: no avatar asset service exists here, so a game supplies its own model.
 */
CNA_C_API CNA_Result cna_avatar_renderer_enable_real_rendering_ext(
    CNA_AvatarRendererHandle renderer,
    CNA_Handle device,
    CNA_Handle model);

/**
 * @brief Sets the colors a real-rendered avatar is drawn in.
 *
 * @param renderer Owned renderer handle.
 * @param appearance The colors to use.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * CNAEXT.
 */
CNA_C_API CNA_Result cna_avatar_renderer_set_appearance_ext(
    CNA_AvatarRendererHandle renderer,
    const CNA_AvatarAppearanceEXT* appearance);

/**
 * @brief Draws the real skinned model at a point in a named clip.
 *
 * @param renderer Owned renderer handle.
 * @param animation_clip_name Clip to draw, borrowed for the duration of the call.
 * @param position_ticks Position within the clip, in 100-nanosecond ticks.
 * @param loop Non-zero to wrap around at the end of the clip.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a disposed renderer or one with no
 *         real model, or a documented argument/handle/thread failure.
 *
 * CNAEXT.
 */
CNA_C_API CNA_Result cna_avatar_renderer_draw_real_ext(
    CNA_AvatarRendererHandle renderer,
    CNA_StringView animation_clip_name,
    int64_t position_ticks,
    CNA_Bool loop);

#ifdef __cplusplus
}
#endif

#endif
