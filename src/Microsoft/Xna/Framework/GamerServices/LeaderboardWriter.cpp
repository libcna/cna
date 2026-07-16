// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    LeaderboardWriter::LeaderboardWriter(Gamer* owner)
        : owner_(owner)
    {
    }

    LeaderboardEntry* LeaderboardWriter::GetLeaderboard(const LeaderboardIdentity& leaderboardId)
    {
        const std::string fileKey = CNA::Internal::GamerServices::MakeLeaderboardFileKeyEXT(
            leaderboardId.getKeyProperty(), leaderboardId.getGameModeProperty()
        );

        auto existing = entriesByLeaderboardKeyEXT_.find(fileKey);
        if (existing != entriesByLeaderboardKeyEXT_.end())
        {
            return &existing->second;
        }

        // First access for this leaderboard from this writer: seed the entry from whatever is
        // already locally persisted for the owning gamer (0 rating, no columns, if nothing was
        // ever written before).
        long long rating = 0;
        for (const auto& record : CNA::Internal::GamerServices::LoadLeaderboardEntriesEXT(fileKey))
        {
            if (record.Gamertag == owner_->getGamertagProperty())
            {
                rating = record.Rating;
                break;
            }
        }

        // RankingEXT is meaningless for a writer-only entry (real rank only makes sense within a
        // full, sorted leaderboard read) - 0, matching this class's own lack of any read/sort
        // context.
        LeaderboardEntry entry = LeaderboardEntry::CreateInternal(owner_, rating, 0);
        CNA::Internal::GamerServices::LoadLeaderboardEntryColumnsEXT(
            fileKey, owner_->getGamertagProperty(), entry.getColumnsProperty()
        );

        auto [inserted, didInsert] = entriesByLeaderboardKeyEXT_.emplace(fileKey, std::move(entry));
        (void) didInsert; // always true here - the find() above already ruled out an existing entry
        LeaderboardEntry* result = &inserted->second;

        // See LeaderboardEntry::setRatingProperty()'s own doc comment for why Rating-set is the
        // real XNA-faithful trigger for "persist this now" (no explicit commit/submit method
        // exists anywhere in the real LeaderboardWriter API surface).
        Gamer* owner = owner_;
        result->SetOnRatingChangedHookEXT([result, fileKey, owner]() {
            CNA::Internal::GamerServices::PersistedLeaderboardEntry record;
            record.Gamertag = owner->getGamertagProperty();
            record.Rating = result->getRatingProperty();
            CNA::Internal::GamerServices::SaveLeaderboardEntryEXT(fileKey, record, &result->getColumnsProperty());
        });

        return result;
    }
}
