// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"

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
        int id = static_cast<int>(index);
        if (id >= static_cast<int>(collection_.size()))
            return nullptr;
        return collection_[static_cast<std::size_t>(id)];
    }
}
