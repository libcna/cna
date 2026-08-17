// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* Every gamer and guide identity, written out in canonical order. An entry that does not sit at
   its own index is a moved ordinal, which is an ABI break rather than a rename. */

static const CNA_GamerPresenceMode gamerPresenceMode[] = {
    CNA_GAMER_PRESENCE_MODE_NONE,
    CNA_GAMER_PRESENCE_MODE_SINGLE_PLAYER,
    CNA_GAMER_PRESENCE_MODE_MULTIPLAYER,
    CNA_GAMER_PRESENCE_MODE_LOCAL_CO_OP,
    CNA_GAMER_PRESENCE_MODE_LOCAL_VERSUS,
    CNA_GAMER_PRESENCE_MODE_ONLINE_CO_OP,
    CNA_GAMER_PRESENCE_MODE_ONLINE_VERSUS,
    CNA_GAMER_PRESENCE_MODE_VERSUS_COMPUTER,
    CNA_GAMER_PRESENCE_MODE_STAGE,
    CNA_GAMER_PRESENCE_MODE_LEVEL,
    CNA_GAMER_PRESENCE_MODE_CO_OP_STAGE,
    CNA_GAMER_PRESENCE_MODE_CO_OP_LEVEL,
    CNA_GAMER_PRESENCE_MODE_ARCADE_MODE,
    CNA_GAMER_PRESENCE_MODE_CAMPAIGN_MODE,
    CNA_GAMER_PRESENCE_MODE_CHALLENGE_MODE,
    CNA_GAMER_PRESENCE_MODE_EXPLORATION_MODE,
    CNA_GAMER_PRESENCE_MODE_PRACTICE_MODE,
    CNA_GAMER_PRESENCE_MODE_PUZZLE_MODE,
    CNA_GAMER_PRESENCE_MODE_SCENARIO_MODE,
    CNA_GAMER_PRESENCE_MODE_STORY_MODE,
    CNA_GAMER_PRESENCE_MODE_SURVIVAL_MODE,
    CNA_GAMER_PRESENCE_MODE_TUTORIAL_MODE,
    CNA_GAMER_PRESENCE_MODE_DIFFICULTY_EASY,
    CNA_GAMER_PRESENCE_MODE_DIFFICULTY_MEDIUM,
    CNA_GAMER_PRESENCE_MODE_DIFFICULTY_HARD,
    CNA_GAMER_PRESENCE_MODE_DIFFICULTY_EXTREME,
    CNA_GAMER_PRESENCE_MODE_SCORE,
    CNA_GAMER_PRESENCE_MODE_VERSUS_SCORE,
    CNA_GAMER_PRESENCE_MODE_WINNING,
    CNA_GAMER_PRESENCE_MODE_LOSING,
    CNA_GAMER_PRESENCE_MODE_SCORE_IS_TIED,
    CNA_GAMER_PRESENCE_MODE_OUTNUMBERED,
    CNA_GAMER_PRESENCE_MODE_ON_A_ROLL,
    CNA_GAMER_PRESENCE_MODE_IN_COMBAT,
    CNA_GAMER_PRESENCE_MODE_BATTLING_BOSS,
    CNA_GAMER_PRESENCE_MODE_TIME_ATTACK,
    CNA_GAMER_PRESENCE_MODE_TRYING_FOR_RECORD,
    CNA_GAMER_PRESENCE_MODE_FREE_PLAY,
    CNA_GAMER_PRESENCE_MODE_WASTING_TIME,
    CNA_GAMER_PRESENCE_MODE_STUCK_ON_A_HARD_BIT,
    CNA_GAMER_PRESENCE_MODE_NEARLY_FINISHED,
    CNA_GAMER_PRESENCE_MODE_LOOKING_FOR_GAMES,
    CNA_GAMER_PRESENCE_MODE_WAITING_FOR_PLAYERS,
    CNA_GAMER_PRESENCE_MODE_WAITING_IN_LOBBY,
    CNA_GAMER_PRESENCE_MODE_SETTING_UP_MATCH,
    CNA_GAMER_PRESENCE_MODE_PLAYING_WITH_FRIENDS,
    CNA_GAMER_PRESENCE_MODE_AT_MENU,
    CNA_GAMER_PRESENCE_MODE_STARTING_GAME,
    CNA_GAMER_PRESENCE_MODE_PAUSED,
    CNA_GAMER_PRESENCE_MODE_GAME_OVER,
    CNA_GAMER_PRESENCE_MODE_WON_THE_GAME,
    CNA_GAMER_PRESENCE_MODE_CONFIGURING_SETTINGS,
    CNA_GAMER_PRESENCE_MODE_CUSTOMIZING_PLAYER,
    CNA_GAMER_PRESENCE_MODE_EDITING_LEVEL,
    CNA_GAMER_PRESENCE_MODE_IN_GAME_STORE,
    CNA_GAMER_PRESENCE_MODE_WATCHING_CUTSCENE,
    CNA_GAMER_PRESENCE_MODE_WATCHING_CREDITS,
    CNA_GAMER_PRESENCE_MODE_PLAYING_MINIGAME,
    CNA_GAMER_PRESENCE_MODE_FOUND_SECRET,
    CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE,
};

static const CNA_NotificationPosition notificationPosition[] = {
    CNA_NOTIFICATION_POSITION_TOP_LEFT,
    CNA_NOTIFICATION_POSITION_TOP_CENTER,
    CNA_NOTIFICATION_POSITION_TOP_RIGHT,
    CNA_NOTIFICATION_POSITION_CENTER_LEFT,
    CNA_NOTIFICATION_POSITION_CENTER,
    CNA_NOTIFICATION_POSITION_CENTER_RIGHT,
    CNA_NOTIFICATION_POSITION_BOTTOM_LEFT,
    CNA_NOTIFICATION_POSITION_BOTTOM_CENTER,
    CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT,
};

static const CNA_GamerZone gamerZone[] = {
    CNA_GAMER_ZONE_UNKNOWN,
    CNA_GAMER_ZONE_RECREATION,
    CNA_GAMER_ZONE_PRO,
    CNA_GAMER_ZONE_FAMILY,
    CNA_GAMER_ZONE_UNDERGROUND,
};

static const CNA_LeaderboardKey leaderboardKey[] = {
    CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME,
    CNA_LEADERBOARD_KEY_BEST_SCORE_RECENT,
    CNA_LEADERBOARD_KEY_BEST_TIME_LIFE_TIME,
    CNA_LEADERBOARD_KEY_BEST_TIME_RECENT,
};

static const CNA_LeaderboardOutcome leaderboardOutcome[] = {
    CNA_LEADERBOARD_OUTCOME_NONE,
    CNA_LEADERBOARD_OUTCOME_WIN,
    CNA_LEADERBOARD_OUTCOME_LOSS,
    CNA_LEADERBOARD_OUTCOME_TIE,
};

static const CNA_MessageBoxIcon messageBoxIcon[] = {
    CNA_MESSAGE_BOX_ICON_NONE,
    CNA_MESSAGE_BOX_ICON_ERROR,
    CNA_MESSAGE_BOX_ICON_WARNING,
    CNA_MESSAGE_BOX_ICON_ALERT,
};

static const CNA_ControllerSensitivity controllerSensitivity[] = {
    CNA_CONTROLLER_SENSITIVITY_LOW,
    CNA_CONTROLLER_SENSITIVITY_MEDIUM,
    CNA_CONTROLLER_SENSITIVITY_HIGH,
};

static const CNA_GameDifficulty gameDifficulty[] = {
    CNA_GAME_DIFFICULTY_EASY,
    CNA_GAME_DIFFICULTY_NORMAL,
    CNA_GAME_DIFFICULTY_HARD,
};

static const CNA_GamerPrivilegeSetting gamerPrivilegeSetting[] = {
    CNA_GAMER_PRIVILEGE_SETTING_BLOCKED,
    CNA_GAMER_PRIVILEGE_SETTING_FRIENDS_ONLY,
    CNA_GAMER_PRIVILEGE_SETTING_EVERYONE,
};

static const CNA_RacingCameraAngle racingCameraAngle[] = {
    CNA_RACING_CAMERA_ANGLE_BACK,
    CNA_RACING_CAMERA_ANGLE_FRONT,
    CNA_RACING_CAMERA_ANGLE_INSIDE,
};

static int ordinals_are_canonical(const uint32_t* const values, const uint64_t count,
                                  const uint32_t maximum)
{
    uint64_t index;
    for (index = UINT64_C(0); index < count; ++index) {
        if (values[index] != (uint32_t)index) {
            return 0;
        }
    }
    return count != UINT64_C(0) && maximum == (uint32_t)(count - UINT64_C(1));
}

int main(void)
{
    if (!ordinals_are_canonical(gamerPresenceMode, COUNT_OF(gamerPresenceMode),
                                CNA_GAMER_PRESENCE_MODE_MAXIMUM)) {
        return 1;
    }
    if (!ordinals_are_canonical(notificationPosition, COUNT_OF(notificationPosition),
                                CNA_NOTIFICATION_POSITION_MAXIMUM)) {
        return 2;
    }
    if (!ordinals_are_canonical(gamerZone, COUNT_OF(gamerZone),
                                CNA_GAMER_ZONE_MAXIMUM)) {
        return 3;
    }
    if (!ordinals_are_canonical(leaderboardKey, COUNT_OF(leaderboardKey),
                                CNA_LEADERBOARD_KEY_MAXIMUM)) {
        return 4;
    }
    if (!ordinals_are_canonical(leaderboardOutcome, COUNT_OF(leaderboardOutcome),
                                CNA_LEADERBOARD_OUTCOME_MAXIMUM)) {
        return 5;
    }
    if (!ordinals_are_canonical(messageBoxIcon, COUNT_OF(messageBoxIcon),
                                CNA_MESSAGE_BOX_ICON_MAXIMUM)) {
        return 6;
    }
    if (!ordinals_are_canonical(controllerSensitivity, COUNT_OF(controllerSensitivity),
                                CNA_CONTROLLER_SENSITIVITY_MAXIMUM)) {
        return 7;
    }
    if (!ordinals_are_canonical(gameDifficulty, COUNT_OF(gameDifficulty),
                                CNA_GAME_DIFFICULTY_MAXIMUM)) {
        return 8;
    }
    if (!ordinals_are_canonical(gamerPrivilegeSetting, COUNT_OF(gamerPrivilegeSetting),
                                CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM)) {
        return 9;
    }
    if (!ordinals_are_canonical(racingCameraAngle, COUNT_OF(racingCameraAngle),
                                CNA_RACING_CAMERA_ANGLE_MAXIMUM)) {
        return 10;
    }

    /* Two spot checks that the table cannot make on its own: the identity is fixed-width, and
       the canonical vocabulary really does end with XNA's own joke presence mode. */
    if (sizeof(CNA_GamerPresenceMode) != sizeof(uint32_t) ||
        CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE != CNA_GAMER_PRESENCE_MODE_MAXIMUM ||
        CNA_GAMER_PRESENCE_MODE_NONE != UINT32_C(0) ||
        CNA_NOTIFICATION_POSITION_CENTER != UINT32_C(4) ||
        CNA_GAMER_ZONE_UNKNOWN != UINT32_C(0) ||
        CNA_LEADERBOARD_KEY_BEST_TIME_RECENT != UINT32_C(3) ||
        CNA_MESSAGE_BOX_ICON_ALERT != UINT32_C(3) ||
        CNA_GAME_DIFFICULTY_NORMAL != UINT32_C(1) ||
        CNA_GAMER_PRIVILEGE_SETTING_EVERYONE != UINT32_C(2) ||
        CNA_RACING_CAMERA_ANGLE_INSIDE != UINT32_C(2) ||
        CNA_CONTROLLER_SENSITIVITY_HIGH != UINT32_C(2) ||
        CNA_LEADERBOARD_OUTCOME_TIE != UINT32_C(3)) {
        return 11;
    }
    return 0;
}
