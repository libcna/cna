// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInEventArgs::SignedInEventArgs(SignedInGamer* gamer)
        : gamer_(gamer)
    {
        // XNA SignedInEventArgs::.ctor, IL_0006..0013 rejects a null gamer.
        if (gamer == nullptr) throw System::ArgumentNullException("gamer");
    }

    SignedInGamer* SignedInEventArgs::getGamerProperty() const
    {
        return gamer_;
    }
}
