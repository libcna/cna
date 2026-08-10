// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Identifies the type of a leaderboard.
     */
    enum class LeaderboardKey
    {
        /** @brief Best score over the lifetime of the title. */
        BestScoreLifeTime,
        /** @brief Best score over a recent time window. */
        BestScoreRecent,
        /** @brief Best time over the lifetime of the title. */
        BestTimeLifeTime,
        /** @brief Best time over a recent time window. */
        BestTimeRecent
    };
}
