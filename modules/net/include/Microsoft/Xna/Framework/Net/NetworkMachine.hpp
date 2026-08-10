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
         * @brief Removes this machine's gamers from the network session.
         *
         * Always throws NotImplementedException, matching FNA's stub.
         */
        void RemoveFromSession();

        /** @brief Creates a NetworkMachine for CNA internal use. */
        CNAEXT static NetworkMachine CreateInternal();

    private:
        NetworkMachine();

        GamerServices::GamerCollection<NetworkGamer> gamers_;
    };
}
