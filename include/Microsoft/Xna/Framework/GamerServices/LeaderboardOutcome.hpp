// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the outcome of a ranked match for leaderboard purposes.
     */
    enum class LeaderboardOutcome
    {
        /** @brief No outcome recorded. */
        None,
        /** @brief The player won the match. */
        Win,
        /** @brief The player lost the match. */
        Loss,
        /** @brief The match ended in a tie. */
        Tie
    };
}
