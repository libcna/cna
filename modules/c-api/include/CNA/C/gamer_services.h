// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GAMER_SERVICES_H
#define CNA_C_GAMER_SERVICES_H

#include "CNA/C/input.h"

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

#ifdef __cplusplus
}
#endif

#endif
