// SPDX-License-Identifier: MS-PL
// Gamer's constructors and assignment. Apart from Gamer.cpp because they build the gamer's
// LeaderboardWriter, which reaches the local leaderboard and achievement store: reading who is
// signed in must not link that.
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    Gamer::Gamer(const std::string& gamertag, std::optional<std::string> displayName)
        // Task 10.1: matches FNA's `displayName ?? gamertag` exactly - substitutes only when
        // displayName was never supplied, never for an explicitly-passed empty string.
        : displayName_(displayName.has_value() ? std::move(*displayName) : gamertag)
        , gamertag_(gamertag)
        // Net Task 4.3: LeaderboardWriter needs to know its owning gamer to
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
}
