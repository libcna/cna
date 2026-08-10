// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerProfile::GamerProfile()
        : gamerScore_(0)
        , gamerZone_(GamerZone::Pro)
        , region_(System::Globalization::RegionInfo::getCurrentRegionProperty())
        , reputation_(5.0f)
        , titlesPlayed_(1)
        , totalAchievements_(0)
        , isDisposed_(false)
    {
    }

    GamerProfile GamerProfile::CreateInternal()
    {
        return GamerProfile();
    }

    int GamerProfile::getGamerScoreProperty() const       { return gamerScore_; }
    GamerZone GamerProfile::getGamerZoneProperty() const   { return gamerZone_; }
    const std::string& GamerProfile::getMottoProperty() const { return motto_; }

    const System::Globalization::RegionInfo& GamerProfile::getRegionProperty() const
    {
        return region_;
    }

    float GamerProfile::getReputationProperty() const      { return reputation_; }
    int GamerProfile::getTitlesPlayedProperty() const      { return titlesPlayed_; }
    int GamerProfile::getTotalAchievementsProperty() const { return totalAchievements_; }
    bool GamerProfile::getIsDisposedProperty() const       { return isDisposed_; }

    void GamerProfile::Dispose()
    {
        isDisposed_ = true;
    }

    System::IO::Stream* GamerProfile::GetGamerPicture() const
    {
        return nullptr;
    }
}
