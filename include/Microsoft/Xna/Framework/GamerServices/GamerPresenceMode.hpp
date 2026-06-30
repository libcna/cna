// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes what a gamer is currently doing in a game.
     */
    enum class GamerPresenceMode
    {
        /** @brief No presence mode set. */
        None,
        /** @brief Playing in single-player mode. */
        SinglePlayer,
        /** @brief Playing in multiplayer mode. */
        Multiplayer,
        /** @brief Playing local co-op. */
        LocalCoOp,
        /** @brief Playing local versus. */
        LocalVersus,
        /** @brief Playing online co-op. */
        OnlineCoOp,
        /** @brief Playing online versus. */
        OnlineVersus,
        /** @brief Playing against the computer. */
        VersusComputer,
        /** @brief On a particular stage. */
        Stage,
        /** @brief On a particular level. */
        Level,
        /** @brief Playing a co-op stage. */
        CoOpStage,
        /** @brief Playing a co-op level. */
        CoOpLevel,
        /** @brief Playing in arcade mode. */
        ArcadeMode,
        /** @brief Playing in campaign mode. */
        CampaignMode,
        /** @brief Playing in challenge mode. */
        ChallengeMode,
        /** @brief Playing in exploration mode. */
        ExplorationMode,
        /** @brief Playing in practice mode. */
        PracticeMode,
        /** @brief Playing in puzzle mode. */
        PuzzleMode,
        /** @brief Playing in scenario mode. */
        ScenarioMode,
        /** @brief Playing in story mode. */
        StoryMode,
        /** @brief Playing in survival mode. */
        SurvivalMode,
        /** @brief Playing in tutorial mode. */
        TutorialMode,
        /** @brief Difficulty set to easy. */
        DifficultyEasy,
        /** @brief Difficulty set to medium. */
        DifficultyMedium,
        /** @brief Difficulty set to hard. */
        DifficultyHard,
        /** @brief Difficulty set to extreme. */
        DifficultyExtreme,
        /** @brief Displaying a score. */
        Score,
        /** @brief Displaying a versus score. */
        VersusScore,
        /** @brief Currently winning. */
        Winning,
        /** @brief Currently losing. */
        Losing,
        /** @brief Score is tied. */
        ScoreIsTied,
        /** @brief Outnumbered by opponents. */
        Outnumbered,
        /** @brief On a roll. */
        OnARoll,
        /** @brief In combat. */
        InCombat,
        /** @brief Battling a boss. */
        BattlingBoss,
        /** @brief In time attack mode. */
        TimeAttack,
        /** @brief Trying for a record. */
        TryingForRecord,
        /** @brief In free play. */
        FreePlay,
        /** @brief Wasting time. */
        WastingTime,
        /** @brief Stuck on a hard section. */
        StuckOnAHardBit,
        /** @brief Nearly finished. */
        NearlyFinished,
        /** @brief Looking for games. */
        LookingForGames,
        /** @brief Waiting for players. */
        WaitingForPlayers,
        /** @brief Waiting in the lobby. */
        WaitingInLobby,
        /** @brief Setting up a match. */
        SettingUpMatch,
        /** @brief Playing with friends. */
        PlayingWithFriends,
        /** @brief At the main menu. */
        AtMenu,
        /** @brief Starting a game. */
        StartingGame,
        /** @brief Game is paused. */
        Paused,
        /** @brief Game over. */
        GameOver,
        /** @brief Won the game. */
        WonTheGame,
        /** @brief Configuring settings. */
        ConfiguringSettings,
        /** @brief Customizing their player. */
        CustomizingPlayer,
        /** @brief Editing a level. */
        EditingLevel,
        /** @brief Browsing the in-game store. */
        InGameStore,
        /** @brief Watching a cutscene. */
        WatchingCutscene,
        /** @brief Watching the credits. */
        WatchingCredits,
        /** @brief Playing a minigame. */
        PlayingMinigame,
        /** @brief Found a secret. */
        FoundSecret,
        /** @brief Cornflower blue. */
        CornflowerBlue
    };
}
