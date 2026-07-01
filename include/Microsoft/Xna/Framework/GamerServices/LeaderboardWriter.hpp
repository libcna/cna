// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    class LeaderboardEntry;

    /**
     * @brief Provides access to a single leaderboard entry for the local gamer.
     */
    class LeaderboardWriter final
    {
    public:
        /**
         * @brief Gets the leaderboard entry identified by the given leaderboard identity.
         *
         * @param leaderboardId The leaderboard to query.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] LeaderboardEntry* GetLeaderboard(const LeaderboardIdentity& leaderboardId);
    };
}
