// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"
#include <functional>

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
         * Task 4.3 (plans/plan_net.md Phase 4): real XNA's LeaderboardWriter has no explicit "submit"/
         * "commit" method anywhere in its API surface (real Xbox 360 submission happened via
         * out-of-scope Xbox LIVE session infrastructure) - setting Rating is the closest honest
         * analog to "I'm done configuring this entry, persist it" for a writer-returned entry, so
         * it immediately persists (Rating and whatever Columns are already set at that moment) to
         * the local store when SetOnRatingChangedHookEXT() has installed a hook. A no-op for
         * reader-returned entries (LeaderboardReader never installs this hook).
         *
         * @param value The rating.
         */
        void setRatingProperty(long long value);

        /**
         * @brief CNAEXT/internal: installs the callback LeaderboardWriter uses to persist this
         * entry to the local store whenever setRatingProperty() runs. Not part of the real XNA
         * 4.0 API - purely internal wiring between LeaderboardWriter and its own returned entries,
         * invisible to any caller.
         *
         * @param hook The callback to invoke after Rating changes; pass an empty std::function to
         *        clear it.
         */
        CNAEXT void SetOnRatingChangedHookEXT(std::function<void()> hook);

        /**
         * @brief Gets the ranking of this entry on the leaderboard.
         *
         * Task 7.10: `RankingEXT` is FNA's own extension, not part of the real XNA 4.0 API
         * surface (real XNA's LeaderboardEntry exposes only Columns/Gamer/Rating) - FNA's own
         * reference source already names it with the "EXT" suffix for exactly this reason.
         *
         * @return The ranking.
         */
        CNAEXT [[nodiscard]] int getRankingEXTProperty() const;

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
        CNAEXT [[nodiscard]] bool operator==(const LeaderboardEntry& other) const;

        /**
         * @brief Determines whether this entry differs from another.
         *
         * @param other The entry to compare against.
         * @return true if any of gamer, rating, or ranking differ.
         */
        CNAEXT [[nodiscard]] bool operator!=(const LeaderboardEntry& other) const;

        /** @brief Creates a LeaderboardEntry for CNA internal use. */
        CNAEXT static LeaderboardEntry CreateInternal(Gamer* gamer, long long rating, int ranking);

    private:
        LeaderboardEntry(Gamer* gamer, long long rating, int ranking);

        Gamer* gamer_;
        long long rating_;
        int rankingEXT_;
        PropertyDictionary columns_;
        std::function<void()> onRatingChangedEXT_;
    };
}
