// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    GamerJoinedEventArgs::GamerJoinedEventArgs(NetworkGamer* gamer)
        : gamer_(gamer)
    {
    }

    NetworkGamer* GamerJoinedEventArgs::getGamerProperty() const { return gamer_; }
}
