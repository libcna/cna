#pragma once

#include <string>

#include "System/EventArgs.hpp"
#include "System/Uri.hpp"

namespace Microsoft::Phone::Notification {

    /**
     * @brief Arguments for the event raised when a notification channel gets its URI.
     *
     * A channel's URI is not known when the channel is created, so an application learns it
     * from this event and only then has something to hand to the service that will push to it.
     */
    class NotificationChannelUriEventArgs : public System::EventArgs {
    public:
        /**
         * @brief Constructs the arguments.
         *
         * @param channelUri The channel's URI.
         */
        explicit NotificationChannelUriEventArgs(System::Uri channelUri)
            : channelUri_(std::move(channelUri))
        {
        }

        /**
         * @brief The channel's URI.
         *
         * @return The URI a service should push to.
         */
        [[nodiscard]] const System::Uri& getChannelUriProperty() const { return channelUri_; }

    private:
        System::Uri channelUri_;
    };

} // namespace Microsoft::Phone::Notification
