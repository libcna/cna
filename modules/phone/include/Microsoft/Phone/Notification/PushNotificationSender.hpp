// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "System/Uri.hpp"

namespace Microsoft::Phone::Notification {

    /** @brief How urgently a notification should be delivered. */
    enum class MessageSendPriority {
        /** @brief Deliver when convenient. */
        Low,
        /** @brief Deliver promptly. */
        Normal,
        /** @brief Deliver immediately. */
        High
    };

    /**
     * @brief A notification carrying bytes the receiving application interprets itself.
     *
     * @note The wire form is the one Windows Phone's push service defined and the receiving
     * end still expects: an HTTP POST whose body is the payload, with the headers that say what
     * kind of notification it is and how urgent. It is spelled out here rather than referenced,
     * because the service that used to define it is retired and there is nothing left to ask.
     */
    class RawPushNotificationMessage {
    public:
        /**
         * @brief Creates a raw notification.
         *
         * @param priority How urgently to deliver it.
         */
        explicit RawPushNotificationMessage(MessageSendPriority priority = MessageSendPriority::Normal)
            : priority_(priority)
        {
        }

        /** @brief The bytes to deliver. */
        std::vector<std::uint8_t> RawData;

        /**
         * @brief Sends the notification to a channel's URI.
         *
         * Returns as soon as the request is on its way; delivery is the receiver's business, and
         * a receiver that is not listening is not an error the sender can act on.
         *
         * @param clientUri The channel URI the receiver handed out.
         * @return True when the receiver accepted it.
         */
        bool SendAsync(const System::Uri& clientUri) const;

    private:
        MessageSendPriority priority_ = MessageSendPriority::Normal;
    };

    /**
     * @brief A notification the shell displays as a banner over whatever is on screen.
     *
     * @note There is no shell here to display it, and the header of HttpNotificationChannel says
     * why that cannot be helped. What this does is deliver it: the receiving application gets the
     * notification and can act on it. What it does not do is put it on top of another
     * application.
     */
    class ToastPushNotificationMessage {
    public:
        /**
         * @brief Creates a toast notification.
         *
         * @param priority How urgently to deliver it.
         */
        explicit ToastPushNotificationMessage(
            MessageSendPriority priority = MessageSendPriority::Normal)
            : priority_(priority)
        {
        }

        /** @brief The bold first line. */
        std::string Title;

        /** @brief The text after the title. */
        std::string SubTitle;

        /**
         * @brief Sends the notification to a channel's URI.
         *
         * @param clientUri The channel URI the receiver handed out.
         * @return True when the receiver accepted it.
         */
        bool SendAsync(const System::Uri& clientUri) const;

    private:
        MessageSendPriority priority_ = MessageSendPriority::Normal;
    };

} // namespace Microsoft::Phone::Notification
