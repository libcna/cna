// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    HostChangedEventArgs::HostChangedEventArgs(NetworkGamer* oldHost, NetworkGamer* newHost)
        : oldHost_(oldHost)
        , newHost_(newHost)
    {
    }

    NetworkGamer* HostChangedEventArgs::getOldHostProperty() const { return oldHost_; }
    NetworkGamer* HostChangedEventArgs::getNewHostProperty() const { return newHost_; }
}
