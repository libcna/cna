// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInGamerCollection::SignedInGamerCollection(std::vector<SignedInGamer*> gamers)
        : GamerCollection<SignedInGamer>(std::move(gamers))
    {
    }

    SignedInGamerCollection SignedInGamerCollection::CreateInternal(std::vector<SignedInGamer*> gamers)
    {
        return SignedInGamerCollection(std::move(gamers));
    }

    SignedInGamer* SignedInGamerCollection::operator[](Microsoft::Xna::Framework::PlayerIndex index) const
    {
        // XNA IL (SignedInGamerCollection.get_Item(PlayerIndex)) returns the gamer whose
        // PlayerIndex matches, or null; the FNA-era port indexed the collection by the ordinal,
        // which named the wrong gamer whenever a lower player was not signed in.
        for (SignedInGamer* gamer : collection_)
        {
            if (gamer->getPlayerIndexProperty() == index)
                return gamer;
        }
        return nullptr;
    }
}
