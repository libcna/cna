// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInEventArgs::SignedInEventArgs(SignedInGamer* gamer)
        : gamer_(gamer)
    {
    }

    SignedInGamer* SignedInEventArgs::getGamerProperty() const
    {
        return gamer_;
    }
}
