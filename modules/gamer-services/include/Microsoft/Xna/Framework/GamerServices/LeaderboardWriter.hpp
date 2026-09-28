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
         * retain local fixture persistence. The returned entry is owned by this writer and
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
        std::vector<CNA::Internal::GamerServices::ServiceLeaderboardWrite> CollectServiceWrites() const;

        Gamer* owner_ = nullptr;
        std::map<std::string, LeaderboardEntry> entriesByLeaderboardKeyEXT_;
        std::map<std::string,LeaderboardIdentity> identities_;
        std::shared_ptr<WriteScope> writeScope_;
    };
}
