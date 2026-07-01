// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    class Gamer;

    /**
     * @brief Represents a single row within a leaderboard.
     */
    class LeaderboardEntry
    {
    public:
        /**
         * @brief Gets the property dictionary holding additional leaderboard columns.
         *
         * @return Reference to the column property dictionary.
         */
        [[nodiscard]] PropertyDictionary& getColumnsProperty();

        /**
         * @brief Gets the property dictionary holding additional leaderboard columns.
         *
         * @return Const reference to the column property dictionary.
         */
        [[nodiscard]] const PropertyDictionary& getColumnsProperty() const;

        /**
         * @brief Gets the gamer associated with this leaderboard entry.
         *
         * @return Pointer to the Gamer, or nullptr if none was supplied.
         */
        [[nodiscard]] Gamer* getGamerProperty() const;

        /**
         * @brief Gets the rating value for this entry.
         *
         * @return The rating.
         */
        [[nodiscard]] long long getRatingProperty() const;

        /**
         * @brief Sets the rating value for this entry.
         *
         * @param value The rating.
         */
        void setRatingProperty(long long value);

        /**
         * @brief Gets the ranking of this entry on the leaderboard.
         *
         * @return The ranking.
         */
        [[nodiscard]] int getRankingEXTProperty() const;

        /**
         * @brief Determines whether this entry is structurally equal to another.
         *
         * FNA's LeaderboardEntry has no custom equality (falls back to reference identity);
         * since entries are stored by value here, structural comparison of gamer/rating/ranking
         * is the closest achievable equivalent. Required by ReadOnlyCollection<T>::IndexOf/Contains.
         *
         * @param other The entry to compare against.
         * @return true if gamer, rating, and ranking are all equal.
         */
        NOXNA [[nodiscard]] bool operator==(const LeaderboardEntry& other) const;

        /**
         * @brief Determines whether this entry differs from another.
         *
         * @param other The entry to compare against.
         * @return true if any of gamer, rating, or ranking differ.
         */
        NOXNA [[nodiscard]] bool operator!=(const LeaderboardEntry& other) const;

        /** @brief Creates a LeaderboardEntry for CNA internal use. */
        NOXNA static LeaderboardEntry CreateInternal(Gamer* gamer, long long rating, int ranking);

    private:
        LeaderboardEntry(Gamer* gamer, long long rating, int ranking);

        Gamer* gamer_;
        long long rating_;
        int rankingEXT_;
        PropertyDictionary columns_;
    };
}
