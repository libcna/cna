// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerZone.hpp"
#include "System/Globalization/RegionInfo.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/Stream.hpp"
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Represents a snapshot of a gamer's public profile information.
     */
    class GamerProfile final : public System::IDisposable
    {
    public:
        /**
         * @brief Gets the gamer's overall gamerscore: the scores of every achievement earned, across
         * titles on the CNA service, or in this title's catalog offline.
         *
         * @return The gamerscore value.
         */
        [[nodiscard]] int getGamerScoreProperty() const;

        /**
         * @brief Gets the gaming zone the gamer has selected.
         *
         * @return The zone an account chose in the Guide (CNA service); GamerZone::Unknown for a
         *         local offline profile or an account that never chose one.
         */
        [[nodiscard]] GamerZone getGamerZoneProperty() const;

        /**
         * @brief Gets the gamer's motto.
         *
         * @return The motto string.
         */
        [[nodiscard]] const std::string& getMottoProperty() const;

        /**
         * @brief Gets the gamer's region.
         *
         * @return The RegionInfo describing this gamer's region.
         */
        [[nodiscard]] const System::Globalization::RegionInfo& getRegionProperty() const;

        /**
         * @brief Gets the gamer's reputation, as a number of stars ranging 0 to 5.
         *
         * @return With a CNA service, stars in quarters from other players' reviews (a CNA formula:
         *         5 x prefer / (prefer + avoid)); 0 for an unreviewed account or a local profile.
         */
        [[nodiscard]] float getReputationProperty() const;

        /**
         * @brief Gets the number of titles the gamer has played: on the CNA service, those with the
         * gamer's presence, an earned achievement or a leaderboard row; offline, this title once the
         * gamer has earned an achievement in it.
         *
         * @return The titles-played count.
         */
        [[nodiscard]] int getTitlesPlayedProperty() const;

        /**
         * @brief Gets the total number of achievements earned by the gamer, counted as the
         * gamerscore is.
         *
         * @return The total achievements count.
         */
        [[nodiscard]] int getTotalAchievementsProperty() const;

        /**
         * @brief Gets whether this profile has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Releases the resources held by this profile.
         */
        void Dispose() override;

        /**
         * @brief Gets a stream containing the gamer's profile picture.
         *
         * @return A caller-owned read-only stream at position zero, or null if no picture is configured.
         * @throws System::ObjectDisposedException if the profile is disposed.
         */
        [[nodiscard]] System::IO::Stream* GetGamerPicture() const;

        /** @brief Creates a GamerProfile for CNA internal use. */
        CNAEXT static GamerProfile CreateInternal();

    private:
        friend class Gamer;
        std::string pictureHash_;
        GamerProfile();

        int gamerScore_;
        GamerZone gamerZone_;
        std::string motto_;
        System::Globalization::RegionInfo region_;
        float reputation_;
        int titlesPlayed_;
        int totalAchievements_;
        bool isDisposed_;
    };
}
