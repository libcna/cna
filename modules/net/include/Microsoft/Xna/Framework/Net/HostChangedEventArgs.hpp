// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    class NetworkGamer;

    /**
     * @brief Contains data for the HostChanged event.
     */
    class HostChangedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of HostChangedEventArgs.
         *
         * @param oldHost The gamer who was previously the host.
         * @param newHost The gamer who is now the host.
         */
        HostChangedEventArgs(NetworkGamer* oldHost, NetworkGamer* newHost);

        /**
         * @brief Gets the gamer who was previously the host.
         *
         * @return Pointer to the previous host's NetworkGamer.
         */
        [[nodiscard]] NetworkGamer* getOldHostProperty() const;

        /**
         * @brief Gets the gamer who is now the host.
         *
         * @return Pointer to the new host's NetworkGamer.
         */
        [[nodiscard]] NetworkGamer* getNewHostProperty() const;

    private:
        NetworkGamer* oldHost_;
        NetworkGamer* newHost_;
    };
}
