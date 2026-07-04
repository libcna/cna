// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedOutEventArgs::SignedOutEventArgs(SignedInGamer* gamer)
        : gamer_(gamer)
    {
    }

    SignedInGamer* SignedOutEventArgs::getGamerProperty() const
    {
        return gamer_;
    }
}
