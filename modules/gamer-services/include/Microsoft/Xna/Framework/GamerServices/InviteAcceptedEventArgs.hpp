// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamer;

    /**
     * @brief Contains data for the InviteAccepted event.
     */
    class InviteAcceptedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of InviteAcceptedEventArgs.
         *
         * @param gamer            The gamer who accepted the invite.
         * @param isCurrentSession true if the invite was for the current session.
         */
        InviteAcceptedEventArgs(SignedInGamer* gamer, bool isCurrentSession);

        /**
         * @brief Gets the gamer who accepted the invite.
         *
         * @return Pointer to the SignedInGamer.
         */
        [[nodiscard]] SignedInGamer* getGamerProperty() const;

        /**
         * @brief Gets whether the invite is for the current session.
         *
         * @return true if this is the current session.
         */
        [[nodiscard]] bool getIsCurrentSessionProperty() const;

    private:
        SignedInGamer* gamer_;
        bool isCurrentSession_;
    };
}
