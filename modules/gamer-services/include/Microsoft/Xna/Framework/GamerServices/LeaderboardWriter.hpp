// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include <map>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    class Gamer;

    /**
     * @brief Provides access to a single leaderboard entry for the local gamer.
     */
    class LeaderboardWriter final
    {
    public:
        /**
         * @brief Gets the leaderboard entry identified by the given leaderboard identity.
         *
         * Service identities keep mutable writes in memory. Explicit offline factory objects
         * retain local fixture persistence. The returned entry is owned by this writer and
         * remains valid until its gamer is destroyed.
         *
         * @param leaderboardId The leaderboard to query.
         * @return A real, mutable LeaderboardEntry for the owning gamer on this leaderboard.
         */
        [[nodiscard]] LeaderboardEntry* GetLeaderboard(const LeaderboardIdentity& leaderboardId);

    private:
        friend class Gamer;
        explicit LeaderboardWriter(Gamer* owner);

        Gamer* owner_ = nullptr;
        std::map<std::string, LeaderboardEntry> entriesByLeaderboardKeyEXT_;
    };
}
