// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    GamerLeftEventArgs::GamerLeftEventArgs(NetworkGamer* gamer)
        : gamer_(gamer)
    {
    }

    NetworkGamer* GamerLeftEventArgs::getGamerProperty() const { return gamer_; }
}
