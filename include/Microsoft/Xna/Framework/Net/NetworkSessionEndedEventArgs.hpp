// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Contains data for the NetworkSession's Ended event.
     */
    class NetworkSessionEndedEventArgs : public System::EventArgs
    {
    public:
        /**
         * @brief Initializes a new instance of NetworkSessionEndedEventArgs.
         *
         * @param endReason The reason the session ended.
         */
        explicit NetworkSessionEndedEventArgs(NetworkSessionEndReason endReason);

        /**
         * @brief Gets the reason the session ended.
         *
         * @return The end reason.
         */
        [[nodiscard]] NetworkSessionEndReason getEndReasonProperty() const;

    private:
        NetworkSessionEndReason endReason_;
    };
}
