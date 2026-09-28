// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

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

    void NetworkMachine::AddGamerInternal(NetworkGamer* gamer)
    {
        gamers_.Add(gamer);
    }

    void NetworkMachine::RemoveGamerInternal(NetworkGamer* gamer)
    {
        gamers_.Remove(gamer);
    }

    void NetworkMachine::RemoveFromSession() const
    {
        // Reference NetworkMachine.RemoveFromSession, in its validation order.
        if (gamers_.getCountProperty() == 0)
            throw System::ObjectDisposedException("NetworkMachine");
        NetworkGamer* gamer = gamers_[0];
        if (gamer->getHasLeftSessionProperty())
            throw System::InvalidOperationException("The machine has already left the session.");
        if (gamer->getIsLocalProperty())
            throw System::InvalidOperationException("The local machine cannot be removed from the session.");
        NetworkSession* session = gamer->getSessionProperty();
        if (session == nullptr || !session->getIsHostProperty())
            throw System::InvalidOperationException("Only the host can remove a machine from the session.");
        session->RemoveMachineInternal(gamer);
    }
}
