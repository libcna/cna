// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedOutEventArgs::SignedOutEventArgs(SignedInGamer* gamer)
        : gamer_(gamer)
    {
        // XNA SignedOutEventArgs::.ctor, IL_0006..0013 rejects a null gamer.
        if (gamer == nullptr) throw System::ArgumentNullException("gamer");
    }

    SignedInGamer* SignedOutEventArgs::getGamerProperty() const
    {
        return gamer_;
    }
}
