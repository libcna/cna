// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    class NetworkGamer;

    /**
     * @brief Contains data for the GamerLeft event.
     */
    class GamerLeftEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of GamerLeftEventArgs.
         *
         * @param gamer The gamer who left the session.
         */
        explicit GamerLeftEventArgs(NetworkGamer* gamer);

        /**
         * @brief Gets the gamer who left the session.
         *
         * @return Pointer to the NetworkGamer.
         */
        [[nodiscard]] NetworkGamer* getGamerProperty() const;

    private:
        NetworkGamer* gamer_;
    };
}
