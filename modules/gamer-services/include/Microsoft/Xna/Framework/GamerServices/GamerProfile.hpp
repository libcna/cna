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
         * @brief Gets the gamer's overall gamerscore.
         *
         * @return The gamerscore value.
         */
        [[nodiscard]] int getGamerScoreProperty() const;

        /**
         * @brief Gets the gaming zone the gamer has selected.
         *
         * @return The GamerZone value.
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
         * @brief Gets the gamer's reputation score.
         *
         * @return The reputation value.
         */
        [[nodiscard]] float getReputationProperty() const;

        /**
         * @brief Gets the number of titles the gamer has played.
         *
         * @return The titles-played count.
         */
        [[nodiscard]] int getTitlesPlayedProperty() const;

        /**
         * @brief Gets the total number of achievements earned by the gamer.
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
         * @return Always nullptr in this platform's implementation.
         */
        [[nodiscard]] System::IO::Stream* GetGamerPicture() const;

        /** @brief Creates a GamerProfile for CNA internal use. */
        NOXNA static GamerProfile CreateInternal();

    private:
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
