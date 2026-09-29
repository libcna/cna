// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/IO/MemoryStream.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    // CNA keeps no gamer zone or reputation, so a profile reports none rather than invent one.
    GamerProfile::GamerProfile()
        : gamerScore_(0)
        , gamerZone_(GamerZone::Unknown)
        , region_(System::Globalization::RegionInfo::getCurrentRegionProperty())
        , reputation_(0.0f)
        , titlesPlayed_(0)
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
        if(isDisposed_)throw System::ObjectDisposedException("GamerProfile");
        if(pictureHash_.empty())return nullptr;
        const auto bytes=CNA::Internal::GamerServices::backend()->asset(pictureHash_);
        return new System::IO::MemoryStream(bytes.data(),static_cast<SharpRuntime::intcs>(bytes.size()),false);
    }
}
