// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include <map>
#include <string>
#include <set>
#include <memory>

namespace Microsoft::Xna::Framework::Net { class NetworkSession; }
namespace CNA::Internal::GamerServices { struct ServiceLeaderboardWrite; }

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
         * retain immediate Rating persistence; LocalWithLeaderboards also saves subsequent
         * column changes when EndGame is processed, before GameEnded. The returned entry is owned by this writer and
         * remains valid until its gamer is destroyed.
         *
         * @param leaderboardId The leaderboard to query.
         * @return A real, mutable LeaderboardEntry for the owning gamer on this leaderboard.
         */
        [[nodiscard]] LeaderboardEntry* GetLeaderboard(const LeaderboardIdentity& leaderboardId);

    private:
        friend class Gamer;
        friend class Microsoft::Xna::Framework::Net::NetworkSession;
        explicit LeaderboardWriter(Gamer* owner);
        struct WriteScope {bool active=false;std::set<std::string> dirty;};
        void BeginServiceGameplay();
        void EndServiceGameplay();
        void BindServiceEntry(const std::string& fileKey,LeaderboardEntry& entry);
        void FlushOfflineWrites();
        std::vector<CNA::Internal::GamerServices::ServiceLeaderboardWrite> CollectServiceWrites() const;

        Gamer* owner_ = nullptr;
        std::map<std::string, LeaderboardEntry> entriesByLeaderboardKeyEXT_;
        // Canonical bytes, not shallow dictionaries: a retained stream/reference can change
        // without calling a setter. Unchanged cached rows must not overwrite another writer.
        std::map<std::string, std::string> persistedColumns_;
        std::map<std::string,LeaderboardIdentity> identities_;
        std::shared_ptr<WriteScope> writeScope_;
    };
}
