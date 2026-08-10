// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    class NetworkGamer;

    /**
     * @brief Contains data for the GamerJoined event.
     */
    class GamerJoinedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of GamerJoinedEventArgs.
         *
         * @param gamer The gamer who joined the session.
         */
        explicit GamerJoinedEventArgs(NetworkGamer* gamer);

        /**
         * @brief Gets the gamer who joined the session.
         *
         * @return Pointer to the NetworkGamer.
         */
        [[nodiscard]] NetworkGamer* getGamerProperty() const;

    private:
        NetworkGamer* gamer_;
    };
}
