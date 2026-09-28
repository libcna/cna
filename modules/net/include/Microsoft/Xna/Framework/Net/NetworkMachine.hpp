// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp"

namespace Microsoft::Xna::Framework::Net
{
    class NetworkGamer;

    /**
     * @brief Represents the local network machine hosting one or more NetworkGamer instances.
     */
    class NetworkMachine final
    {
    public:
        /**
         * @brief Gets the gamers associated with this machine.
         *
         * @return Const reference to the gamer collection.
         */
        [[nodiscard]] const GamerServices::GamerCollection<NetworkGamer>& getGamersProperty() const;

        /**
         * @brief Removes this machine from the session: its gamers leave for everyone else, and
         * its own session ends with NetworkSessionEndReason::RemovedByHost.
         *
         * @throws System::ObjectDisposedException if the machine has no gamers.
         * @throws System::InvalidOperationException if the machine has left the session, is the
         *         local machine, or this machine is not the host.
         */
        void RemoveFromSession() const;

        /** @brief Creates a NetworkMachine for CNA internal use. */
        CNAEXT static NetworkMachine CreateInternal();

        /**
         * @brief Records a gamer playing on this machine; used by the session that projects it.
         *
         * @param gamer Non-owning gamer pointer, retained by its session.
         */
        CNAEXT void AddGamerInternal(NetworkGamer* gamer);

        /**
         * @brief Forgets a gamer that left this machine's session.
         *
         * @param gamer Gamer previously added with AddGamerInternal.
         */
        CNAEXT void RemoveGamerInternal(NetworkGamer* gamer);

    private:
        NetworkMachine();

        GamerServices::GamerCollection<NetworkGamer> gamers_;
    };
}
