// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamer;

    /**
     * @brief Contains data for the SignedIn event.
     */
    class SignedInEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of SignedInEventArgs.
         *
         * @param gamer The gamer who signed in.
         */
        explicit SignedInEventArgs(SignedInGamer* gamer);

        /**
         * @brief Gets the gamer who signed in.
         *
         * @return Pointer to the SignedInGamer.
         */
        [[nodiscard]] SignedInGamer* getGamerProperty() const;

    private:
        SignedInGamer* gamer_;
    };
}
