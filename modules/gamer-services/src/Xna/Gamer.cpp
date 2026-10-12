// SPDX-License-Identifier: MS-PL
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

    Gamer::Gamer(const std::string& gamertag, std::optional<std::string> displayName)
        // Task 10.1: matches FNA's `displayName ?? gamertag` exactly - substitutes only when
        // displayName was never supplied, never for an explicitly-passed empty string.
        : displayName_(displayName.has_value() ? std::move(*displayName) : gamertag)
        , gamertag_(gamertag)
        // Task 4.3 (plans/plan_net.md Phase 4): LeaderboardWriter needs to know its owning gamer to
        // persist to the right per-gamer local store entry - `this` is a valid pointer value in a
        // member-initializer-list (LeaderboardWriter's constructor only stores it, never
        // dereferences it before Gamer itself is fully constructed).
        , leaderboardWriter_(this)
    {
    }

    Gamer::Gamer(const Gamer& other)
        : serviceUserId_(other.serviceUserId_), displayName_(other.displayName_), gamertag_(other.gamertag_)
        , isDisposed_(other.isDisposed_), tag_(other.tag_), leaderboardWriter_(this)
    {
    }

    Gamer::Gamer(Gamer&& other) noexcept
        : serviceUserId_(std::move(other.serviceUserId_)), displayName_(std::move(other.displayName_))
        , gamertag_(std::move(other.gamertag_)), isDisposed_(other.isDisposed_), tag_(std::move(other.tag_))
        , leaderboardWriter_(this)
    {
    }

    Gamer& Gamer::operator=(const Gamer& other)
    {
        if (this != &other)
        {
            serviceUserId_ = other.serviceUserId_; displayName_ = other.displayName_; gamertag_ = other.gamertag_;
            isDisposed_ = other.isDisposed_; tag_ = other.tag_; leaderboardWriter_ = LeaderboardWriter(this);
        }
        return *this;
    }

    Gamer& Gamer::operator=(Gamer&& other) noexcept
    {
        if (this != &other)
        {
            serviceUserId_ = std::move(other.serviceUserId_); displayName_ = std::move(other.displayName_);
            gamertag_ = std::move(other.gamertag_); isDisposed_ = other.isDisposed_; tag_ = std::move(other.tag_);
            leaderboardWriter_ = LeaderboardWriter(this);
        }
        return *this;
    }

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
