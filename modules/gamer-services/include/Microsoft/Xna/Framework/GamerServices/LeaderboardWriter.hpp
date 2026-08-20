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
         * Task 4.3 (plans/plan_net.md Phase 4): loads (or creates, on first access) a real entry for
         * the owning gamer from the local store, keyed by LeaderboardIdentity. The returned
         * pointer is owned by this writer (stored by value in entriesByLeaderboardKeyEXT_, a
         * std::map - reference/pointer-stable across further insertions) and stays valid until
         * the owning Gamer is destroyed. Callers set Rating (and optionally Columns) on it; see
         * LeaderboardEntry::setRatingProperty()'s own doc comment for exactly when that gets
         * persisted.
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
