// SPDX-License-Identifier: MS-PL
// Gamer's identity and the signed-in collection, which the Guide's license and marketplace checks
// read. Link boundary: constructing a Gamer builds its LeaderboardWriter, which reaches the local
// leaderboard and achievement store, so construction is GamerConstruction.cpp -- only sign-in and
// lookups construct gamers.
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/NotSupportedException.hpp"
#include "../Internal/ServiceAsyncResult.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInGamerCollection* Gamer::signedInGamers_ = nullptr;

    const std::string& Gamer::getDisplayNameProperty() const  { return displayName_; }
    void Gamer::setDisplayNameProperty(const std::string& v)  { displayName_ = v; }
    const std::string& Gamer::getGamertagProperty() const     { return gamertag_; }
    bool Gamer::getIsDisposedProperty() const                 { return isDisposed_; }
    LeaderboardWriter& Gamer::getLeaderboardWriterProperty()  { return leaderboardWriter_; }
    std::any& Gamer::getTagProperty()                         { return tag_; }
    void Gamer::setTagProperty(const std::any& v)             { tag_ = v; }

    SignedInGamerCollection* Gamer::getSignedInGamersProperty()
    {
        if (!signedInGamers_)
            signedInGamers_ = new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}));
        return signedInGamers_;
    }

    void Gamer::setSignedInGamersProperty(SignedInGamerCollection* value)
    {
        if (signedInGamers_ != value)
        {
            delete signedInGamers_;
            signedInGamers_ = value;
        }
    }

    GetTypeNameCPP(Gamer, "Microsoft.Xna.Framework.GamerServices.Gamer")

    std::string Gamer::ToString() const
    {
        return displayName_;
    }
}
