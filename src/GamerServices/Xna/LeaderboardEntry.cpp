// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    LeaderboardEntry::LeaderboardEntry(Gamer* gamer, long long rating, int ranking)
        : gamer_(gamer)
        , rating_(rating)
        , rankingEXT_(ranking)
        , columns_(PropertyDictionary::CreateInternal({}))
    {
    }

    LeaderboardEntry LeaderboardEntry::CreateInternal(Gamer* gamer, long long rating, int ranking)
    {
        return LeaderboardEntry(gamer, rating, ranking);
    }

    PropertyDictionary& LeaderboardEntry::getColumnsProperty()             { return columns_; }
    const PropertyDictionary& LeaderboardEntry::getColumnsProperty() const { return columns_; }
    Gamer* LeaderboardEntry::getGamerProperty() const                      { return gamer_; }
    long long LeaderboardEntry::getRatingProperty() const                  { return rating_; }
    int LeaderboardEntry::getRankingEXTProperty() const                    { return rankingEXT_; }

    void LeaderboardEntry::setRatingProperty(long long value)
    {
        rating_ = value;
        if (onRatingChangedEXT_)
        {
            onRatingChangedEXT_();
        }
    }

    void LeaderboardEntry::SetOnRatingChangedHookEXT(std::function<void()> hook)
    {
        onRatingChangedEXT_ = std::move(hook);
    }

    bool LeaderboardEntry::operator==(const LeaderboardEntry& other) const
    {
        return gamer_ == other.gamer_ && rating_ == other.rating_ && rankingEXT_ == other.rankingEXT_;
    }

    bool LeaderboardEntry::operator!=(const LeaderboardEntry& other) const
    {
        return !(*this == other);
    }
}
