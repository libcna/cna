// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "System/NotImplementedException.hpp"

namespace Microsoft::Xna::Framework::Net
{
    NetworkMachine::NetworkMachine()
        : gamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
    {
    }

    NetworkMachine NetworkMachine::CreateInternal()
    {
        return NetworkMachine();
    }

    const GamerServices::GamerCollection<NetworkGamer>& NetworkMachine::getGamersProperty() const
    {
        return gamers_;
    }

    void NetworkMachine::RemoveFromSession()
    {
        throw System::NotImplementedException();
    }
}
