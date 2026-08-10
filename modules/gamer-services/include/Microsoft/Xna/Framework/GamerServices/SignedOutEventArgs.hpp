// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamer;

    /**
     * @brief Contains data for the SignedOut event.
     */
    class SignedOutEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of SignedOutEventArgs.
         *
         * @param gamer The gamer who signed out.
         */
        explicit SignedOutEventArgs(SignedInGamer* gamer);

        /**
         * @brief Gets the gamer who signed out.
         *
         * @return Pointer to the SignedInGamer.
         */
        [[nodiscard]] SignedInGamer* getGamerProperty() const;

    private:
        SignedInGamer* gamer_;
    };
}
