// SPDX-License-Identifier: MS-PL
// SignedInGamer's construction. Apart from SignedInGamer.cpp, whose getters the Guide's license and
// marketplace checks read, because a gamer's construction builds its defaults, presence,
// privileges and (through Gamer) its LeaderboardWriter -- only sign-in constructs one.
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInGamer::SignedInGamer(
        const std::string& gamertag,
        bool isSignedInToLive,
        bool isGuest,
        Microsoft::Xna::Framework::PlayerIndex playerIndex
    )
        : Gamer(gamertag, gamertag)
        , isGuest_(isGuest)
        , isSignedInToLive_(isSignedInToLive)
        , playerIndex_(playerIndex)
        , gameDefaults_(GameDefaults::CreateInternal())
        , presence_(GamerPresence::CreateInternal())
        , privileges_(GamerPrivileges::CreateInternal())
        , partySize_(0)
    {
    }

    SignedInGamer SignedInGamer::CreateInternal(
        const std::string& gamertag,
        bool isSignedInToLive,
        bool isGuest,
        Microsoft::Xna::Framework::PlayerIndex playerIndex
    ) {
        return SignedInGamer(gamertag, isSignedInToLive, isGuest, playerIndex);
    }
}
